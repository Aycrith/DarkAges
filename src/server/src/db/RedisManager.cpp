// [DATABASE_AGENT] Redis hot-state cache implementation
// Real hiredis async integration for non-blocking operations

#include "db/RedisManager.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <queue>

#ifdef REDIS_AVAILABLE
// hiredis headers
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#endif

namespace DarkAges {

// ============================================================================
// Internal Implementation Structure
// ============================================================================

struct PendingCommand {
    uint64_t id{0};
    std::chrono::steady_clock::time_point submitTime;
    #ifdef REDIS_AVAILABLE
    std::function<void(redisReply*)> callback;
    #else
    std::function<void(const std::string&)> callback;
    #endif
};

struct RedisInternal {
    #ifdef REDIS_AVAILABLE
    redisAsyncContext* context{nullptr};
    #endif
    bool connected{false};
    std::string host;
    uint16_t port{6379};
    
    // Command tracking
    uint64_t nextCommandId{1};
    std::unordered_map<uint64_t, PendingCommand> pendingCommands;
    
    // Callback storage for pub/sub
    std::function<void(std::string_view, std::string_view)> subscribeCallback;
    
    // Latency tracking for rolling average
    std::queue<float> latencySamples;
    static constexpr size_t MAX_LATENCY_SAMPLES = 100;
    
    // Connection callbacks
    std::function<void(bool, const std::string&)> connectCallback;
    
    // === Pub/Sub state ===
    struct Subscription {
        std::string channel;
        std::function<void(std::string_view, std::string_view)> callback;
    };
    std::vector<Subscription> subscriptions;
    std::vector<std::pair<std::string, std::string>> messageQueue;
    std::mutex messageMutex;
    uint32_t messageSequence{0};
};

#ifdef REDIS_AVAILABLE
// ============================================================================
// hiredis Callbacks
// ============================================================================

static void OnConnect(const redisAsyncContext* ctx, int status) {
    auto* internal = static_cast<RedisInternal*>(ctx->data);
    if (internal == nullptr) return;
    
    if (status == REDIS_OK) {
        std::cout << "[REDIS] Connected to " << internal->host << ":" << internal->port << std::endl;
        internal->connected = true;
        if (internal->connectCallback) {
            internal->connectCallback(true, "");
        }
    } else {
        std::cerr << "[REDIS] Connection failed: " << ctx->errstr << std::endl;
        internal->connected = false;
        if (internal->connectCallback) {
            internal->connectCallback(false, ctx->errstr ? ctx->errstr : "Unknown error");
        }
    }
}

static void OnDisconnect(const redisAsyncContext* ctx, int status) {
    auto* internal = static_cast<RedisInternal*>(ctx->data);
    if (internal == nullptr) return;
    
    std::cout << "[REDIS] Disconnected from " << internal->host << ":" << internal->port 
              << " (status: " << status << ")" << std::endl;
    internal->connected = false;
    
    // Clear pending commands on disconnect
    internal->pendingCommands.clear();
}

static void OnCommandComplete(redisAsyncContext* ctx, void* reply, void* privdata) {
    auto* internal = static_cast<RedisInternal*>(ctx->data);
    if (internal == nullptr) return;
    
    uint64_t cmdId = reinterpret_cast<uint64_t>(privdata);
    
    auto it = internal->pendingCommands.find(cmdId);
    if (it == internal->pendingCommands.end()) {
        // Unknown command or already timed out - free reply if present
        if (reply) {
            freeReplyObject(reply);
        }
        return;
    }
    
    PendingCommand cmd = std::move(it->second);
    internal->pendingCommands.erase(it);
    
    // Calculate latency
    auto now = std::chrono::steady_clock::now();
    float latencyMs = std::chrono::duration<float, std::milli>(now - cmd.submitTime).count();
    
    // Store latency sample
    internal->latencySamples.push(latencyMs);
    if (internal->latencySamples.size() > RedisInternal::MAX_LATENCY_SAMPLES) {
        internal->latencySamples.pop();
    }
    
    // Invoke callback with reply
    if (cmd.callback) {
        cmd.callback(static_cast<redisReply*>(reply));
    }
    
    // Free the reply object
    if (reply) {
        freeReplyObject(reply);
    }
}
#endif

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
    // Create async context
    redisAsyncContext* ctx = redisAsyncConnect(host.c_str(), port);
    if (ctx == nullptr) {
        std::cerr << "[REDIS] Failed to create async context" << std::endl;
        return false;
    }
    
