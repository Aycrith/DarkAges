// [DATABASE_AGENT] Redis hot-state cache implementation with connection pooling
// WP-6-2: Implements <1ms latency, 10k ops/sec via connection pooling and pipelining

#include "db/RedisManager.hpp"
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
// Connection Pool Implementation
// ============================================================================

class RedisConnectionPool {
public:
    struct Connection {
        #ifdef REDIS_AVAILABLE
        redisContext* ctx{nullptr};
        #endif
        std::chrono::steady_clock::time_point lastUsed;
        bool inUse{false};
        uint64_t commandsExecuted{0};
    };

private:
    std::vector<Connection> connections_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string host_;
    uint16_t port_{6379};
    size_t maxPoolSize_{10};
    size_t minPoolSize_{2};
    std::atomic<bool> shutdown_{false};
    std::atomic<uint64_t> totalCommands_{0};
    std::atomic<uint64_t> failedCommands_{0};

    #ifdef REDIS_AVAILABLE
    redisContext* createConnection() {
        struct timeval timeout;
        timeout.tv_sec = Constants::REDIS_CONNECTION_TIMEOUT_MS / 1000;
        timeout.tv_usec = (Constants::REDIS_CONNECTION_TIMEOUT_MS % 1000) * 1000;
        
        redisContext* ctx = redisConnectWithTimeout(host_.c_str(), port_, timeout);
        if (!ctx || ctx->err) {
            if (ctx) {
                std::cerr << "[REDIS] Connection error: " << ctx->errstr << std::endl;
                redisFree(ctx);
            } else {
                std::cerr << "[REDIS] Failed to allocate context" << std::endl;
            }
            return nullptr;
        }
        
        // Enable TCP keepalive
        redisEnableKeepAlive(ctx);
        
        return ctx;
    }
    #endif

public:
    RedisConnectionPool() = default;
    ~RedisConnectionPool() { shutdown(); }

    bool initialize(const std::string& host, uint16_t port, size_t minPoolSize = 2, size_t maxPoolSize = 10) {
        host_ = host;
        port_ = port;
        minPoolSize_ = minPoolSize;
        maxPoolSize_ = maxPoolSize;
        shutdown_ = false;

        std::cout << "[REDIS] Initializing connection pool (" << minPoolSize_ << "-" << maxPoolSize_ << ")..." << std::endl;

        #ifdef REDIS_AVAILABLE
        // Create minimum pool size
        for (size_t i = 0; i < minPoolSize_; ++i) {
            auto* ctx = createConnection();
            if (!ctx) {
                std::cerr << "[REDIS] Failed to create initial connection " << i << std::endl;
                shutdown();
                return false;
            }
            
            Connection conn;
            conn.ctx = ctx;
            conn.lastUsed = std::chrono::steady_clock::now();
            conn.inUse = false;
            connections_.push_back(conn);
        }
        #endif

        std::cout << "[REDIS] Connection pool initialized with " << connections_.size() << " connections" << std::endl;
        return true;
    }

    #ifdef REDIS_AVAILABLE
    redisContext* acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        auto pred = [this]() {
            if (shutdown_) return true;
            // Check for available connection
            for (auto& conn : connections_) {
                if (!conn.inUse) return true;
            }
            // Can we create more?
            return connections_.size() < maxPoolSize_;
        };
        
        if (!cv_.wait_for(lock, timeout, pred)) {
            return nullptr; // Timeout
        }
        
        if (shutdown_) return nullptr;
        
        // Find existing available connection
        for (auto& conn : connections_) {
            if (!conn.inUse) {
                // Check if connection is still valid
                if (conn.ctx && conn.ctx->err) {
                    // Connection is dead, free it
                    redisFree(conn.ctx);
                    conn.ctx = createConnection();
                    if (!conn.ctx) continue;
                }
                
                conn.inUse = true;
                conn.lastUsed = std::chrono::steady_clock::now();
                return conn.ctx;
            }
        }
        
        // Create new connection if under limit
        if (connections_.size() < maxPoolSize_) {
            auto* ctx = createConnection();
            if (ctx) {
                Connection conn;
                conn.ctx = ctx;
                conn.lastUsed = std::chrono::steady_clock::now();
                conn.inUse = true;
                connections_.push_back(conn);
                return ctx;
            }
        }
        
