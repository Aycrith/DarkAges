#pragma once

#include "ecs/CoreTypes.hpp"
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <memory>

// [DATABASE_AGENT] Redis hot-state cache wrapper
// Async operations only - never blocks game thread

namespace DarkAges {

// Forward declaration
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

// ============================================================================
// Pub/Sub Message Types for Cross-Zone Communication
// ============================================================================

// Message types for cross-zone communication
enum class ZoneMessageType : uint8_t {
    ENTITY_SYNC = 1,        // Entity state update for aura
    MIGRATION_REQUEST = 2,  // Initiate entity migration
    MIGRATION_STATE = 3,    // Migration state update
    MIGRATION_COMPLETE = 4, // Migration finished
    BROADCAST = 5,          // Zone-wide broadcast
    CHAT = 6,               // Cross-zone chat
    ZONE_STATUS = 7         // Zone status update
};

// Cross-zone message header
struct ZoneMessage {
    ZoneMessageType type;
    uint32_t sourceZoneId;
    uint32_t targetZoneId;  // 0 = broadcast to all
    uint32_t timestamp;
    uint32_t sequence;
    std::vector<uint8_t> payload;
    
    std::vector<uint8_t> serialize() const;
    static std::optional<ZoneMessage> deserialize(const std::vector<uint8_t>& data);
};

// [DATABASE_AGENT] Redis connection manager
class RedisManager {
public:
    using ConnectCallback = std::function<void(bool success, const std::string& error)>;
    using GetCallback = std::function<void(const AsyncResult<std::string>& result)>;
    using SetCallback = std::function<void(bool success)>;
    using SessionCallback = std::function<void(const AsyncResult<PlayerSession>& result)>;

public:
    RedisManager();
    ~RedisManager();
    
    // Initialize connection (non-blocking)
    bool initialize(const std::string& host = "localhost", 
                   uint16_t port = Constants::REDIS_DEFAULT_PORT);
    
    // Shutdown
    void shutdown();
    
    // Check if connected
    [[nodiscard]] bool isConnected() const;
    
    // Process async callbacks - call every tick
    void update();
    
    // === Key-Value Operations (Async) ===
    
    // Set key with TTL (seconds, 0 = no expiration)
    void set(std::string_view key, std::string_view value, 
            uint32_t ttlSeconds = Constants::REDIS_KEY_TTL_SECONDS,
            SetCallback callback = nullptr);
    
    // Get key value
    void get(std::string_view key, GetCallback callback);
    
    // Delete key
    void del(std::string_view key, SetCallback callback = nullptr);
    
    // === Player Session Operations ===
    
    // Save player session to Redis
    void savePlayerSession(const PlayerSession& session, SetCallback callback = nullptr);
    
    // Load player session from Redis
    void loadPlayerSession(uint64_t playerId, SessionCallback callback);
    
    // Remove player session
    void removePlayerSession(uint64_t playerId, SetCallback callback = nullptr);
    
    // Update player position (lightweight, called frequently)
    void updatePlayerPosition(uint64_t playerId, const Position& pos, 
                             uint32_t timestamp, SetCallback callback = nullptr);
    
    // === Zone Operations ===
    
    // Add player to zone set
    void addPlayerToZone(uint32_t zoneId, uint64_t playerId, SetCallback callback = nullptr);
    
    // Remove player from zone
    void removePlayerFromZone(uint32_t zoneId, uint64_t playerId, SetCallback callback = nullptr);
    
    // Get all players in zone
    void getZonePlayers(uint32_t zoneId, 
                       std::function<void(const AsyncResult<std::vector<uint64_t>>&)> callback);
    
    // === Pub/Sub (for cross-zone communication) ===
    
    // Publish message to channel
    void publish(std::string_view channel, std::string_view message);
    
    // Subscribe to channel with callback
    void subscribe(std::string_view channel, 
                   std::function<void(std::string_view channel, std::string_view message)> callback);
    
    // Unsubscribe from channel
    void unsubscribe(std::string_view channel);
    
    // Process subscribed messages - call every tick
    void processSubscriptions();
    
    // Zone-specific helpers
    void publishToZone(uint32_t zoneId, const ZoneMessage& message);
    void broadcastToAllZones(const ZoneMessage& message);
    void subscribeToZoneChannel(uint32_t myZoneId, 
                                std::function<void(const ZoneMessage&)> callback);
    
    // === Pipeline Batching ===
    
    // Batch SET operations for high throughput (10k+ ops/sec)
    void pipelineSet(const std::vector<std::pair<std::string, std::string>>& kvs,
                     uint32_t ttlSeconds = Constants::REDIS_KEY_TTL_SECONDS,
                     SetCallback callback = nullptr);
    
    // === Metrics ===
    
    [[nodiscard]] uint64_t getCommandsSent() const;
    [[nodiscard]] uint64_t getCommandsCompleted() const;
    [[nodiscard]] uint64_t getCommandsFailed() const;
    [[nodiscard]] float getAverageLatencyMs() const;

private:
    std::unique_ptr<RedisInternal> internal_;
    bool connected_{false};
    
    // Metrics
    uint64_t commandsSent_{0};
    uint64_t commandsCompleted_{0};
    uint64_t commandsFailed_{0};
    float avgLatencyMs_{0.0f};
};

// [DATABASE_AGENT] Key naming conventions
namespace RedisKeys {
    inline std::string playerSession(uint64_t playerId) {
        return "player:" + std::to_string(playerId) + ":session";
    }
    
    inline std::string playerPosition(uint64_t playerId) {
        return "player:" + std::to_string(playerId) + ":pos";
    }
    
    inline std::string zonePlayers(uint32_t zoneId) {
        return "zone:" + std::to_string(zoneId) + ":players";
    }
    
    inline std::string zoneEntities(uint32_t zoneId) {
        return "zone:" + std::to_string(zoneId) + ":entities";
    }
    
    inline std::string entityState(uint32_t entityId) {
        return "entity:" + std::to_string(entityId) + ":state";
    }
}

} // namespace DarkAges
