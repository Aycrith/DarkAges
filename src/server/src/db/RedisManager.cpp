// [DATABASE_AGENT] Redis hot-state cache implementation with connection pooling
// WP-6-2: Implements <1ms latency, 10k ops/sec via connection pooling and pipelining
// Facade: delegates player sessions to PlayerSessionManager, pub/sub to PubSubManager

#include "db/RedisManager.hpp"
#include "db/PlayerSessionManager.hpp"
#include "db/PubSubManager.hpp"
#include "db/ZoneManager.hpp"
#include "db/StreamManager.hpp"
#include "db/RedisInternal.hpp"
#include "db/ConnectionPool.hpp"
#include "Constants.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <algorithm>

#ifdef REDIS_AVAILABLE
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <hiredis.h>
#endif

namespace DarkAges {

// ============================================================================
// RedisManager Implementation
// ============================================================================

RedisManager::RedisManager() 
    : internal_(std::make_unique<RedisInternal>()) {}

RedisManager::~RedisManager() {
    // Explicitly clean up sub-managers while internal_ is still alive
    // (they may reference internal_ or pool connections in their destructors)
    sessionManager_.reset();
    zoneManager_.reset();
    streamManager_.reset();
    #ifdef REDIS_AVAILABLE
    pubSubManager_.reset();
    #endif
    // internal_ destroyed automatically after this
}

bool RedisManager::initialize(const std::string& host, uint16_t port) {
    internal_->host = host;
    internal_->port = port;
    
    std::cout << "[REDIS] Connecting to " << host << ":" << port << "..." << std::endl;
    
    #ifdef REDIS_AVAILABLE
    // Initialize connection pool
    internal_->pool = std::make_unique<ConnectionPool>();
    if (!internal_->pool->initialize(host, port, 2, 10)) {
        std::cerr << "[REDIS] Failed to initialize connection pool" << std::endl;
        return false;
    }
    
    // Test connection
    auto* ctx = internal_->pool->acquire(std::chrono::milliseconds(1000));
    if (!ctx) {
        std::cerr << "[REDIS] Failed to acquire connection from pool" << std::endl;
        return false;
    }
    
    // Test with PING
    redisReply* reply = (redisReply*)redisCommand(ctx, "PING");
    if (!reply || reply->type != REDIS_REPLY_STATUS || std::string(reply->str) != "PONG") {
        std::cerr << "[REDIS] PING failed" << std::endl;
        freeReplyObject(reply);
        internal_->pool->release(ctx);
        return false;
    }
    freeReplyObject(reply);
    internal_->pool->release(ctx);
    
    std::cout << "[REDIS] Connection pool initialized successfully" << std::endl;
    #else
    std::cout << "[REDIS] hiredis not available - using stub implementation" << std::endl;
    #endif
    
    internal_->connected = true;
    
    // Create sub-managers
    sessionManager_ = std::make_unique<PlayerSessionManager>(*this);
    zoneManager_ = std::make_unique<ZoneManager>(*this, *internal_);
    streamManager_ = std::make_unique<StreamManager>(*this, *internal_);
    #ifdef REDIS_AVAILABLE
    pubSubManager_ = std::make_unique<PubSubManager>(*this, *internal_);
    #endif
    
    return true;
}

void RedisManager::shutdown() {
    if (!internal_->connected) return;
    
    std::cout << "[REDIS] Shutting down..." << std::endl;
    
    #ifdef REDIS_AVAILABLE
    // Shutdown connection pool
    if (internal_->pool) {
        internal_->pool->shutdown();
    }
    #endif
    
    internal_->connected = false;
    std::cout << "[REDIS] Shutdown complete" << std::endl;
}

bool RedisManager::isConnected() const {
    return internal_->connected;
}

void RedisManager::update() {
    // Always process pending callbacks, even when disconnected
    // (queued operations need to fire their failure callbacks)
    std::queue<RedisInternal::PendingCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        callbacks.swap(internal_->callbackQueue);
    }
    