        return nullptr;
    }

    void release(redisContext* ctx) {
        if (!ctx) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& conn : connections_) {
            if (conn.ctx == ctx) {
                if (ctx->err) {
                    // Connection is dead, remove it
                    redisFree(ctx);
                    conn.ctx = nullptr;
                    conn.inUse = false;
                    
                    // Create replacement if under min pool size
                    if (connections_.size() <= minPoolSize_) {
                        conn.ctx = createConnection();
                    }
                } else {
                    conn.inUse = false;
                    conn.lastUsed = std::chrono::steady_clock::now();
                }
                break;
            }
        }
        
        cv_.notify_one();
    }
    #endif

    void shutdown() {
        shutdown_ = true;
        cv_.notify_all();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        #ifdef REDIS_AVAILABLE
        for (auto& conn : connections_) {
            if (conn.ctx) {
                redisFree(conn.ctx);
                conn.ctx = nullptr;
            }
        }
        #endif
        
        connections_.clear();
    }

    [[nodiscard]] size_t getPoolSize() const { return connections_.size(); }
    [[nodiscard]] size_t getAvailableCount() const {
        size_t count = 0;
        for (const auto& conn : connections_) {
            if (!conn.inUse) count++;
        }
        return count;
    }
    
    void incrementCommands() { totalCommands_++; }
    void incrementFailed() { failedCommands_++; }
    [[nodiscard]] uint64_t getTotalCommands() const { return totalCommands_.load(); }
    [[nodiscard]] uint64_t getFailedCommands() const { return failedCommands_.load(); }
};

// ============================================================================
// Internal Implementation Structure
// ============================================================================

struct RedisInternal {
    std::unique_ptr<RedisConnectionPool> pool;
    std::string host;
    uint16_t port{6379};
    bool connected{false};
    
    // Async callback handling
    struct PendingCallback {
        std::function<void()> func;
        std::chrono::steady_clock::time_point enqueueTime;
    };
    std::queue<PendingCallback> callbackQueue;
    std::mutex callbackMutex;
    
    // Pub/Sub handling
    struct Subscription {
        std::string channel;
        std::function<void(std::string_view, std::string_view)> callback;
    };
    std::vector<Subscription> subscriptions;
    std::mutex subMutex;
    
    // Pub/Sub listener thread
    #ifdef REDIS_AVAILABLE
    redisContext* pubSubCtx{nullptr};
    #endif
    std::atomic<bool> pubSubRunning{false};
    std::thread pubSubThread;
    std::queue<std::pair<std::string, std::string>> messageQueue;
    std::mutex messageMutex;
    
    // Latency tracking
    std::queue<float> latencySamples;
    std::mutex latencyMutex;
    static constexpr size_t MAX_LATENCY_SAMPLES = 100;
    
    // Metrics
    std::atomic<uint64_t> commandsSent_{0};
    std::atomic<uint64_t> commandsCompleted_{0};
    std::atomic<uint64_t> commandsFailed_{0};
};

// ============================================================================
// Binary Serialization Helpers
// ============================================================================

// PlayerSession binary format:
// playerId(8) + zoneId(4) + connectionId(4) + pos.x(4) + pos.y(4) + pos.z(4) + 
// pos.timestamp(4) + health(4) + lastActivity(8) + username(32) = 76 bytes
static constexpr size_t PLAYER_SESSION_SIZE = 76;

static void WriteUint64(std::vector<uint8_t>& data, uint64_t value) {
    data.push_back(static_cast<uint8_t>(value));
    data.push_back(static_cast<uint8_t>(value >> 8));
    data.push_back(static_cast<uint8_t>(value >> 16));
    data.push_back(static_cast<uint8_t>(value >> 24));
    data.push_back(static_cast<uint8_t>(value >> 32));
    data.push_back(static_cast<uint8_t>(value >> 40));
    data.push_back(static_cast<uint8_t>(value >> 48));
    data.push_back(static_cast<uint8_t>(value >> 56));
}

static void WriteUint32(std::vector<uint8_t>& data, uint32_t value) {
    data.push_back(static_cast<uint8_t>(value));
    data.push_back(static_cast<uint8_t>(value >> 8));
    data.push_back(static_cast<uint8_t>(value >> 16));
    data.push_back(static_cast<uint8_t>(value >> 24));
}

static void WriteInt32(std::vector<uint8_t>& data, int32_t value) {
    WriteUint32(data, static_cast<uint32_t>(value));
}

