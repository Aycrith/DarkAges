#pragma once
#include "ecs/CoreTypes.hpp"
#include "db/RedisManager.hpp"
#include <vector>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace DarkAges {

// Forward declarations
class ZoneOrchestrator;

// Migration states
enum class MigrationState {
    NONE,           // No migration in progress
    PREPARING,      // Starting migration, collecting state
    TRANSFERRING,   // Sending state to target zone
    SYNCING,        // Both zones have entity, syncing state
    COMPLETING,     // Final handoff to target zone
    COMPLETED,      // Migration done, entity removed from source
    FAILED          // Migration failed, rollback
};

// Entity snapshot for migration
struct EntitySnapshot {
    // Core identity
    EntityID entity;
    uint64_t playerId;           // Persistent player ID (0 for NPCs)
    
    // Transform
    Position position;
    Velocity velocity;
    Rotation rotation;
    
    // Combat
    CombatState combat;
    
    // Network
    NetworkState network;
    
    // Input
    InputState lastInput;
    
    // Anti-cheat state (needed for seamless transition)
    AntiCheatState antiCheat;
    
    // Metadata
    uint32_t sourceZoneId;
    uint32_t targetZoneId;
    uint32_t timestamp;
    uint32_t sequence;           // Migration sequence number
    ConnectionID connectionId;   // Network connection for players
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static std::optional<EntitySnapshot> deserialize(const std::vector<uint8_t>& data);
};

// Active migration tracking
struct ActiveMigration {
    EntityID entity;
    uint64_t playerId;
    uint32_t sourceZoneId;
    uint32_t targetZoneId;
    MigrationState state;
    uint32_t startTime;
    uint32_t timeoutMs;
    uint32_t sequence;
    EntitySnapshot snapshot;     // Captured state for transfer
    
    // Callbacks for completion/failure
    std::function<void()> onSuccess;
    std::function<void(const std::string&)> onFailure;
};

// Migration statistics
struct MigrationStats {
    uint32_t totalMigrations{0};
    uint32_t successfulMigrations{0};
    uint32_t failedMigrations{0};
    uint32_t cancelledMigrations{0};
    uint32_t timeoutCount{0};
    uint32_t avgMigrationTimeMs{0};
    uint32_t maxMigrationTimeMs{0};
};

// [ZONE_AGENT] Entity migration coordinator
// Handles seamless transfer of entities between zones with state preservation
class EntityMigrationManager {
public:
    using MigrationCallback = std::function<void(EntityID entity, bool success)>;
    using ConnectionRedirectCallback = std::function<void(ConnectionID connId, uint32_t newZoneId, uint16_t newPort)>;

public:
    EntityMigrationManager(uint32_t myZoneId, RedisManager* redis);
    
    // Start migration of entity to target zone
    // Returns true if migration started successfully
    bool initiateMigration(EntityID entity, uint32_t targetZoneId,
                          Registry& registry,
                          MigrationCallback callback = nullptr);
    
    // Handle incoming migration request (target zone side)
    void onMigrationRequestReceived(const EntitySnapshot& snapshot);
    
    // Handle migration state update from other zone (via Redis pub/sub)
    void onMigrationStateUpdate(EntityID entity, uint32_t fromZoneId,
                               MigrationState newState,
                               const std::vector<uint8_t>& data);
    
    // Update active migrations (call every tick)
    void update(Registry& registry, uint32_t currentTimeMs);
    
    // Check if entity is currently migrating
    bool isMigrating(EntityID entity) const;
    
    // Get migration state for entity
    MigrationState getMigrationState(EntityID entity) const;
    
    // Cancel ongoing migration (e.g., player disconnected)
    bool cancelMigration(EntityID entity);
    
    // Get count of active migrations
    size_t getActiveMigrationCount() const { return activeMigrations_.size(); }
    
    // Set timeout for migrations
    void setMigrationTimeout(uint32_t timeoutMs) { defaultTimeoutMs_ = timeoutMs; }
    
    // Get migration statistics
    const MigrationStats& getStats() const { return stats_; }
    
    // Set callback for connection redirect (notify client of new zone)
    void setOnConnectionRedirect(ConnectionRedirectCallback callback) { 
        onConnectionRedirect_ = callback; 
    }
    
    // Set zone port lookup callback (get port for zone ID)
    void setZonePortLookup(std::function<uint16_t(uint32_t)> callback) {
        zonePortLookup_ = callback;
    }

private:
    uint32_t myZoneId_;
    RedisManager* redis_;
    uint32_t defaultTimeoutMs_{5000};  // 5 second default timeout
    uint32_t migrationSequence_{0};
    
    std::unordered_map<EntityID, ActiveMigration> activeMigrations_;
    MigrationStats stats_;
    
    // Callbacks
    ConnectionRedirectCallback onConnectionRedirect_;
    std::function<uint16_t(uint32_t)> zonePortLookup_;
    
    // Redis channel names
    std::string getMigrationChannel(uint32_t zoneId) const {
        return "zone:" + std::to_string(zoneId) + ":migration";
    }
    
    std::string getMigrationStateChannel(uint32_t zoneId) const {
        return "zone:" + std::to_string(zoneId) + ":migration:state";
    }
    
    // State machine transitions
    void transitionState(EntityID entity, MigrationState newState);
    void onPreparing(EntityID entity, Registry& registry, uint32_t currentTimeMs);
    void onTransferring(EntityID entity, Registry& registry, uint32_t currentTimeMs);
    void onSyncing(EntityID entity, Registry& registry, uint32_t currentTimeMs);
    void onCompleting(EntityID entity, Registry& registry, uint32_t currentTimeMs);
    void onCompleted(EntityID entity, Registry& registry);
    void onFailed(EntityID entity, const std::string& reason);
    
    // Serialization helpers
    EntitySnapshot captureEntityState(EntityID entity, Registry& registry,
                                     uint32_t targetZoneId) const;
    EntityID restoreEntityState(const EntitySnapshot& snapshot, Registry& registry);
    
    // Network communication via Redis
    void sendMigrationData(uint32_t targetZoneId, const EntitySnapshot& snapshot);
    void sendStateUpdate(uint32_t targetZoneId, EntityID entity, 
                        MigrationState state, const std::vector<uint8_t>& data);
    void broadcastMigrationComplete(uint32_t sourceZoneId, uint32_t targetZoneId, 
                                   EntityID entity, uint64_t playerId);
    
    // Redis subscription handling
    void setupRedisSubscriptions();
    void handleRedisMessage(std::string_view channel, std::string_view message);
    
    // Cleanup
    void cleanupMigration(EntityID entity);
    void updateStats(uint32_t migrationTimeMs, bool success);
};

} // namespace DarkAges
