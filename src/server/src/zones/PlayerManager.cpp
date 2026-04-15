#include "zones/PlayerManager.hpp"
#include "zones/ZoneServer.hpp"
#include <iostream>
#include <cstring>

namespace DarkAges {

PlayerManager::PlayerManager(ZoneServer* zoneServer)
    : zoneServer_(zoneServer)
    , redis_(nullptr)
    , scylla_(nullptr)
    , zoneId_(1) {
}

PlayerManager::~PlayerManager() = default;

void PlayerManager::setDatabaseConnections(RedisManager* redis, ScyllaManager* scylla) {
    redis_ = redis;
    scylla_ = scylla;
}

void PlayerManager::setZoneId(uint32_t zoneId) {
    zoneId_ = zoneId;
}

EntityID PlayerManager::registerPlayer(ConnectionID connectionId, uint64_t playerId,
                                       const std::string& username, 
                                       const Position& spawnPos) {
    auto& registry = zoneServer_->getRegistry();

    EntityID entity = registry.create();

    registry.emplace<Position>(entity, spawnPos);
    registry.emplace<Velocity>(entity);
    registry.emplace<Rotation>(entity);
    registry.emplace<BoundingVolume>(entity);
    registry.emplace<InputState>(entity);
    registry.emplace<CombatState>(entity);
    registry.emplace<NetworkState>(entity);
    registry.emplace<AntiCheatState>(entity);

    PlayerInfo& info = registry.emplace<PlayerInfo>(entity);
    info.playerId = playerId;
    info.connectionId = connectionId;
    std::strncpy(info.username, username.c_str(), sizeof(info.username) - 1);
    info.username[sizeof(info.username) - 1] = '\0';
    info.sessionStart = getCurrentTimeMs();

    registry.emplace<PlayerTag>(entity);

    connectionToEntity_[connectionId] = entity;
    entityToConnection_[entity] = connectionId;
    playerIdToEntity_[playerId] = entity;

    return entity;
}

void PlayerManager::unregisterPlayer(EntityID entity) {
    auto& registry = zoneServer_->getRegistry();

    auto it = entityToConnection_.find(entity);
    if (it != entityToConnection_.end()) {
        ConnectionID connId = it->second;
        connectionToEntity_.erase(connId);
        entityToConnection_.erase(it);
    }

    auto view = registry.view<PlayerInfo>();
    for (auto e : view) {
        if (e == entity) {
            const PlayerInfo* info = registry.try_get<PlayerInfo>(e);
            if (info) {
                playerIdToEntity_.erase(info->playerId);
            }
            break;
        }
    }

    savePlayerState(entity);

    registry.destroy(entity);
}

void PlayerManager::despawnPlayer(EntityID entity) {
    auto& registry = zoneServer_->getRegistry();

    auto it = entityToConnection_.find(entity);
    if (it != entityToConnection_.end()) {
        ConnectionID connId = it->second;
        connectionToEntity_.erase(connId);
        entityToConnection_.erase(it);
    }

    auto view = registry.view<PlayerInfo>();
    for (auto e : view) {
        if (e == entity) {
            const PlayerInfo* info = registry.try_get<PlayerInfo>(e);
            if (info) {
                playerIdToEntity_.erase(info->playerId);
            }
            break;
        }
    }

    registry.destroy(entity);
}

void PlayerManager::removeConnectionMapping(EntityID entity) {
    auto it = entityToConnection_.find(entity);
    if (it != entityToConnection_.end()) {
        connectionToEntity_.erase(it->second);
        entityToConnection_.erase(it);
    }
}

EntityID PlayerManager::getEntityByConnection(ConnectionID connectionId) const {
    auto it = connectionToEntity_.find(connectionId);
    if (it != connectionToEntity_.end()) {
        return it->second;
    }
    return entt::null;
}

ConnectionID PlayerManager::getConnectionByEntity(EntityID entity) const {
    auto it = entityToConnection_.find(entity);
    if (it != entityToConnection_.end()) {
        return it->second;
    }
    return 0;
}

EntityID PlayerManager::getEntityByPlayerId(uint64_t playerId) const {
    auto it = playerIdToEntity_.find(playerId);
    if (it != playerIdToEntity_.end()) {
        return it->second;
    }
    return entt::null;
}

bool PlayerManager::isPlayerConnected(uint64_t playerId) const {
    return playerIdToEntity_.find(playerId) != playerIdToEntity_.end();
}

bool PlayerManager::isEntityPlayer(EntityID entity) const {
    auto& registry = zoneServer_->getRegistry();
    return registry.all_of<PlayerTag>(entity);
}

void PlayerManager::saveAllPlayerStates() {
    for (const auto& [connId, entityId] : connectionToEntity_) {
        savePlayerState(entityId);
    }
}

void PlayerManager::forEachPlayer(const std::function<void(EntityID, ConnectionID, const PlayerInfo*)>& callback) {
    auto& registry = zoneServer_->getRegistry();

    for (const auto& [connId, entityId] : connectionToEntity_) {
        const PlayerInfo* info = registry.try_get<PlayerInfo>(entityId);
        callback(entityId, connId, info);
    }
}

void PlayerManager::savePlayerState(EntityID entity) {
    auto& registry = zoneServer_->getRegistry();

    const PlayerInfo* info = registry.try_get<PlayerInfo>(entity);
    if (!info) return;

    const Position* pos = registry.try_get<Position>(entity);

    uint32_t currentTimeMs = getCurrentTimeMs();

    if (redis_ && redis_->isConnected()) {
        PlayerSession session;
        session.playerId = info->playerId;
        session.zoneId = zoneId_;
        session.connectionId = info->connectionId;
        if (pos) {
            session.position = *pos;
        }
        if (const CombatState* combat = registry.try_get<CombatState>(entity)) {
            session.health = combat->health;
        } else {
            session.health = Constants::DEFAULT_HEALTH;
        }
        session.lastActivity = currentTimeMs;
        std::strncpy(session.username, info->username, sizeof(session.username) - 1);
        session.username[sizeof(session.username) - 1] = '\0';

        redis_->savePlayerSession(session, [playerId = info->playerId](bool success) {
            if (!success) {
                std::cerr << "[REDIS] Failed to save session for player " << playerId << std::endl;
            }
        });

        if (pos) {
            redis_->updatePlayerPosition(info->playerId, *pos, currentTimeMs);
        }

        redis_->addPlayerToZone(zoneId_, info->playerId);
    }

    if (scylla_ && scylla_->isConnected()) {
        scylla_->savePlayerState(info->playerId, zoneId_, currentTimeMs,
            [playerId = info->playerId](bool success) {
                if (!success) {
                    std::cerr << "[SCYLLA] Failed to persist state for player " << playerId << std::endl;
                }
            });
    }
}

uint32_t PlayerManager::getCurrentTimeMs() const {
    return zoneServer_->getCurrentTimeMs();
}

std::vector<std::pair<ConnectionID, EntityID>> PlayerManager::getAllPlayers() const {
    std::vector<std::pair<ConnectionID, EntityID>> result;
    result.reserve(connectionToEntity_.size());
    for (const auto& [connId, entityId] : connectionToEntity_) {
        result.emplace_back(connId, entityId);
    }
    return result;
}

} // namespace DarkAges