static uint64_t ReadUint64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0]) |
           (static_cast<uint64_t>(data[1]) << 8) |
           (static_cast<uint64_t>(data[2]) << 16) |
           (static_cast<uint64_t>(data[3]) << 24) |
           (static_cast<uint64_t>(data[4]) << 32) |
           (static_cast<uint64_t>(data[5]) << 40) |
           (static_cast<uint64_t>(data[6]) << 48) |
           (static_cast<uint64_t>(data[7]) << 56);
}

static uint32_t ReadUint32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

static int32_t ReadInt32(const uint8_t* data) {
    return static_cast<int32_t>(ReadUint32(data));
}

static std::vector<uint8_t> SerializePlayerSession(const PlayerSession& session) {
    std::vector<uint8_t> data;
    data.reserve(PLAYER_SESSION_SIZE);
    
    // playerId (8 bytes)
    WriteUint64(data, session.playerId);
    
    // zoneId (4 bytes)
    WriteUint32(data, session.zoneId);
    
    // connectionId (4 bytes)
    WriteUint32(data, session.connectionId);
    
    // position.x (4 bytes) - fixed point
    WriteInt32(data, session.position.x);
    
    // position.y (4 bytes)
    WriteInt32(data, session.position.y);
    
    // position.z (4 bytes)
    WriteInt32(data, session.position.z);
    
    // position.timestamp_ms (4 bytes)
    WriteUint32(data, session.position.timestamp_ms);
    
    // health (4 bytes)
    WriteInt32(data, session.health);
    
    // lastActivity (8 bytes)
    WriteUint64(data, session.lastActivity);
    
    // username (32 bytes, null-padded)
    for (size_t i = 0; i < 32; ++i) {
        data.push_back(i < 32 && session.username[i] != '\0' ? 
                      static_cast<uint8_t>(session.username[i]) : 0);
    }
    
    return data;
}

static bool DeserializePlayerSession(const uint8_t* data, size_t len, PlayerSession& session) {
    if (len < PLAYER_SESSION_SIZE) {
        return false;
    }
    
    size_t offset = 0;
    
    // playerId
    session.playerId = ReadUint64(data + offset);
    offset += 8;
    
    // zoneId
    session.zoneId = ReadUint32(data + offset);
    offset += 4;
    
    // connectionId
    session.connectionId = ReadUint32(data + offset);
    offset += 4;
    
    // position.x
    session.position.x = ReadInt32(data + offset);
    offset += 4;
    
    // position.y
    session.position.y = ReadInt32(data + offset);
    offset += 4;
    
    // position.z
    session.position.z = ReadInt32(data + offset);
    offset += 4;
    
    // position.timestamp_ms
    session.position.timestamp_ms = ReadUint32(data + offset);
    offset += 4;
    
    // health
    session.health = ReadInt32(data + offset);
    offset += 4;
    
    // lastActivity
    session.lastActivity = ReadUint64(data + offset);
    offset += 8;
    
    // username (copy up to 31 chars, ensure null termination)
    for (size_t i = 0; i < 31; ++i) {
        session.username[i] = static_cast<char>(data[offset + i]);
    }
    session.username[31] = '\0';
    
    return true;
}

// Position binary format: x(4) + y(4) + z(4) + timestamp(4) = 16 bytes
static constexpr size_t POSITION_SIZE = 16;

static std::vector<uint8_t> SerializePosition(const Position& pos) {
    std::vector<uint8_t> data;
    data.reserve(POSITION_SIZE);
    
    WriteInt32(data, pos.x);
    WriteInt32(data, pos.y);
    WriteInt32(data, pos.z);
    WriteUint32(data, pos.timestamp_ms);
    
    return data;
}

// ============================================================================
// Pub/Sub Listener Thread
// ============================================================================

