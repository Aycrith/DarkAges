// Aura projection and zone migration handling implementation
// Extracted from ZoneServer.cpp to improve code organization

#include "zones/AuraZoneHandler.hpp"
#include "zones/ZoneServer.hpp"
#include "netcode/NetworkManager.hpp"
#include "db/RedisManager.hpp"
#include <iostream>
#include <vector>

namespace DarkAges {

AuraZoneHandler::AuraZoneHandler(ZoneServer& server)
    : server_(server) {
}

void AuraZoneHandler::setConnectionMappings(
    std::unordered_map<ConnectionID, EntityID>* connToEntity,
    std::unordered_map<EntityID, ConnectionID>* entityToConn) {
    connectionToEntity_ = connToEntity;
    entityToConnection_ = entityToConn;
}

uint32_t AuraZoneHandler::getCurrentTimeMs() const {
    return server_.getCurrentTimeMs();
}

void AuraZoneHandler::initializeAuraManager() {
    const auto& config = server_.getConfig();

    // Build adjacent zone definitions from grid layout
    // Current zone is at grid position (myX, myZ) in a 2x2 grid
    std::vector<ZoneDefinition> adjacentZones;
    uint32_t myZoneId = config.zoneId;
    uint32_t myX = (myZoneId - 1) % 2;
    uint32_t myZ = (myZoneId - 1) / 2;

    float zoneWidth = (Constants::WORLD_MAX_X - Constants::WORLD_MIN_X) / 2.0f;
    float zoneDepth = (Constants::WORLD_MAX_Z - Constants::WORLD_MIN_Z) / 2.0f;

    // Check all 8 neighbors (including diagonals)
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue; // Skip self

            int nx = static_cast<int>(myX) + dx;
            int nz = static_cast<int>(myZ) + dz;

            // Bounds check for 2x2 grid
            if (nx < 0 || nx > 1 || nz < 0 || nz > 1) continue;

            uint32_t adjId = static_cast<uint32_t>(nz) * 2 + static_cast<uint32_t>(nx) + 1;

            ZoneDefinition adjDef;
            adjDef.zoneId = adjId;
            adjDef.zoneName = "Zone_" + std::to_string(adjId);
            adjDef.shape = ZoneShape::RECTANGLE;
            adjDef.minX = Constants::WORLD_MIN_X + static_cast<float>(nx) * zoneWidth;
            adjDef.maxX = adjDef.minX + zoneWidth;
            adjDef.minZ = Constants::WORLD_MIN_Z + static_cast<float>(nz) * zoneDepth;
            adjDef.maxZ = adjDef.minZ + zoneDepth;
            adjDef.centerX = (adjDef.minX + adjDef.maxX) / 2.0f;
            adjDef.centerZ = (adjDef.minZ + adjDef.maxZ) / 2.0f;
            adjDef.host = "127.0.0.1";
            adjDef.port = Constants::DEFAULT_SERVER_PORT + static_cast<uint16_t>(adjId) - 1;

            adjacentZones.push_back(adjDef);
        }
    }

    if (auraManager_) {
        auraManager_->initialize(adjacentZones);
    }

    std::cout << "[ZONE " << config.zoneId << "] Aura projection initialized" << std::endl;
}

