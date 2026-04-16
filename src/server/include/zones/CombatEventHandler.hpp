#pragma once

#include "ecs/CoreTypes.hpp"
#include "netcode/NetworkManager.hpp"
#include "combat/CombatSystem.hpp"
#include "combat/LagCompensatedCombat.hpp"
#include "security/AntiCheat.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace DarkAges {

class ZoneServer;
class NetworkManager;
class ScyllaManager;

// Pending respawn entry
struct PendingRespawn {
    EntityID entity;
    uint32_t respawnTimeMs;
};

// Handles combat event processing, entity death, respawns, and combat logging.
// Extracted from ZoneServer to reduce monolithic file size.
class CombatEventHandler {
public:
    explicit CombatEventHandler(ZoneServer& server);
    ~CombatEventHandler() = default;

    // Set subsystem references (called during ZoneServer::initialize)
    void setNetwork(NetworkManager* network) { network_ = network; }
    void setScylla(ScyllaManager* scylla) { scylla_ = scylla; }
    void setCombatSystem(CombatSystem* combat) { combatSystem_ = combat; }
    void setLagCompensator(LagCompensator* lag) { lagCompensator_ = lag; }
    void setAntiCheat(Security::AntiCheatSystem* antiCheat) { antiCheat_ = antiCheat; }

    // Set entity-connection mapping references (owned by ZoneServer)
    void setConnectionMappings(
        std::unordered_map<ConnectionID, EntityID>* connToEntity,
        std::unordered_map<EntityID, ConnectionID>* entityToConn);

    // Process combat (called each tick from updateGameLogic)
    void processCombat();

    // Handle entity death - logs events, updates stats, schedules respawn
    void onEntityDied(EntityID victim, EntityID killer);

    // Process pending respawns
    void processRespawns();

    // Send combat damage event to relevant clients
    void sendCombatEvent(EntityID attacker, EntityID target, int16_t damage, const Position& location);

    // Log combat event to ScyllaDB
    void logCombatEvent(const HitResult& hit, EntityID attacker, EntityID target);

    // Process attack input with lag compensation and anti-cheat validation
    void processAttackInput(EntityID entity, const ClientInputPacket& input);

    // Get current time from ZoneServer
    uint32_t getCurrentTimeMs() const;

    // Access pending respawns (for testing)
    [[nodiscard]] const std::vector<PendingRespawn>& getPendingRespawns() const { return pendingRespawns_; }

private:
    ZoneServer& server_;
    NetworkManager* network_{nullptr};
    ScyllaManager* scylla_{nullptr};
    CombatSystem* combatSystem_{nullptr};
    LagCompensator* lagCompensator_{nullptr};
    Security::AntiCheatSystem* antiCheat_{nullptr};

    // Borrowed references to ZoneServer's connection mappings
    std::unordered_map<ConnectionID, EntityID>* connectionToEntity_{nullptr};
    std::unordered_map<EntityID, ConnectionID>* entityToConnection_{nullptr};

    // Owned state
    std::vector<PendingRespawn> pendingRespawns_;
    static constexpr uint32_t RESPAWN_DELAY_MS = 5000;
};

} // namespace DarkAges