#ifdef REDIS_AVAILABLE
static void PubSubListener(RedisInternal* internal) {
    while (internal->pubSubRunning) {
        redisReply* reply = nullptr;
        
        if (internal->pubSubCtx && redisGetReply(internal->pubSubCtx, (void**)&reply) == REDIS_OK) {
            if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
                // Subscription message format: [message, channel, data]
                std::string_view msgType(reply->element[0]->str, reply->element[0]->len);
                if (msgType == "message" || msgType == "pmessage") {
                    size_t channelIdx = (msgType == "pmessage") ? 2 : 1;
                    size_t messageIdx = (msgType == "pmessage") ? 3 : 2;
                    
                    if (reply->elements > messageIdx) {
                        std::string channel(reply->element[channelIdx]->str, reply->element[channelIdx]->len);
                        std::string message(reply->element[messageIdx]->str, reply->element[messageIdx]->len);
                        
                        {
                            std::lock_guard<std::mutex> lock(internal->messageMutex);
                            internal->messageQueue.emplace(std::move(channel), std::move(message));
                        }
                    }
                }
            }
            freeReplyObject(reply);
        } else {
            // Error or shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
#endif

// ============================================================================
// RedisManager Implementation
// ============================================================================

RedisManager::RedisManager() 
    : internal_(std::make_unique<RedisInternal>()) {}

RedisManager::~RedisManager() {
    shutdown();
}

bool RedisManager::initialize(const std::string& host, uint16_t port) {
    internal_->host = host;
    internal_->port = port;
    
    std::cout << "[REDIS] Connecting to " << host << ":" << port << "..." << std::endl;
    
    #ifdef REDIS_AVAILABLE
    // Initialize connection pool
    internal_->pool = std::make_unique<RedisConnectionPool>();
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
    return true;
}

void RedisManager::shutdown() {
    if (!internal_->connected) return;
    
    std::cout << "[REDIS] Shutting down..." << std::endl;
    
    #ifdef REDIS_AVAILABLE
    // Stop pub/sub thread
    internal_->pubSubRunning = false;
    if (internal_->pubSubThread.joinable()) {
        internal_->pubSubThread.join();
    }
    if (internal_->pubSubCtx) {
        redisFree(internal_->pubSubCtx);
        internal_->pubSubCtx = nullptr;
    }
    
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
    if (!internal_->connected) return;
    
    // Process pending callbacks
    std::queue<RedisInternal::PendingCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        callbacks.swap(internal_->callbackQueue);
    }
    
    while (!callbacks.empty()) {
        callbacks.front().func();
        callbacks.pop();
    }
    
    // Process pub/sub messages
    processSubscriptions();
}

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
// Player Session Operations
// ============================================================================

void RedisManager::savePlayerSession(const PlayerSession& session, SetCallback callback) {
    std::string key = RedisKeys::playerSession(session.playerId);
    auto data = SerializePlayerSession(session);
    
    set(key, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
        Constants::REDIS_KEY_TTL_SECONDS, callback);
}

void RedisManager::loadPlayerSession(uint64_t playerId, SessionCallback callback) {
    std::string key = RedisKeys::playerSession(playerId);
    
    get(key, [playerId, callback](const AsyncResult<std::string>& result) {
        AsyncResult<PlayerSession> sessionResult;
        
        if (!result.success) {
            sessionResult.success = false;
            sessionResult.error = result.error;
        } else {
            PlayerSession session;
            session.playerId = playerId;
            
            if (DeserializePlayerSession(
                    reinterpret_cast<const uint8_t*>(result.value.data()),
                    result.value.size(), session)) {
                sessionResult.success = true;
                sessionResult.value = session;
            } else {
                sessionResult.success = false;
                sessionResult.error = "Failed to deserialize session data";
            }
        }
        
        if (callback) callback(sessionResult);
    });
}

void RedisManager::removePlayerSession(uint64_t playerId, SetCallback callback) {
    del(RedisKeys::playerSession(playerId), callback);
}

void RedisManager::updatePlayerPosition(uint64_t playerId, const Position& pos,
                                       uint32_t timestamp, SetCallback callback) {
    (void)timestamp;  // Timestamp is included in serialized position
    
    std::string key = RedisKeys::playerPosition(playerId);
    auto data = SerializePosition(pos);
    
    set(key, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
        Constants::REDIS_KEY_TTL_SECONDS, callback);
}

// ============================================================================
// Zone Operations
// ============================================================================

void RedisManager::addPlayerToZone(uint32_t zoneId, uint64_t playerId, SetCallback callback) {
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
    
    std::string key = RedisKeys::zonePlayers(zoneId);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SADD %s %llu",
                                                  key.c_str(),
                                                  static_cast<unsigned long long>(playerId));
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
    (void)zoneId;
    (void)playerId;
    internal_->commandsCompleted_++;
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback]() { callback(true); }, std::chrono::steady_clock::now()});
    }
    #endif
}

void RedisManager::removePlayerFromZone(uint32_t zoneId, uint64_t playerId, SetCallback callback) {
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
    
    std::string key = RedisKeys::zonePlayers(zoneId);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SREM %s %llu",
                                                  key.c_str(),
                                                  static_cast<unsigned long long>(playerId));
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
    (void)zoneId;
    (void)playerId;
    internal_->commandsCompleted_++;
    if (callback) {
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback]() { callback(true); }, std::chrono::steady_clock::now()});
    }
    #endif
}

