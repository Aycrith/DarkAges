#pragma once

#include "ecs/CoreTypes.hpp"
#include "db/RedisManager.hpp"
#include "db/ScyllaManager.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <string>
#include <functional>

namespace DarkAges {

class ZoneServer;

class PlayerManager {
public:
    explicit PlayerManager() : zoneServer_(nullptr), redis_(nullptr), scylla_(nullptr), zoneId_(1) {}
    explicit PlayerManager(ZoneServer* zoneServer);
    ~PlayerManager();

    void setZoneServer(ZoneServer* zoneServer) { zoneServer_ = zoneServer; }

    void setDatabaseConnections(RedisManager* redis, ScyllaManager* scylla);
    void setZoneId(uint32_t zoneId);

    EntityID registerPlayer(ConnectionID connectionId, uint64_t playerId,
                         const std::string& username, const Position& spawnPos);

    void unregisterPlayer(EntityID entity);
    void despawnPlayer(EntityID entity);
    void removeConnectionMapping(EntityID entity);

    [[nodiscard]] EntityID getEntityByConnection(ConnectionID connectionId) const;
    [[nodiscard]] ConnectionID getConnectionByEntity(EntityID entity) const;
    [[nodiscard]] EntityID getEntityByPlayerId(uint64_t playerId) const;

    bool isPlayerConnected(uint64_t playerId) const;
    bool isEntityPlayer(EntityID entity) const;

    void saveAllPlayerStates();

    size_t getPlayerCount() const { return connectionToEntity_.size(); }

    void forEachPlayer(const std::function<void(EntityID, ConnectionID, const PlayerInfo*)>& callback);

    void savePlayerState(EntityID entity);

    uint32_t getCurrentTimeMs() const;

    std::vector<std::pair<ConnectionID, EntityID>> getAllPlayers() const;

private:
    ZoneServer* zoneServer_;
    RedisManager* redis_;
    ScyllaManager* scylla_;
    uint32_t zoneId_;

    std::unordered_map<ConnectionID, EntityID> connectionToEntity_;
    std::unordered_map<EntityID, ConnectionID> entityToConnection_;
    std::unordered_map<uint64_t, EntityID> playerIdToEntity_;

    void updatePlayerIdMapping(EntityID entity, uint64_t playerId);
    void removePlayerIdMapping(EntityID entity);
};

} // namespace DarkAges