    while (!callbacks.empty()) {
        callbacks.front().func();
        callbacks.pop();
    }
    
    if (!internal_->connected) return;
    
    #ifdef REDIS_AVAILABLE
    // Process pub/sub messages via PubSubManager
    if (pubSubManager_) {
        pubSubManager_->processSubscriptions();
    }
    #endif
}

// ============================================================================
// Key-Value Operations (Core Redis operations - kept in facade)
// ============================================================================

void RedisManager::set(std::string_view key, std::string_view value,
                      uint32_t ttlSeconds, SetCallback callback) {
    internal_->commandsSent_++;
    
    if (!internal_->connected) {
        internal_->commandsFailed_++;
        if (callback) {
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback]() { callback(false); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    auto start = std::chrono::high_resolution_clock::now();
    
    auto* ctx = internal_->pool->acquire();
    if (!ctx) {
        internal_->commandsFailed_++;
        if (callback) {
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback]() { callback(false); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    redisReply* reply;
    if (ttlSeconds > 0) {
        reply = (redisReply*)redisCommand(ctx, "SET %b %b EX %u",
                                          key.data(), key.size(),
                                          value.data(), value.size(),
                                          ttlSeconds);
    } else {
        reply = (redisReply*)redisCommand(ctx, "SET %b %b",
                                          key.data(), key.size(),
                                          value.data(), value.size());
    }
    
    bool success = (reply && reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
    
    if (reply) {
        freeReplyObject(reply);
    }
    
    internal_->pool->release(ctx);
    
    // Track latency
    auto end = std::chrono::high_resolution_clock::now();
    float latencyMs = std::chrono::duration<float, std::milli>(end - start).count();
    {
        std::lock_guard<std::mutex> lock(internal_->latencyMutex);
        internal_->latencySamples.push(latencyMs);
        if (internal_->latencySamples.size() > RedisInternal::MAX_LATENCY_SAMPLES) {
            internal_->latencySamples.pop();
        }
    }
    
    if (success) {
        internal_->commandsCompleted_++;
    } else {
        internal_->commandsFailed_++;
    }
    
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback, success]() { callback(success); }, std::chrono::steady_clock::now()});
    }
    #else
    // Stub
    internal_->commandsCompleted_++;
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback]() { callback(true); }, std::chrono::steady_clock::now()});
    }
    #endif
}