void RedisManager::getZonePlayers(uint32_t zoneId, 
                                 std::function<void(const AsyncResult<std::vector<uint64_t>>&)> callback) {
    internal_->commandsSent_++;
    
    if (!internal_->connected || !callback) {
        internal_->commandsFailed_++;
        if (callback) {
            AsyncResult<std::vector<uint64_t>> result;
            result.success = false;
            result.error = "Not connected";
            std::lock_guard<std::mutex> lock(internal_->callbackMutex);
            internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
        }
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    auto* ctx = internal_->pool->acquire();
    if (!ctx) {
        internal_->commandsFailed_++;
        AsyncResult<std::vector<uint64_t>> result;
        result.success = false;
        result.error = "Failed to acquire connection";
        std::lock_guard<std::mutex> lock(internal_->callbackMutex);
        internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
        return;
    }
    
    std::string key = RedisKeys::zonePlayers(zoneId);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SMEMBERS %s", key.c_str());
    
    AsyncResult<std::vector<uint64_t>> result;
    if (!reply) {
        result.success = false;
        result.error = "Command failed";
        internal_->commandsFailed_++;
    } else if (reply->type == REDIS_REPLY_ARRAY) {
        result.success = true;
        result.value.reserve(reply->elements);
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING ||
                reply->element[i]->type == REDIS_REPLY_INTEGER) {
                uint64_t playerId = 0;
                if (reply->element[i]->type == REDIS_REPLY_INTEGER) {
                    playerId = static_cast<uint64_t>(reply->element[i]->integer);
                } else {
                    playerId = std::stoull(std::string(reply->element[i]->str, reply->element[i]->len));
                }
                result.value.push_back(playerId);
            }
        }
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
    
    std::lock_guard<std::mutex> lock(internal_->callbackMutex);
    internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
    #else
    (void)zoneId;
    internal_->commandsCompleted_++;
    AsyncResult<std::vector<uint64_t>> result;
    result.success = true;
    std::lock_guard<std::mutex> lock(internal_->callbackMutex);
    internal_->callbackQueue.push({[callback, result]() { callback(result); }, std::chrono::steady_clock::now()});
    #endif
}

// ============================================================================
// Pub/Sub Operations
// ============================================================================

void RedisManager::publish(std::string_view channel, std::string_view message) {
    if (!internal_->connected) return;
    
    internal_->commandsSent_++;
    
    #ifdef REDIS_AVAILABLE
    auto* ctx = internal_->pool->acquire();
    if (!ctx) {
        internal_->commandsFailed_++;
        return;
    }
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "PUBLISH %b %b",
                                                  channel.data(), channel.size(),
                                                  message.data(), message.size());
    
    if (reply) {
        freeReplyObject(reply);
        internal_->commandsCompleted_++;
    } else {
        internal_->commandsFailed_++;
    }
    
    internal_->pool->release(ctx);
    #else
    (void)channel;
    (void)message;
    internal_->commandsCompleted_++;
    #endif
}

void RedisManager::subscribe(std::string_view channel,
                            std::function<void(std::string_view, std::string_view)> callback) {
    if (!internal_->connected) return;
    
    {
        std::lock_guard<std::mutex> lock(internal_->subMutex);
        RedisInternal::Subscription sub;
        sub.channel = std::string(channel);
        sub.callback = std::move(callback);
        internal_->subscriptions.push_back(std::move(sub));
    }
    
    #ifdef REDIS_AVAILABLE
    // Start pub/sub listener if not running
    if (!internal_->pubSubRunning) {
        internal_->pubSubRunning = true;
        
        // Create dedicated pub/sub connection
        struct timeval timeout;
        timeout.tv_sec = Constants::REDIS_CONNECTION_TIMEOUT_MS / 1000;
        timeout.tv_usec = (Constants::REDIS_CONNECTION_TIMEOUT_MS % 1000) * 1000;
        
        internal_->pubSubCtx = redisConnectWithTimeout(internal_->host.c_str(), internal_->port, timeout);
        if (internal_->pubSubCtx && !internal_->pubSubCtx->err) {
            internal_->pubSubThread = std::thread(PubSubListener, internal_.get());
        } else {
            std::cerr << "[REDIS] Failed to create pub/sub connection" << std::endl;
            internal_->pubSubRunning = false;
            if (internal_->pubSubCtx) {
                redisFree(internal_->pubSubCtx);
                internal_->pubSubCtx = nullptr;
            }
        }
    }
    
    // Subscribe to channel
    if (internal_->pubSubCtx && !internal_->pubSubCtx->err) {
        redisReply* reply = (redisReply*)redisCommand(internal_->pubSubCtx, "SUBSCRIBE %b",
                                                      channel.data(), channel.size());
        if (reply) {
            freeReplyObject(reply);
        }
    }
    #else
    (void)channel;
    #endif
}

