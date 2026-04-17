// Combat event handling implementation
// Extracted from ZoneServer.cpp to improve code organization

#include "zones/CombatEventHandler.hpp"
#include "zones/ZoneServer.hpp"
#include "netcode/NetworkManager.hpp"
#include "netcode/ProtobufProtocol.hpp"
#include "db/ScyllaManager.hpp"
#include "security/AntiCheat.hpp"
#include <iostream>
#include <cstring>
#include <ctime>
#include <cmath>
#include <glm/glm.hpp>

namespace DarkAges {

CombatEventHandler::CombatEventHandler(ZoneServer& server)
    : server_(server) {
}

void CombatEventHandler::setConnectionMappings(
    std::unordered_map<ConnectionID, EntityID>* connToEntity,
    std::unordered_map<EntityID, ConnectionID>* entityToConn) {
    connectionToEntity_ = connToEntity;
    entityToConnection_ = entityToConn;
}

uint32_t CombatEventHandler::getCurrentTimeMs() const {
    return server_.getCurrentTimeMs();
}

void CombatEventHandler::processCombat() {
    // Combat is triggered by client inputs (attack action)
    // The actual combat processing happens in validateAndApplyInput
    // when an attack input is received
}

void CombatEventHandler::onEntityDied(EntityID victim, EntityID killer) {
    auto& registry = server_.getRegistry();
    uint32_t currentTick = server_.getCurrentTick();

    std::cout << "[ZONE " << server_.getConfig().zoneId << "] Entity " << static_cast<uint32_t>(victim)
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
    if (network_) {
#ifdef DARKAGES_HAS_PROTOBUF
        auto deathEvent = Netcode::ProtobufProtocol::createPlayerDeathEvent(
            static_cast<uint32_t>(victim),
            static_cast<uint32_t>(killer)
        );
        deathEvent.set_timestamp(getCurrentTimeMs());
        auto eventData = Netcode::ProtobufProtocol::serializeEvent(deathEvent);
        network_->broadcastEvent(eventData);
#endif
    }

    // [DATABASE_AGENT] Log kill event to ScyllaDB
    if (scylla_ && scylla_->isConnected()) {
        // Log the kill for the killer
        if (killerPlayerId > 0) {
            CombatEvent killEvent;
            killEvent.eventId = 0;
            killEvent.timestamp = getCurrentTimeMs();
            killEvent.zoneId = server_.getConfig().zoneId;
            killEvent.attackerId = killerPlayerId;
            killEvent.targetId = victimPlayerId;
            killEvent.eventType = "kill";
            killEvent.damageAmount = 0;  // Final blow already logged as damage
            killEvent.isCritical = false;
            killEvent.weaponType = "melee";
            killEvent.position = killLocation;
            killEvent.serverTick = currentTick;

            scylla_->logCombatEvent(killEvent, nullptr);  // No callback needed
        }

        // Log the death for the victim
        if (victimPlayerId > 0) {
            CombatEvent deathEvent;
            deathEvent.eventId = 0;
            deathEvent.timestamp = getCurrentTimeMs();
            deathEvent.zoneId = server_.getConfig().zoneId;
            deathEvent.attackerId = killerPlayerId;
            deathEvent.targetId = victimPlayerId;
            deathEvent.eventType = "death";
            deathEvent.damageAmount = 0;
            deathEvent.isCritical = false;
            deathEvent.weaponType = "melee";
            deathEvent.position = killLocation;
            deathEvent.serverTick = currentTick;

            scylla_->logCombatEvent(deathEvent, nullptr);
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
            killerStats.damageDealt = 0;  // Tracked separately
            killerStats.damageTaken = 0;
            killerStats.longestKillStreak = 0;
            killerStats.currentKillStreak = 0;

            scylla_->updatePlayerStats(killerStats, [killerName](bool success) {
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

            scylla_->updatePlayerStats(victimStats, [victimName](bool success) {
                if (!success) {
                    std::cerr << "[SCYLLA] Failed to update stats for victim: " << victimName << std::endl;
                }
            });
        }
    }

    // Schedule respawn
    pendingRespawns_.push_back({victim, getCurrentTimeMs() + RESPAWN_DELAY_MS});
}

void CombatEventHandler::processRespawns() {
    if (pendingRespawns_.empty()) return;

    auto& registry = server_.getRegistry();
    uint32_t currentTimeMs = getCurrentTimeMs();

    // Process from back to front for safe removal during iteration
    for (auto it = pendingRespawns_.begin(); it != pendingRespawns_.end(); ) {
        if (currentTimeMs >= it->respawnTimeMs) {
            EntityID entity = it->entity;

            // Verify entity still exists
            if (registry.valid(entity)) {
                // Restore health
                if (auto* stats = registry.try_get<CombatState>(entity)) {
                    stats->health = stats->maxHealth;
                }

                // Teleport to spawn point (origin for now)
                if (auto* pos = registry.try_get<Position>(entity)) {
                    pos->x = 0.0f;
                    pos->y = 0.0f;
                    pos->z = 0.0f;
                }

                std::cout << "[ZONE " << server_.getConfig().zoneId << "] Entity "
                          << static_cast<uint32_t>(entity) << " respawned" << std::endl;
            }

            it = pendingRespawns_.erase(it);
        } else {
            ++it;
        }
    }
}

void CombatEventHandler::sendCombatEvent(EntityID attacker, EntityID target, int16_t damage, const Position& location) {
    if (!network_ || !entityToConnection_) return;

#ifdef DARKAGES_HAS_PROTOBUF
    auto attackerConnIt = entityToConnection_->find(attacker);
    auto targetConnIt = entityToConnection_->find(target);

    auto damageEvent = Netcode::ProtobufProtocol::createDamageEvent(
        static_cast<uint32_t>(attacker),
        static_cast<uint32_t>(target),
        static_cast<int32_t>(damage)
    );
    damageEvent.set_timestamp(getCurrentTimeMs());
    auto eventData = Netcode::ProtobufProtocol::serializeEvent(damageEvent);

    if (attackerConnIt != entityToConnection_->end()) {
        network_->sendEvent(attackerConnIt->second, eventData);
    }
    if (targetConnIt != entityToConnection_->end()) {
        network_->sendEvent(targetConnIt->second, eventData);
    }
#endif

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
    if (!scylla_ || !scylla_->isConnected()) return;

    auto& registry = server_.getRegistry();

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
    event.eventId = 0;  // Will be generated by UUID
    event.timestamp = getCurrentTimeMs();
    event.zoneId = server_.getConfig().zoneId;
    event.attackerId = attackerPlayerId;
    event.targetId = targetPlayerId;
    event.eventType = hit.hitType ? hit.hitType : "damage";
    event.damageAmount = hit.damageDealt;
    event.isCritical = hit.isCritical;
    event.weaponType = "melee";  // NOTE: CombatState has no weapon field
    event.position = hit.hitLocation;
    event.serverTick = server_.getCurrentTick();

    scylla_->logCombatEvent(event, [attackerPlayerId, targetPlayerId, attackerName, targetName](bool success) {
        if (!success) {
            std::cerr << "[SCYLLA] Failed to log combat event: "
                      << attackerName << "(" << attackerPlayerId << ") -> "
                      << targetName << "(" << targetPlayerId << ")" << std::endl;
        }
    });
}

void CombatEventHandler::processAttackInput(EntityID entity, const ClientInputPacket& input) {
    auto& registry = server_.getRegistry();

    // Build attack input from client data
    AttackInput attackInput;
    attackInput.type = AttackInput::MELEE;  // Default to melee
    attackInput.sequence = input.input.sequence;
    attackInput.timestamp = input.input.timestamp_ms;
    attackInput.aimDirection = glm::vec3(
        std::sin(input.input.yaw),
        0.0f,  // Ignore pitch for horizontal aim
        std::cos(input.input.yaw)
    );

    // Get RTT for lag compensation
    uint32_t rttMs = 100;  // Default fallback
    if (const NetworkState* netState = registry.try_get<NetworkState>(entity)) {
        rttMs = netState->rttMs;
        if (rttMs == 0) {
            rttMs = 100;  // Assume 100ms if not measured yet
        }
    }

    // Create lag-compensated attack
    uint32_t oneWayLatency = rttMs / 2;
    uint32_t clientTimestamp = (input.receiveTimeMs > oneWayLatency)
        ? input.receiveTimeMs - oneWayLatency
        : 0;

    LagCompensatedAttack lagAttack;
    lagAttack.attacker = entity;
    lagAttack.input = attackInput;
    lagAttack.clientTimestamp = clientTimestamp;
    lagAttack.serverTimestamp = getCurrentTimeMs();
    lagAttack.rttMs = rttMs;

    // Process attack with lag compensation
    if (!combatSystem_ || !lagCompensator_) return;

    LagCompensatedCombat lagCombat(*combatSystem_, *lagCompensator_);
    auto hits = lagCombat.processAttackWithRewind(registry, lagAttack);

    // Send hit results to relevant clients
    for (const auto& hit : hits) {
        if (hit.hit) {
            // [SECURITY_AGENT] Validate combat action for anti-cheat
            if (antiCheat_) {
                Position attackerPos{0, 0, 0, 0};
                if (const Position* pos = registry.try_get<Position>(entity)) {
                    attackerPos = *pos;
                }

                auto combatValidation = antiCheat_->validateCombat(entity, hit.target,
                                                                   hit.damageDealt, hit.hitLocation,
                                                                   attackerPos, registry);

                if (combatValidation.detected) {
                    std::cerr << "[ANTICHEAT] Combat validation failed: "
                              << combatValidation.description << std::endl;

                    if (combatValidation.severity == Security::ViolationSeverity::CRITICAL ||
                        combatValidation.severity == Security::ViolationSeverity::BAN) {
                        auto connIt = entityToConnection_->find(entity);
                        if (connIt != entityToConnection_->end()) {
                            network_->disconnect(connIt->second, combatValidation.description);
                        }
                        return;
                    }
                    continue;  // Skip applying this hit
                }
            }

            // Send damage event to target
            auto targetConnIt = entityToConnection_->find(hit.target);
            if (targetConnIt != entityToConnection_->end()) {
#ifdef DARKAGES_HAS_PROTOBUF
                auto damageEvent = Netcode::ProtobufProtocol::createDamageEvent(
                    static_cast<uint32_t>(entity),
                    static_cast<uint32_t>(hit.target),
                    static_cast<int32_t>(hit.damageDealt)
                );
                damageEvent.set_timestamp(getCurrentTimeMs());
                auto eventData = Netcode::ProtobufProtocol::serializeEvent(damageEvent);
                network_->sendEvent(targetConnIt->second, eventData);
#endif
                std::cerr << "[NETWORK] Sent damage event: " << hit.damageDealt
                          << " to entity " << static_cast<uint32_t>(hit.target) << std::endl;
            }

            // Send hit confirmation to attacker
            auto attackerConnIt = entityToConnection_->find(entity);
            if (attackerConnIt != entityToConnection_->end()) {
#ifdef DARKAGES_HAS_PROTOBUF
                auto hitConfirm = Netcode::ProtobufProtocol::createDamageEvent(
                    static_cast<uint32_t>(entity),
                    static_cast<uint32_t>(hit.target),
                    static_cast<int32_t>(hit.damageDealt)
                );
                hitConfirm.set_timestamp(getCurrentTimeMs());
                auto eventData = Netcode::ProtobufProtocol::serializeEvent(hitConfirm);
                network_->sendEvent(attackerConnIt->second, eventData);
#endif
                std::cerr << "[NETWORK] Sent hit confirmation: " << hit.damageDealt
                          << " to attacker entity " << static_cast<uint32_t>(entity) << std::endl;
            }
        }
    }
}

} // namespace DarkAges
