// Player session management implementation
// Handles CRUD operations for player sessions stored in Redis

#include "db/PlayerSessionManager.hpp"
#include "db/RedisManager.hpp"
#include "Constants.hpp"
#include <cstring>

namespace DarkAges {

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
// RedisKeys - same as in RedisManager.hpp
// ============================================================================

namespace SessionRedisKeys {
    inline std::string playerSession(uint64_t playerId) {
        return "player:" + std::to_string(playerId) + ":session";
    }
    
    inline std::string playerPosition(uint64_t playerId) {
        return "player:" + std::to_string(playerId) + ":pos";
    }
}

// ============================================================================
// PlayerSessionManager Implementation
// ============================================================================

PlayerSessionManager::PlayerSessionManager(RedisManager& redis)
    : redis_(redis) {}

void PlayerSessionManager::savePlayerSession(const PlayerSession& session, SetCallback callback) {
    std::string key = SessionRedisKeys::playerSession(session.playerId);
    auto data = SerializePlayerSession(session);
    
    redis_.set(key, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
               Constants::REDIS_KEY_TTL_SECONDS, callback);
}

void PlayerSessionManager::loadPlayerSession(uint64_t playerId, SessionCallback callback) {
    std::string key = SessionRedisKeys::playerSession(playerId);
    
    redis_.get(key, [playerId, callback](const AsyncResult<std::string>& result) {
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

void PlayerSessionManager::removePlayerSession(uint64_t playerId, SetCallback callback) {
    redis_.del(SessionRedisKeys::playerSession(playerId), callback);
}

void PlayerSessionManager::updatePlayerPosition(uint64_t playerId, const Position& pos,
                                                 uint32_t timestamp, SetCallback callback) {
    (void)timestamp;  // Timestamp is included in serialized position
    
    std::string key = SessionRedisKeys::playerPosition(playerId);
    auto data = SerializePosition(pos);
    
    redis_.set(key, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
               Constants::REDIS_KEY_TTL_SECONDS, callback);
}

} // namespace DarkAges
