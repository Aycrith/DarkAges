// [ZONE_AGENT] Combat event handler implementation
// Extracted from ZoneServer for separation of concerns

#include "zones/CombatEventHandler.hpp"
#include "zones/ZoneServer.hpp"
#ifdef DARKAGES_HAS_PROTOBUF
#include "netcode/ProtobufProtocol.hpp"
#endif
#include "db/ScyllaManager.hpp"
#include <iostream>
#include <ctime>

namespace DarkAges {

CombatEventHandler::CombatEventHandler(ZoneServer& zone) : zone_(zone) {}

void CombatEventHandler::processCombat() {
    // Combat is triggered by client inputs (attack action)
    // The actual combat processing happens in validateAndApplyInput
    // when an attack input is received
}

void CombatEventHandler::onEntityDied(EntityID victim, EntityID killer) {
    auto& registry = zone_.getRegistry();
    auto& config = zone_.getConfig();
    auto& network = zone_.getNetwork();
    auto* scylla = zone_.getScylla();
    uint32_t currentTick = zone_.getCurrentTick();

    std::cout << "[ZONE " << config.zoneId << "] Entity " << static_cast<uint32_t>(victim)
              << " killed by " << static_cast<uint32_t>(killer) << std::endl;

    // Get victim and killer info
    uint64_t victimPlayerId = 0;
    uint64_t killerPlayerId = 0;
    std::string victimName = "NPC";
    std::string killerName = "NPC";
    Position killLocation{0, 0, 0, 0};

    if (const PlayerInfo* info = registry.try_get<PlayerInfo>(victim)) {
        victimPlayerId = info->playerId;
        victimName = info->username;
    }
    if (const PlayerInfo* info = registry.try_get<PlayerInfo>(killer)) {
        killerPlayerId = info->playerId;
        killerName = info->username;
    }
    if (const Position* pos = registry.try_get<Position>(victim)) {
        killLocation = *pos;
    }

    // Send death event to clients
    if (killer != entt::null) {
#ifdef DARKAGES_HAS_PROTOBUF
        auto deathEvent = Netcode::ProtobufProtocol::createPlayerDeathEvent(
            static_cast<uint32_t>(victim));
        auto data = Netcode::ProtobufProtocol::serializeEvent(deathEvent);
        network.broadcastEvent(std::span<const uint8_t>(data.data(), data.size()));
#endif
    }

    // [DATABASE_AGENT] Log kill event to ScyllaDB
    if (scylla && scylla->isConnected()) {
        // Log the kill for the killer
        if (killerPlayerId > 0) {
            CombatEvent killEvent;
            killEvent.eventId = 0;
            killEvent.timestamp = zone_.getCurrentTimeMs();
            killEvent.zoneId = config.zoneId;
            killEvent.attackerId = killerPlayerId;
            killEvent.targetId = victimPlayerId;
            killEvent.eventType = "kill";
            killEvent.damageAmount = 0;
            killEvent.isCritical = false;
            killEvent.weaponType = "melee";
            killEvent.position = killLocation;
            killEvent.serverTick = currentTick;

            scylla->logCombatEvent(killEvent, nullptr);
        }

        // Log the death for the victim
        if (victimPlayerId > 0) {
            CombatEvent deathEvent;
            deathEvent.eventId = 0;
            deathEvent.timestamp = zone_.getCurrentTimeMs();
            deathEvent.zoneId = config.zoneId;
            deathEvent.attackerId = killerPlayerId;
            deathEvent.targetId = victimPlayerId;
            deathEvent.eventType = "death";
            deathEvent.damageAmount = 0;
            deathEvent.isCritical = false;
            deathEvent.weaponType = "melee";
            deathEvent.position = killLocation;
            deathEvent.serverTick = currentTick;

            scylla->logCombatEvent(deathEvent, nullptr);
        }

        // Update player stats if both are players
        if (killerPlayerId > 0 && victimPlayerId > 0) {
            // Get current date in YYYYMMDD format
            time_t now = time(nullptr);
            struct tm* tm_info = localtime(&now);
            uint32_t sessionDate = (tm_info->tm_year + 1900) * 10000 +
                                   (tm_info->tm_mon + 1) * 100 +
                                   tm_info->tm_mday;

            // Update killer stats
            PlayerCombatStats killerStats;
            killerStats.playerId = killerPlayerId;
            killerStats.sessionDate = sessionDate;
            killerStats.kills = 1;
            killerStats.deaths = 0;
            killerStats.damageDealt = 0;
            killerStats.damageTaken = 0;
            killerStats.longestKillStreak = 0;
            killerStats.currentKillStreak = 0;

            scylla->updatePlayerStats(killerStats, [killerName](bool success) {
                if (!success) {
                    std::cerr << "[SCYLLA] Failed to update stats for killer: " << killerName << std::endl;
                }
            });

            // Update victim stats
            PlayerCombatStats victimStats;
            victimStats.playerId = victimPlayerId;
            victimStats.sessionDate = sessionDate;
            victimStats.kills = 0;
            victimStats.deaths = 1;
            victimStats.damageDealt = 0;
            victimStats.damageTaken = 0;
            victimStats.longestKillStreak = 0;
            victimStats.currentKillStreak = 0;

            scylla->updatePlayerStats(victimStats, [victimName](bool success) {
                if (!success) {
                    std::cerr << "[SCYLLA] Failed to update stats for victim: " << victimName << std::endl;
                }
            });
        }
    }

    // Schedule respawn
    Position spawnPos = killLocation;
    zone_.addPendingRespawn(victim, zone_.getCurrentTimeMs() + 5000, spawnPos);
}

void CombatEventHandler::sendCombatEvent(EntityID attacker, EntityID target, int16_t damage, const Position& location) {
    auto& network = zone_.getNetwork();
    auto& entityToConnection = zone_.getEntityToConnection();

    // Send damage event to relevant clients
#ifdef DARKAGES_HAS_PROTOBUF
    auto damageEvent = Netcode::ProtobufProtocol::createDamageEvent(
        static_cast<uint32_t>(attacker), static_cast<uint32_t>(target), damage);
    auto data = Netcode::ProtobufProtocol::serializeEvent(damageEvent);

    // Send to target
    auto targetConnIt = entityToConnection.find(target);
    if (targetConnIt != entityToConnection.end()) {
        network.sendEvent(targetConnIt->second, std::span<const uint8_t>(data.data(), data.size()));
    }
    // Send to attacker
    auto attackerConnIt = entityToConnection.find(attacker);
    if (attackerConnIt != entityToConnection.end()) {
        network.sendEvent(attackerConnIt->second, std::span<const uint8_t>(data.data(), data.size()));
    }
#else
    (void)network;
    (void)entityToConnection;
#endif

    (void)location;

    // [DATABASE_AGENT] Log combat event to ScyllaDB for analytics
    HitResult hit;
    hit.hit = true;
    hit.target = target;
    hit.damageDealt = damage;
    hit.hitLocation = location;
    hit.hitType = "damage";
    hit.isCritical = false;
    logCombatEvent(hit, attacker, target);
}

void CombatEventHandler::logCombatEvent(const HitResult& hit, EntityID attacker, EntityID target) {
    auto* scylla = zone_.getScylla();
    if (!scylla || !scylla->isConnected()) return;

    auto& registry = zone_.getRegistry();
    auto& config = zone_.getConfig();

    // Get player IDs from PlayerInfo - use persistent IDs, not entity IDs
    uint64_t attackerPlayerId = 0;
    uint64_t targetPlayerId = 0;
    std::string attackerName = "Unknown";
    std::string targetName = "Unknown";

    if (const PlayerInfo* info = registry.try_get<PlayerInfo>(attacker)) {
        attackerPlayerId = info->playerId;
        attackerName = info->username;
    }
    if (const PlayerInfo* info = registry.try_get<PlayerInfo>(target)) {
        targetPlayerId = info->playerId;
        targetName = info->username;
    }

    // Skip logging if neither party is a player (NPC vs NPC not logged)
    if (attackerPlayerId == 0 && targetPlayerId == 0) {
        return;
    }

    CombatEvent event;
    event.eventId = 0;
    event.timestamp = zone_.getCurrentTimeMs();
    event.zoneId = config.zoneId;
    event.attackerId = attackerPlayerId;
    event.targetId = targetPlayerId;
    event.eventType = hit.hitType ? hit.hitType : "damage";
    event.damageAmount = hit.damageDealt;
    event.isCritical = hit.isCritical;
    event.weaponType = "melee";
    event.position = hit.hitLocation;
    event.serverTick = zone_.getCurrentTick();

    scylla->logCombatEvent(event, [attackerPlayerId, targetPlayerId, attackerName, targetName](bool success) {
        if (!success) {
            std::cerr << "[SCYLLA] Failed to log combat event: "
                      << attackerName << "(" << attackerPlayerId << ") -> "
                      << targetName << "(" << targetPlayerId << ")" << std::endl;
        }
    });
}

} // namespace DarkAges
