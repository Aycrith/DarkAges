#pragma once

#include "ecs/CoreTypes.hpp"
#include "physics/SpatialHash.hpp"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstddef>

// [ZONE_AGENT] Area of Interest (AOI) system for entity visibility management
// Filters entities so players only receive updates for entities within their AOI
// This reduces bandwidth and client processing requirements

namespace DarkAges {

// AOI query result for a player
struct AOIResult {
    std::vector<EntityID> visibleEntities;     // Entities to include in snapshot
    std::vector<EntityID> enteredEntities;     // Newly entered AOI (for spawn events)
    std::vector<EntityID> leftEntities;        // Left AOI (for despawn events)
};

// [ZONE_AGENT] Area of Interest management system
class AreaOfInterestSystem {
public:
    // Query entities visible to a player at given position
    // Uses spatial hash for O(1) queries instead of O(n) scan
    AOIResult queryVisibleEntities(
        Registry& registry,
        const SpatialHash& spatialHash,
        EntityID viewerEntity,
        const Position& viewerPos
    );
    
    // Get update rate priority for an entity based on distance
    // Returns: 0 = full rate (20Hz), 1 = half rate (10Hz), 2 = quarter rate (5Hz)
    int getUpdatePriority(const Position& viewerPos, const Position& targetPos) const;
    
    // Track which entities each player can see (for enter/leave detection)
    void updatePlayerAOI(ConnectionID connId, const std::vector<EntityID>& visible);
    
    // Get entities that entered/left since last update
    AOIResult getAOIChanges(ConnectionID connId, const std::vector<EntityID>& currentlyVisible);
    
    // Remove player AOI state when they disconnect
    void removePlayer(ConnectionID connId);
    
    // Get the count of players being tracked
    [[nodiscard]] size_t getPlayerCount() const { return playerAOIStates_.size(); }

private:
    // Per-player AOI state
    struct PlayerAOIState {
        std::unordered_set<EntityID> lastVisible;
    };
    
    std::unordered_map<ConnectionID, PlayerAOIState> playerAOIStates_;
};

} // namespace DarkAges