void RedisManager::get(std::string_view key, GetCallback callback) {
    internal_->commandsSent_++;
    
    if (!internal_->connected || !callback) {
        internal_->commandsFailed_++;
        if (callback) {
            AsyncResult<std::string> result;
            result.success = false;
            result.error = "Not connected";
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    auto start = std::chrono::high_resolution_clock::now();
    
    auto* ctx = internal_->pool->acquire();
    if (!ctx) {
        internal_->commandsFailed_++;
        AsyncResult<std::string> result;
        result.success = false;
        result.error = "Failed to acquire connection";
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
        return;
    }
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %b", key.data(), key.size());
    
    AsyncResult<std::string> result;
    if (!reply) {
        result.success = false;
        result.error = "Command failed";
        internal_->commandsFailed_++;
    } else if (reply->type == REDIS_REPLY_NIL) {
        result.success = false;
        result.error = "Key not found";
        internal_->commandsFailed_++;
    } else if (reply->type == REDIS_REPLY_STRING) {
        result.success = true;
        result.value = std::string(reply->str, reply->len);
        internal_->commandsCompleted_++;
    } else {
        result.success = false;
        result.error = "Unexpected reply type";
        internal_->commandsFailed_++;
    }
    
    if (reply) {
        freeReplyObject(reply);
    }
    
    internal_->pool->release(ctx);
    
    // Track latency
    auto end = std::chrono::high_resolution_clock::now();
    float latencyMs = std::chrono::duration<float, std::milli>(end - start).count();
    {
        std::lock_guard<std::mutex> lock(internal_->latencyMutex);
        internal_->latencySamples.push(latencyMs);
        if (internal_->latencySamples.size() > RedisInternal::MAX_LATENCY_SAMPLES) {
            internal_->latencySamples.pop();
        }
    }
    
    std::lock_guard<std::mutex> lock(internal_->callbackMutex);
    internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
    #else
    // Stub
    internal_->commandsCompleted_++;
    AsyncResult<std::string> result;
    result.success = true;
    result.value = "stub_value";
    std::lock_guard<std::mutex> lock(internal_->callbackMutex);
    internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
    #endif
}

void RedisManager::del(std::string_view key, SetCallback callback) {
    internal_->commandsSent_++;
    
    if (!internal_->connected) {
        internal_->commandsFailed_++;
        if (callback) {
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback]() { callback(false); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    auto* ctx = internal_->pool->acquire();
    if (!ctx) {
        internal_->commandsFailed_++;
        if (callback) {
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback]() { callback(false); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %b", key.data(), key.size());
    bool success = (reply != nullptr && reply->type == REDIS_REPLY_INTEGER);
    
    if (reply) {
        freeReplyObject(reply);
    }
    
    internal_->pool->release(ctx);
    
    if (success) {
        internal_->commandsCompleted_++;
    } else {
        internal_->commandsFailed_++;
    }
    
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback, success]() { callback(success); }, std::chrono::steady_clock::now()});
    }
    #else
    internal_->commandsCompleted_++;
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback]() { callback(true); }, std::chrono::steady_clock::now()});
    }
    #endif
}

// ============================================================================
// Player Session Operations - Delegated to PlayerSessionManager
// ============================================================================

void RedisManager::savePlayerSession(const PlayerSession& session, SetCallback callback) {
    sessionManager_->savePlayerSession(session, callback);
}

void RedisManager::loadPlayerSession(uint64_t playerId, SessionCallback callback) {
    sessionManager_->loadPlayerSession(playerId, callback);
}

void RedisManager::removePlayerSession(uint64_t playerId, SetCallback callback) {
    sessionManager_->removePlayerSession(playerId, callback);
}

void RedisManager::updatePlayerPosition(uint64_t playerId, const Position& pos,
                                       uint32_t timestamp, SetCallback callback) {
    sessionManager_->updatePlayerPosition(playerId, pos, timestamp, callback);
}

// ============================================================================
// Zone Operations - Delegated to ZoneManager
// ============================================================================

void RedisManager::addPlayerToZone(uint32_t zoneId, uint64_t playerId, SetCallback callback) {
    zoneManager_->addPlayerToZone(zoneId, playerId, callback);
}

void RedisManager::removePlayerFromZone(uint32_t zoneId, uint64_t playerId, SetCallback callback) {
    zoneManager_->removePlayerFromZone(zoneId, playerId, callback);
}

void RedisManager::getZonePlayers(uint32_t zoneId, 
                                 std::function<void(const AsyncResult<std::vector<uint64_t>>&)> callback) {
    zoneManager_->getZonePlayers(zoneId, std::move(callback));
}

// ============================================================================
// Pub/Sub Operations - Delegated to PubSubManager
// ============================================================================

#ifdef REDIS_AVAILABLE

void RedisManager::publish(std::string_view channel, std::string_view message) {
    pubSubManager_->publish(channel, message);
}

void RedisManager::subscribe(std::string_view channel,
                            std::function<void(std::string_view, std::string_view)> callback) {
    pubSubManager_->subscribe(channel, std::move(callback));
}

void RedisManager::unsubscribe(std::string_view channel) {
    pubSubManager_->unsubscribe(channel);
}

void RedisManager::processSubscriptions() {
    pubSubManager_->processSubscriptions();
}

void RedisManager::publishToZone(uint32_t zoneId, const ZoneMessage& message) {
    pubSubManager_->publishToZone(zoneId, message);
}