void AuraZoneHandler::initializeHandoffController() {
    const auto& config = server_.getConfig();

    if (!handoffController_ || !migrationManager_ || !auraManager_) return;

    // Set up zone definition for distance calculations
    ZoneDefinition zoneDef;
    zoneDef.zoneId = config.zoneId;
    zoneDef.minX = config.minX;
    zoneDef.maxX = config.maxX;
    zoneDef.minZ = config.minZ;
    zoneDef.maxZ = config.maxZ;
    zoneDef.centerX = (config.minX + config.maxX) / 2.0f;
    zoneDef.centerZ = (config.minZ + config.maxZ) / 2.0f;
    handoffController_->setMyZoneDefinition(zoneDef);

    // Set up zone lookup callbacks
    handoffController_->setZoneLookupCallbacks(
        [this](uint32_t zoneId) -> ZoneDefinition* { return lookupZone(zoneId); },
        [this](float x, float z) -> uint32_t { return findZoneByPosition(x, z); }
    );

    // Set up handoff callbacks
    handoffController_->setOnHandoffStarted(
        [this](uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success) {
            onHandoffStarted(playerId, sourceZone, targetZone, success);
        }
    );
    handoffController_->setOnHandoffCompleted(
        [this](uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success) {
            onHandoffCompleted(playerId, sourceZone, targetZone, success);
        }
    );

    // Initialize with default config
    if (!handoffController_->initialize()) {
        std::cerr << "[ZONE " << config.zoneId << "] Failed to initialize handoff controller!" << std::endl;
    } else {
        std::cout << "[ZONE " << config.zoneId << "] Zone handoff controller initialized" << std::endl;
    }
}

void AuraZoneHandler::syncAuraState() {
    if (!redis_ || !auraManager_) return;

    uint32_t currentTime = getCurrentTimeMs();

    if (currentTime - lastAuraSyncTime_ < AURA_SYNC_INTERVAL_MS) {
        return;
    }
    lastAuraSyncTime_ = currentTime;

    // Get entities in aura to sync to adjacent zones
    std::vector<AuraEntityState> entitiesToSync;
    auraManager_->getEntitiesToSync(entitiesToSync);

    // Publish to Redis for adjacent zones
    if (redis_->isConnected() && !entitiesToSync.empty()) {
        const auto& config = server_.getConfig();

        // Batch all entities into a single ZoneMessage payload
        std::vector<uint8_t> payload;
        auto packU32 = [&payload](uint32_t v) {
            payload.push_back(static_cast<uint8_t>(v & 0xFF));
            payload.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            payload.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            payload.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        };

        // Pack entity count first, then each entity state
        packU32(static_cast<uint32_t>(entitiesToSync.size()));
        for (const auto& entity : entitiesToSync) {
            packU32(static_cast<uint32_t>(entity.entity));
            packU32(entity.ownerZoneId);
            packU32(static_cast<uint32_t>(entity.lastKnownPosition.x));
            packU32(static_cast<uint32_t>(entity.lastKnownPosition.y));
            packU32(static_cast<uint32_t>(entity.lastKnownPosition.z));
            packU32(static_cast<uint32_t>(entity.lastKnownVelocity.dx));
            packU32(static_cast<uint32_t>(entity.lastKnownVelocity.dy));
            packU32(static_cast<uint32_t>(entity.lastKnownVelocity.dz));
            packU32(entity.lastUpdateTick);
            payload.push_back(entity.isGhost ? 1 : 0);
        }

        ZoneMessage msg;
        msg.type = ZoneMessageType::ENTITY_SYNC;
        msg.sourceZoneId = config.zoneId;
        msg.targetZoneId = 0; // broadcast to adjacent
        msg.timestamp = currentTime;
        msg.sequence = 0;
        msg.payload = std::move(payload);

        redis_->publishToZone(config.zoneId, msg);
    }
}

