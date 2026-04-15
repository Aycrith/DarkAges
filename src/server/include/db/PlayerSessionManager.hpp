#pragma once

#include "ecs/CoreTypes.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace DarkAges {

// Forward declarations
class RedisManager;
struct RedisInternal;

// Player session data stored in Redis
struct PlayerSession {
    uint64_t playerId{0};
    uint32_t zoneId{0};
    uint32_t connectionId{0};
    Position position;
    int32_t health{0};
    uint64_t lastActivity{0};
    char username[32]{0};
};

// Async operation result
template<typename T>
struct AsyncResult {
    bool success{false};
    T value{};
    std::string error;
};

// Manages player session CRUD operations in Redis
// Extracted from RedisManager for cohesion - session save/load/position updates
class PlayerSessionManager {
public:
    using SetCallback = std::function<void(bool success)>;
    using SessionCallback = std::function<void(const AsyncResult<PlayerSession>& result)>;

    explicit PlayerSessionManager(RedisManager& redis);

    // Save player session to Redis
    void savePlayerSession(const PlayerSession& session, SetCallback callback = nullptr);
    
    // Load player session from Redis
    void loadPlayerSession(uint64_t playerId, SessionCallback callback);
    
    // Remove player session
    void removePlayerSession(uint64_t playerId, SetCallback callback = nullptr);
    
    // Update player position (lightweight, called frequently)
    void updatePlayerPosition(uint64_t playerId, const Position& pos,
                             uint32_t timestamp, SetCallback callback = nullptr);

private:
    RedisManager& redis_;
};

} // namespace DarkAges
