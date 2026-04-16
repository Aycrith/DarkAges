#pragma once

#include "ecs/CoreTypes.hpp"
#include "zones/AuraProjection.hpp"
#include "zones/EntityMigration.hpp"
#include "zones/ZoneHandoff.hpp"
#include "zones/ZoneDefinition.hpp"
#include <memory>
#include <vector>
#include <cstdint>

namespace DarkAges {

class ZoneServer;
class NetworkManager;
class RedisManager;

// Handles aura projection synchronization, zone transitions, entity migration,
// and zone handoff logic. Extracted from ZoneServer to reduce monolithic file size.
class AuraZoneHandler {
public:
    explicit AuraZoneHandler(ZoneServer& server);
    ~AuraZoneHandler() = default;

    // Set subsystem references (called during ZoneServer::initialize)
    void setNetwork(NetworkManager* network) { network_ = network; }
    void setRedis(RedisManager* redis) { redis_ = redis; }
    void setAuraManager(AuraProjectionManager* aura) { auraManager_ = aura; }
    void setMigrationManager(EntityMigrationManager* mgr) { migrationManager_ = mgr; }
    void setHandoffController(ZoneHandoffController* ctrl) { handoffController_ = ctrl; }

    // Set entity-connection mapping references (owned by ZoneServer)
    void setConnectionMappings(
        std::unordered_map<ConnectionID, EntityID>* connToEntity,
        std::unordered_map<EntityID, ConnectionID>* entityToConn);

    // Initialize aura projection manager with adjacent zones
    void initializeAuraManager();

    // Initialize handoff controller with zone definition and callbacks
    void initializeHandoffController();

    // Sync aura state to adjacent zones via Redis (called from updateReplication)
    void syncAuraState();

    // Check for entities crossing zone boundaries (called from updateGameLogic)
    void checkEntityZoneTransitions();

    // Handle ownership transfers within aura buffer
    void handleAuraEntityMigration();

    // Update zone handoffs for all players (called from updateGameLogic)
    void updateZoneHandoffs();

    // Callback: entity migration completed
    void onEntityMigrationComplete(EntityID entity, bool success);

    // Callback: handoff started
    void onHandoffStarted(uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success);

    // Callback: handoff completed
    void onHandoffCompleted(uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success);

    // Zone lookup: returns zone definition by ID
    ZoneDefinition* lookupZone(uint32_t zoneId);

    // Zone lookup: returns zone ID containing position
    uint32_t findZoneByPosition(float x, float z);

    // Get current time from ZoneServer
    uint32_t getCurrentTimeMs() const;

private:
    ZoneServer& server_;
    NetworkManager* network_{nullptr};
    RedisManager* redis_{nullptr};
    AuraProjectionManager* auraManager_{nullptr};
    EntityMigrationManager* migrationManager_{nullptr};
    ZoneHandoffController* handoffController_{nullptr};

    // Borrowed references to ZoneServer's connection mappings
    std::unordered_map<ConnectionID, EntityID>* connectionToEntity_{nullptr};
    std::unordered_map<EntityID, ConnectionID>* entityToConnection_{nullptr};

    // Owned state
    uint32_t lastAuraSyncTime_{0};
    static constexpr uint32_t AURA_SYNC_INTERVAL_MS = 50;
};

} // namespace DarkAges