    if (ctx->err) {
        std::cerr << "[REDIS] Connection error: " << ctx->errstr << std::endl;
        redisAsyncFree(ctx);
        return false;
    }
    
    internal_->context = ctx;
    ctx->data = internal_.get();
    
    // Set up callbacks
    redisAsyncSetConnectCallback(ctx, OnConnect);
    redisAsyncSetDisconnectCallback(ctx, OnDisconnect);
    
    std::cout << "[REDIS] Async connection initiated" << std::endl;
    return true;
    #else
    std::cout << "[REDIS] hiredis not available - using stub implementation" << std::endl;
    internal_->connected = true;
    connected_ = true;
    return true;
    #endif
}

void RedisManager::shutdown() {
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) return;
    #else
    if (!connected_) return;
    #endif
    
    std::cout << "[REDIS] Shutting down..." << std::endl;
    
    // Cancel all pending commands with failure
    for (auto& [id, cmd] : internal_->pendingCommands) {
        #ifdef REDIS_AVAILABLE
        if (cmd.callback) {
            cmd.callback(nullptr);  // nullptr reply indicates failure
        }
        #else
        if (cmd.callback) {
            cmd.callback("");
        }
        #endif
        commandsFailed_++;
    }
    internal_->pendingCommands.clear();
    
    #ifdef REDIS_AVAILABLE
    // Disconnect and free context
    if (internal_->connected) {
        redisAsyncDisconnect(internal_->context);
    }
    redisAsyncFree(internal_->context);
    internal_->context = nullptr;
    #endif
    
    internal_->connected = false;
    connected_ = false;
    
    std::cout << "[REDIS] Shutdown complete" << std::endl;
}

bool RedisManager::isConnected() const {
    return internal_->connected;
}

void RedisManager::update() {
    if (!internal_->connected) return;
    
    #ifdef REDIS_AVAILABLE
    // Process any pending replies from hiredis
    // In a full implementation with event loop, this happens automatically
    // Here we use the blocking read with zero timeout to poll
    
    redisReader* reader = internal_->context->c.reader;
    if (reader && redisReaderGetReply(reader, nullptr) == REDIS_OK) {
        // Replies are processed via callbacks registered with each command
        // The callbacks are invoked by hiredis internally
    }
    #endif
    
    // Check for command timeouts
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(Constants::REDIS_COMMAND_TIMEOUT_MS);
    
    std::vector<uint64_t> timedOutIds;
    for (auto& [id, cmd] : internal_->pendingCommands) {
        if (now - cmd.submitTime > timeout) {
            timedOutIds.push_back(id);
        }
    }
    
    for (uint64_t id : timedOutIds) {
        auto it = internal_->pendingCommands.find(id);
        if (it != internal_->pendingCommands.end()) {
            #ifdef REDIS_AVAILABLE
            if (it->second.callback) {
                it->second.callback(nullptr);  // Timeout = failure
            }
            #else
            if (it->second.callback) {
                it->second.callback("");
            }
            #endif
            internal_->pendingCommands.erase(it);
            commandsFailed_++;
        }
    }
    
    // Update connected status
    connected_ = internal_->connected;
}