void AuraZoneHandler::handleAuraEntityMigration() {
    if (!redis_ || !auraManager_) return;

    auto& registry = server_.getRegistry();
    const auto& config = server_.getConfig();

    // Check for entities that need ownership transfer
    auto view = registry.view<Position>(entt::exclude<StaticTag>);

    for (auto entity : view) {
        const Position& pos = view.get<Position>(entity);

        // Skip if not in aura
        if (!auraManager_->isEntityInAura(entity)) {
            continue;
        }

        // Check if we should take ownership
        if (auraManager_->getEntityOwnerZone(entity) != config.zoneId) {
            if (auraManager_->shouldTakeOwnership(entity, pos)) {
                uint32_t fromZoneId = auraManager_->getEntityOwnerZone(entity);
                auraManager_->onOwnershipTransferred(entity, fromZoneId);

                // Notify the previous zone of ownership transfer
                ZoneMessage migrateMsg;
                migrateMsg.type = ZoneMessageType::MIGRATION_COMPLETE;
                migrateMsg.sourceZoneId = config.zoneId;
                migrateMsg.targetZoneId = fromZoneId;
                migrateMsg.timestamp = getCurrentTimeMs();
                migrateMsg.sequence = 0;
                // Pack: entityID(4) + newOwnerZoneId(4)
                std::vector<uint8_t> mPayload(8);
                uint32_t eid = static_cast<uint32_t>(entity);
                mPayload[0] = static_cast<uint8_t>(eid & 0xFF);
                mPayload[1] = static_cast<uint8_t>((eid >> 8) & 0xFF);
                mPayload[2] = static_cast<uint8_t>((eid >> 16) & 0xFF);
                mPayload[3] = static_cast<uint8_t>((eid >> 24) & 0xFF);
                uint32_t nid = config.zoneId;
                mPayload[4] = static_cast<uint8_t>(nid & 0xFF);
                mPayload[5] = static_cast<uint8_t>((nid >> 8) & 0xFF);
                mPayload[6] = static_cast<uint8_t>((nid >> 16) & 0xFF);
                mPayload[7] = static_cast<uint8_t>((nid >> 24) & 0xFF);
                migrateMsg.payload = std::move(mPayload);
                redis_->publishToZone(fromZoneId, migrateMsg);

                // Update entity's zone assignment in Redis
                redis_->set("entity:" + std::to_string(eid) + ":zone",
                           std::to_string(config.zoneId));
            }
        }
    }
}

void AuraZoneHandler::checkEntityZoneTransitions() {
    if (!auraManager_) return;

    auto& registry = server_.getRegistry();
    const auto& config = server_.getConfig();

    auto view = registry.view<Position>(entt::exclude<StaticTag>);

    for (auto entity : view) {
        const Position& pos = view.get<Position>(entity);

        // Check if entity is entering aura from core
        if (auraManager_->isInAuraBuffer(pos)) {
            if (!auraManager_->isEntityInAura(entity)) {
                // Entity entering aura from our core
                auraManager_->onEntityEnteringAura(entity, pos, config.zoneId);
            } else {
                // Update entity state in aura
                Velocity vel{};
                if (auto* v = registry.try_get<Velocity>(entity)) {
                    vel = *v;
                }
                auraManager_->updateEntityState(entity, pos, vel, server_.getCurrentTick());
            }
        } else if (auraManager_->isInCoreZone(pos)) {
            // Entity is in our core - if it was in aura, it left
            if (auraManager_->isEntityInAura(entity)) {
                auraManager_->onEntityLeavingAura(entity, config.zoneId);
            }
        }
    }

    // Handle ownership transfers within aura
    handleAuraEntityMigration();
}

void AuraZoneHandler::onEntityMigrationComplete(EntityID entity, bool success) {
    const auto& config = server_.getConfig();

    if (success) {
        std::cout << "[ZONE " << config.zoneId
                  << "] Entity migration completed successfully for entity "
                  << static_cast<uint32_t>(entity) << std::endl;

        // Update connection mapping
        if (entityToConnection_) {
            auto it = entityToConnection_->find(entity);
            if (it != entityToConnection_->end()) {
                ConnectionID connId = it->second;
                if (connectionToEntity_) {
                    connectionToEntity_->erase(connId);
                }
                entityToConnection_->erase(it);
            }
        }
    } else {
        std::cerr << "[ZONE " << config.zoneId
                  << "] Entity migration failed for entity "
                  << static_cast<uint32_t>(entity) << std::endl;
    }
}