void RedisManager::unsubscribe(std::string_view channel) {
    if (!internal_->connected) return;
    
    {
        std::lock_guard<std::mutex> lock(internal_->subMutex);
        auto& subs = internal_->subscriptions;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
            [channel](const auto& sub) { return sub.channel == channel; }), subs.end());
    }
    
    #ifdef REDIS_AVAILABLE
    if (internal_->pubSubCtx && !internal_->pubSubCtx->err) {
        redisReply* reply = (redisReply*)redisCommand(internal_->pubSubCtx, "UNSUBSCRIBE %b",
                                                      channel.data(), channel.size());
        if (reply) {
            freeReplyObject(reply);
        }
    }
    #endif
    
    std::cout << "[REDIS] Unsubscribed from " << channel << std::endl;
}

void RedisManager::processSubscriptions() {
    if (!internal_->connected) return;
    
    // Process queued messages
    std::queue<std::pair<std::string, std::string>> messages;
    {
        std::lock_guard<std::mutex> lock(internal_->messageMutex);
        messages.swap(internal_->messageQueue);
    }
    
    while (!messages.empty()) {
        const auto& [channel, message] = messages.front();
        
        std::lock_guard<std::mutex> lock(internal_->subMutex);
        for (const auto& sub : internal_->subscriptions) {
            // Simple channel matching (exact match for now, could use pattern matching)
            if (sub.channel == channel) {
                sub.callback(channel, message);
            }
        }
        
        messages.pop();
    }
}

void RedisManager::publishToZone(uint32_t zoneId, const ZoneMessage& message) {
    std::string channel = "zone:" + std::to_string(zoneId) + ":messages";
    auto data = message.serialize();
    publish(channel, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

void RedisManager::broadcastToAllZones(const ZoneMessage& message) {
    std::string channel = "zone:broadcast";
    auto data = message.serialize();
    publish(channel, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

void RedisManager::subscribeToZoneChannel(uint32_t myZoneId,
                                          std::function<void(const ZoneMessage&)> callback) {
    std::string channel = "zone:" + std::to_string(myZoneId) + ":messages";
    
    subscribe(channel, [callback](std::string_view ch, std::string_view msg) {
        std::vector<uint8_t> data(msg.begin(), msg.end());
        auto message = ZoneMessage::deserialize(data);
        if (message) {
            callback(*message);
        }
    });
    
    // Also subscribe to broadcast channel
    subscribe("zone:broadcast", [callback](std::string_view ch, std::string_view msg) {
        std::vector<uint8_t> data(msg.begin(), msg.end());
        auto message = ZoneMessage::deserialize(data);
        if (message) {
            callback(*message);
        }
    });
}

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
// ZoneMessage Serialization
// ============================================================================

std::vector<uint8_t> ZoneMessage::serialize() const {
    std::vector<uint8_t> data;
    
    // Header: type(1) + source(4) + target(4) + timestamp(4) + sequence(4) + payload_size(4)
    size_t headerSize = 1 + 4 + 4 + 4 + 4 + 4;
    data.resize(headerSize + payload.size());
    
    size_t offset = 0;
    data[offset++] = static_cast<uint8_t>(type);
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = sourceZoneId;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = targetZoneId;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = timestamp;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = sequence;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = static_cast<uint32_t>(payload.size());
    offset += 4;
    
    std::memcpy(&data[offset], payload.data(), payload.size());
    
    return data;
}

std::optional<ZoneMessage> ZoneMessage::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 21) {  // Minimum header size
        return std::nullopt;
    }
    
    ZoneMessage msg;
    size_t offset = 0;
    
    msg.type = static_cast<ZoneMessageType>(data[offset++]);
    
    msg.sourceZoneId = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.targetZoneId = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.timestamp = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.sequence = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    uint32_t payloadSize = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    if (data.size() < offset + payloadSize) {
        return std::nullopt;
    }
    
    msg.payload.assign(data.begin() + offset, data.begin() + offset + payloadSize);
    
    return msg;
}

} // namespace DarkAges