void RedisManager::set(std::string_view key, std::string_view value,
                      uint32_t ttlSeconds, SetCallback callback) {
    (void)ttlSeconds;
    
    if (!internal_->connected) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    uint64_t cmdId = internal_->nextCommandId++;
    
    PendingCommand cmd;
    cmd.id = cmdId;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [this, callback](redisReply* reply) {
        bool success = (reply != nullptr && 
                       reply->type == REDIS_REPLY_STATUS && 
                       std::string(reply->str) == "OK");
        
        if (success) {
            commandsCompleted_++;
        } else {
            commandsFailed_++;
        }
        
        if (callback) callback(success);
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    
    // Send command
    int status;
    if (ttlSeconds > 0) {
        status = redisAsyncCommand(internal_->context, OnCommandComplete, 
                                  reinterpret_cast<void*>(cmdId),
                                  "SET %b %b EX %u", 
                                  key.data(), key.size(),
                                  value.data(), value.size(),
                                  ttlSeconds);
    } else {
        status = redisAsyncCommand(internal_->context, OnCommandComplete,
                                  reinterpret_cast<void*>(cmdId),
                                  "SET %b %b",
                                  key.data(), key.size(),
                                  value.data(), value.size());
    }
    
    if (status == REDIS_OK) {
        commandsSent_++;
    } else {
        internal_->pendingCommands.erase(cmdId);
        commandsFailed_++;
        if (callback) callback(false);
    }
    #else
    // Stub implementation
    PendingCommand cmd;
    cmd.id = internal_->nextCommandId++;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [callback](const std::string&) {
        if (callback) callback(true);
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    commandsSent_++;
    #endif
}

void RedisManager::get(std::string_view key, GetCallback callback) {
    (void)key;
    
    if (!internal_->connected) {
        commandsFailed_++;
        if (callback) {
            AsyncResult<std::string> result;
            result.success = false;
            result.error = "Not connected";
            callback(result);
        }
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) {
        commandsFailed_++;
        if (callback) {
            AsyncResult<std::string> result;
            result.success = false;
            result.error = "Not connected";
            callback(result);
        }
        return;
    }
    
    uint64_t cmdId = internal_->nextCommandId++;
    
    PendingCommand cmd;
    cmd.id = cmdId;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [this, callback](redisReply* reply) {
        AsyncResult<std::string> result;
        
        if (reply == nullptr) {
            result.success = false;
            result.error = "Command failed or timed out";
            commandsFailed_++;
        } else if (reply->type == REDIS_REPLY_NIL) {
            result.success = false;
            result.error = "Key not found";
            commandsFailed_++;
        } else if (reply->type == REDIS_REPLY_STRING || reply->type == REDIS_REPLY_BINARY) {
            result.success = true;
            result.value = std::string(reply->str, reply->len);
            commandsCompleted_++;
        } else {
            result.success = false;
            result.error = "Unexpected reply type";
            commandsFailed_++;
        }
        
        if (callback) callback(result);
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    
    int status = redisAsyncCommand(internal_->context, OnCommandComplete,
                                  reinterpret_cast<void*>(cmdId),
                                  "GET %b",
                                  key.data(), key.size());
    
    if (status == REDIS_OK) {
        commandsSent_++;
    } else {
        internal_->pendingCommands.erase(cmdId);
        commandsFailed_++;
        if (callback) {
            AsyncResult<std::string> result;
            result.success = false;
            result.error = "Failed to send command";
            callback(result);
        }
    }
    #else
    // Stub implementation
    commandsSent_++;
    
    if (callback) {
        AsyncResult<std::string> result;
        result.success = true;
        result.value = "stub_value";
        callback(result);
        commandsCompleted_++;
    }
    #endif
}

void RedisManager::del(std::string_view key, SetCallback callback) {
    (void)key;
    
    if (!internal_->connected) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    uint64_t cmdId = internal_->nextCommandId++;
    
    PendingCommand cmd;
    cmd.id = cmdId;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [this, callback](redisReply* reply) {
        bool success = (reply != nullptr && reply->type == REDIS_REPLY_INTEGER);
        
        if (success) {
            commandsCompleted_++;
        } else {
            commandsFailed_++;
        }
        
        if (callback) callback(success);
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    
    int status = redisAsyncCommand(internal_->context, OnCommandComplete,
                                  reinterpret_cast<void*>(cmdId),
                                  "DEL %b",
                                  key.data(), key.size());
    
    if (status == REDIS_OK) {
        commandsSent_++;
    } else {
        internal_->pendingCommands.erase(cmdId);
        commandsFailed_++;
        if (callback) callback(false);
    }
    #else
    // Stub implementation
    commandsSent_++;
    if (callback) callback(true);
    commandsCompleted_++;
    #endif
}

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
            session.playerId = playerId;  // Set the playerId from the key
            
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

void RedisManager::addPlayerToZone(uint32_t zoneId, uint64_t playerId, SetCallback callback) {
    if (!internal_->connected) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    std::string key = RedisKeys::zonePlayers(zoneId);
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    uint64_t cmdId = internal_->nextCommandId++;
    
    PendingCommand cmd;
    cmd.id = cmdId;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [this, callback](redisReply* reply) {
        // SADD returns the number of elements added (1 if new, 0 if already present)
        bool success = (reply != nullptr && reply->type == REDIS_REPLY_INTEGER);
        
        if (success) {
            commandsCompleted_++;
        } else {
            commandsFailed_++;
        }
        
        if (callback) callback(success);
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    
    int status = redisAsyncCommand(internal_->context, OnCommandComplete,
                                  reinterpret_cast<void*>(cmdId),
                                  "SADD %s %llu",
                                  key.c_str(),
                                  static_cast<unsigned long long>(playerId));
    
    if (status == REDIS_OK) {
        commandsSent_++;
    } else {
        internal_->pendingCommands.erase(cmdId);
        commandsFailed_++;
        if (callback) callback(false);
    }
    #else
    // Stub implementation
    (void)zoneId;
    (void)playerId;
    if (callback) callback(true);
    #endif
}

void RedisManager::removePlayerFromZone(uint32_t zoneId, uint64_t playerId, SetCallback callback) {
    if (!internal_->connected) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    std::string key = RedisKeys::zonePlayers(zoneId);
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) {
        commandsFailed_++;
        if (callback) callback(false);
        return;
    }
    
    uint64_t cmdId = internal_->nextCommandId++;
    
    PendingCommand cmd;
    cmd.id = cmdId;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [this, callback](redisReply* reply) {
        // SREM returns the number of elements removed
        bool success = (reply != nullptr && reply->type == REDIS_REPLY_INTEGER);
        
        if (success) {
            commandsCompleted_++;
        } else {
            commandsFailed_++;
        }
        
        if (callback) callback(success);
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    
    int status = redisAsyncCommand(internal_->context, OnCommandComplete,
                                  reinterpret_cast<void*>(cmdId),
                                  "SREM %s %llu",
                                  key.c_str(),
                                  static_cast<unsigned long long>(playerId));
    
    if (status == REDIS_OK) {
        commandsSent_++;
    } else {
        internal_->pendingCommands.erase(cmdId);
        commandsFailed_++;
        if (callback) callback(false);
    }
    #else
    // Stub implementation
    (void)zoneId;
    (void)playerId;
    if (callback) callback(true);
    #endif
}

void RedisManager::getZonePlayers(uint32_t zoneId, 
                                 std::function<void(const AsyncResult<std::vector<uint64_t>>&)> callback) {
    if (!internal_->connected) {
        commandsFailed_++;
        if (callback) {
            AsyncResult<std::vector<uint64_t>> result;
            result.success = false;
            result.error = "Not connected";
            callback(result);
        }
        return;
    }
    
    std::string key = RedisKeys::zonePlayers(zoneId);
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) {
        commandsFailed_++;
        if (callback) {
            AsyncResult<std::vector<uint64_t>> result;
            result.success = false;
            result.error = "Not connected";
            callback(result);
        }
        return;
    }
    
    uint64_t cmdId = internal_->nextCommandId++;
    
    PendingCommand cmd;
    cmd.id = cmdId;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [this, callback](redisReply* reply) {
        AsyncResult<std::vector<uint64_t>> result;
        
        if (reply == nullptr) {
            result.success = false;
            result.error = "Command failed or timed out";
            commandsFailed_++;
        } else if (reply->type == REDIS_REPLY_NIL) {
            // Empty set
            result.success = true;
            commandsCompleted_++;
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
            commandsCompleted_++;
        } else {
            result.success = false;
            result.error = "Unexpected reply type";
            commandsFailed_++;
        }
        
        if (callback) callback(result);
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    
    int status = redisAsyncCommand(internal_->context, OnCommandComplete,
                                  reinterpret_cast<void*>(cmdId),
                                  "SMEMBERS %s",
                                  key.c_str());
    
    if (status == REDIS_OK) {
        commandsSent_++;
    } else {
        internal_->pendingCommands.erase(cmdId);
        commandsFailed_++;
        if (callback) {
            AsyncResult<std::vector<uint64_t>> result;
            result.success = false;
            result.error = "Failed to send command";
            callback(result);
        }
    }
    #else
    // Stub implementation
    (void)zoneId;
    if (callback) {
        AsyncResult<std::vector<uint64_t>> result;
        result.success = true;
        callback(result);
    }
    #endif
}

void RedisManager::publish(std::string_view channel, std::string_view message) {
    if (!internal_->connected) return;
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) return;
    
    // Fire-and-forget publish - no callback needed
    redisAsyncCommand(internal_->context, nullptr, nullptr,
                     "PUBLISH %b %b",
                     channel.data(), channel.size(),
                     message.data(), message.size());
    
    commandsSent_++;
    // Note: Not tracking completion for fire-and-forget
    #else
    (void)channel;
    (void)message;
    #endif
}

void RedisManager::subscribe(std::string_view channel,
                            std::function<void(std::string_view, std::string_view)> callback) {
    if (!internal_->connected) return;
    
    #ifdef REDIS_AVAILABLE
    if (!internal_->context) return;
    
    internal_->subscribeCallback = std::move(callback);
    
    // Store callback ID for the subscribe command
    uint64_t cmdId = internal_->nextCommandId++;
    
    PendingCommand cmd;
    cmd.id = cmdId;
    cmd.submitTime = std::chrono::steady_clock::now();
    cmd.callback = [this](redisReply* reply) {
        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
            // Subscription message format: [message, channel, data]
            if (internal_->subscribeCallback) {
                std::string_view msgType(reply->element[0]->str, reply->element[0]->len);
                if (msgType == "message") {
                    std::string_view channel(reply->element[1]->str, reply->element[1]->len);
                    std::string_view message(reply->element[2]->str, reply->element[2]->len);
                    internal_->subscribeCallback(channel, message);
                }
            }
        }
    };
    
    internal_->pendingCommands[cmdId] = std::move(cmd);
    
    int status = redisAsyncCommand(internal_->context, OnCommandComplete,
                                  reinterpret_cast<void*>(cmdId),
                                  "SUBSCRIBE %b",
                                  channel.data(), channel.size());
    
    if (status == REDIS_OK) {
        commandsSent_++;
    } else {
        internal_->pendingCommands.erase(cmdId);
    }
    #else
    (void)channel;
    (void)callback;
    #endif
}

uint64_t RedisManager::getCommandsSent() const {
    return commandsSent_;
}

uint64_t RedisManager::getCommandsCompleted() const {
    return commandsCompleted_;
}

uint64_t RedisManager::getCommandsFailed() const {
    return commandsFailed_;
}

float RedisManager::getAverageLatencyMs() const {
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

void RedisManager::unsubscribe(std::string_view channel) {
    if (!internal_->connected) return;
    
    auto& subs = internal_->subscriptions;
    subs.erase(std::remove_if(subs.begin(), subs.end(),
        [channel](const auto& sub) { return sub.channel == channel; }), subs.end());
    
    std::cout << "[REDIS] Unsubscribed from " << channel << std::endl;
}

void RedisManager::processSubscriptions() {
    if (!internal_->connected) return;
    
    // Process queued messages
    std::vector<std::pair<std::string, std::string>> messages;
    {
        std::lock_guard<std::mutex> lock(internal_->messageMutex);
        messages.swap(internal_->messageQueue);
    }
    
    for (const auto& [channel, message] : messages) {
        for (const auto& sub : internal_->subscriptions) {
            // Simple channel matching (could use pattern matching)
            if (sub.channel == channel) {
                sub.callback(channel, message);
            }
        }
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
