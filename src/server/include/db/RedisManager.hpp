#pragma once

#include "ecs/CoreTypes.hpp"
#include "db/PlayerSessionManager.hpp"  // Provides PlayerSession, AsyncResult
#include "db/PubSubManager.hpp"          // Provides ZoneMessageType, ZoneMessage
#include "db/ZoneManager.hpp"            // Provides ZoneManager
#include "db/StreamManager.hpp"          // Provides StreamEntry
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <memory>

// [DATABASE_AGENT] Redis hot-state cache wrapper
// Async operations only - never blocks game thread

namespace DarkAges {

// Forward declarations
struct RedisInternal;
class PlayerSessionManager;
class ZoneManager;
class StreamManager;
#ifdef REDIS_AVAILABLE
class PubSubManager;
#endif

class RedisManager {
public:
    using GetCallback = std::function<void(const AsyncResult<std::string>& result)>;
    using SetCallback = std::function<void(bool success)>;
    using SessionCallback = std::function<void(const AsyncResult<PlayerSession>& result)>;

    RedisManager();
    ~RedisManager();  // Defined in .cpp (needs full RedisInternal type)
    
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
    
    // === Streams (Non-blocking alternative to Pub/Sub) ===
    
    using StreamAddCallback = StreamManager::StreamAddCallback;
    using StreamReadCallback = StreamManager::StreamReadCallback;
    using StreamEntry = DarkAges::StreamEntry;
    
    // Add entry to stream (XADD)
    void xadd(std::string_view streamKey, 
              std::string_view id,
              const std::unordered_map<std::string, std::string>& fields,
              StreamAddCallback callback = nullptr);
    
    // Read entries from stream (XREAD)
    void xread(std::string_view streamKey,
               std::string_view lastId,
               StreamReadCallback callback,
               uint32_t count = 100,
               uint32_t blockMs = 0);
    
    // === Pipeline Batching ===
    
    void pipelineSet(const std::vector<std::pair<std::string, std::string>>& kvs,
                     uint32_t ttlSeconds = Constants::REDIS_KEY_TTL_SECONDS,
                     SetCallback callback = nullptr);
    
    // === Metrics ===
    
    [[nodiscard]] uint64_t getCommandsSent() const;
    [[nodiscard]] uint64_t getCommandsCompleted() const;
    [[nodiscard]] uint64_t getCommandsFailed() const;
    [[nodiscard]] float getAverageLatencyMs() const;

    // Access internal state for sub-managers
    RedisInternal& getInternal() { return *internal_; }

private:
    std::unique_ptr<RedisInternal> internal_;
    std::unique_ptr<PlayerSessionManager> sessionManager_;
    std::unique_ptr<ZoneManager> zoneManager_;
    std::unique_ptr<StreamManager> streamManager_;
    #ifdef REDIS_AVAILABLE
    std::unique_ptr<PubSubManager> pubSubManager_;
    #endif
    bool connected_{false};
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