void AuraZoneHandler::updateZoneHandoffs() {
    if (!handoffController_) return;

    auto& registry = server_.getRegistry();
    uint32_t currentTimeMs = getCurrentTimeMs();

    // Check all players for potential handoffs
    auto view = registry.view<PlayerInfo, Position, Velocity>();

    view.each([this, currentTimeMs](EntityID entity, const PlayerInfo& info,
                                     const Position& pos, const Velocity& vel) {
        if (!entityToConnection_) return;

        auto connIt = entityToConnection_->find(entity);
        if (connIt == entityToConnection_->end()) return;

        handoffController_->checkPlayerPosition(info.playerId, entity, connIt->second,
                                               pos, vel, currentTimeMs);
    });

    // Update handoff controller state machine
    handoffController_->update(registry, currentTimeMs);
}

void AuraZoneHandler::onHandoffStarted(uint64_t playerId, uint32_t sourceZone,
                                        uint32_t targetZone, bool success) {
    const auto& config = server_.getConfig();

    std::cout << "[ZONE " << config.zoneId << "] Handoff started for player "
              << playerId << " from zone " << sourceZone << " to zone " << targetZone << std::endl;
}

void AuraZoneHandler::onHandoffCompleted(uint64_t playerId, uint32_t sourceZone,
                                          uint32_t targetZone, bool success) {
    const auto& config = server_.getConfig();

    if (success) {
        std::cout << "[ZONE " << config.zoneId << "] Handoff completed for player "
                  << playerId << " to zone " << targetZone << std::endl;
    } else {
        std::cerr << "[ZONE " << config.zoneId << "] Handoff failed for player "
                  << playerId << " to zone " << targetZone << std::endl;
    }
}

ZoneDefinition* AuraZoneHandler::lookupZone(uint32_t zoneId) {
    // Simple grid layout: zones are adjacent rectangles
    static ZoneDefinition tempDef;
    tempDef.zoneId = zoneId;

    // Calculate approximate position based on zone ID
    // Assuming 2x2 grid for simple case
    uint32_t zoneX = (zoneId - 1) % 2;
    uint32_t zoneZ = (zoneId - 1) / 2;

    float zoneWidth = (Constants::WORLD_MAX_X - Constants::WORLD_MIN_X) / 2.0f;
    float zoneDepth = (Constants::WORLD_MAX_Z - Constants::WORLD_MIN_Z) / 2.0f;

    tempDef.minX = Constants::WORLD_MIN_X + zoneX * zoneWidth;
    tempDef.maxX = tempDef.minX + zoneWidth;
    tempDef.minZ = Constants::WORLD_MIN_Z + zoneZ * zoneDepth;
    tempDef.maxZ = tempDef.minZ + zoneDepth;
    tempDef.centerX = (tempDef.minX + tempDef.maxX) / 2.0f;
    tempDef.centerZ = (tempDef.minZ + tempDef.maxZ) / 2.0f;
    tempDef.port = Constants::DEFAULT_SERVER_PORT + static_cast<uint16_t>(zoneId) - 1;
    tempDef.host = "127.0.0.1";  // Localhost for testing

    return &tempDef;
}

uint32_t AuraZoneHandler::findZoneByPosition(float x, float z) {
    // Simple grid-based zone lookup
    float zoneWidth = (Constants::WORLD_MAX_X - Constants::WORLD_MIN_X) / 2.0f;
    float zoneDepth = (Constants::WORLD_MAX_Z - Constants::WORLD_MIN_Z) / 2.0f;

    uint32_t zoneX = static_cast<uint32_t>((x - Constants::WORLD_MIN_X) / zoneWidth);
    uint32_t zoneZ = static_cast<uint32_t>((z - Constants::WORLD_MIN_Z) / zoneDepth);

    // Clamp to valid range
    zoneX = std::min(zoneX, 1u);
    zoneZ = std::min(zoneZ, 1u);

    return zoneZ * 2 + zoneX + 1;  // 1-indexed zone IDs
}

} // namespace DarkAges