void RedisManager::broadcastToAllZones(const ZoneMessage& message) {
    pubSubManager_->broadcastToAllZones(message);
}

void RedisManager::subscribeToZoneChannel(uint32_t myZoneId,
                                          std::function<void(const ZoneMessage&)> callback) {
    pubSubManager_->subscribeToZoneChannel(myZoneId, std::move(callback));
}

#endif // REDIS_AVAILABLE

// ============================================================================
// Metrics
// ============================================================================

uint64_t RedisManager::getCommandsSent() const {
    return internal_->commandsSent_.load();
}

uint64_t RedisManager::getCommandsCompleted() const {
    return internal_->commandsCompleted_.load();
}

uint64_t RedisManager::getCommandsFailed() const {
    return internal_->commandsFailed_.load();
}

float RedisManager::getAverageLatencyMs() const {
    std::lock_guard<std::mutex> lock(internal_->latencyMutex);
    
    if (internal_->latencySamples.empty()) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    auto samples = internal_->latencySamples;
    while (!samples.empty()) {
        sum += samples.front();
        samples.pop();
    }
    
    return sum / internal_->latencySamples.size();
}

// ============================================================================
// Pipeline Batching for High Throughput
// ============================================================================

void RedisManager::pipelineSet(const std::vector<std::pair<std::string, std::string>>& kvs,
                               uint32_t ttlSeconds, SetCallback callback) {
    internal_->commandsSent_ += kvs.size();
    
    if (!internal_->connected || kvs.empty()) {
        internal_->commandsFailed_ += kvs.size();
        if (callback) {
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback]() { callback(false); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    auto* ctx = internal_->pool->acquire();
    if (!ctx) {
        internal_->commandsFailed_ += kvs.size();
        if (callback) {
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback]() { callback(false); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    // Append all commands to pipeline
    int status = REDIS_OK;
    for (const auto& [k, v] : kvs) {
        if (ttlSeconds > 0) {
            status = redisAppendCommand(ctx, "SET %s %s EX %u", k.c_str(), v.c_str(), ttlSeconds);
        } else {
            status = redisAppendCommand(ctx, "SET %s %s", k.c_str(), v.c_str());
        }
        if (status != REDIS_OK) break;
    }
    
    bool allSuccess = (status == REDIS_OK);
    
    // Get all replies
    if (allSuccess) {
        for (size_t i = 0; i < kvs.size(); ++i) {
            redisReply* reply = nullptr;
            if (redisGetReply(ctx, (void**)&reply) == REDIS_OK) {
                if (reply && reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK") {
                    internal_->commandsCompleted_++;
                } else {
                    internal_->commandsFailed_++;
                    allSuccess = false;
                }
                freeReplyObject(reply);
            } else {
                internal_->commandsFailed_++;
                allSuccess = false;
            }
        }
    } else {
        internal_->commandsFailed_ += kvs.size();
    }
    
    internal_->pool->release(ctx);
    
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback, allSuccess]() { callback(allSuccess); }, std::chrono::steady_clock::now()});
    }
    #else
    internal_->commandsCompleted_ += kvs.size();
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback]() { callback(true); }, std::chrono::steady_clock::now()});
    }
    #endif
}

// ============================================================================
// Redis Streams - Delegated to StreamManager
// ============================================================================

void RedisManager::xadd(std::string_view streamKey,
                        std::string_view id,
                        const std::unordered_map<std::string, std::string>& fields,
                        StreamAddCallback callback) {
    streamManager_->xadd(streamKey, id, fields, std::move(callback));
}

void RedisManager::xread(std::string_view streamKey,
                         std::string_view lastId,
                         StreamReadCallback callback,
                         uint32_t count,
                         uint32_t blockMs) {
    streamManager_->xread(streamKey, lastId, std::move(callback), count, blockMs);
}

} // namespace DarkAges
