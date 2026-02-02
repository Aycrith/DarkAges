#include "zones/AreaOfInterest.hpp"
#include "Constants.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace DarkAges {

AOIResult AreaOfInterestSystem::queryVisibleEntities(
    Registry& registry,
    const SpatialHash& spatialHash,
    EntityID viewerEntity,
    const Position& viewerPos) {
    
    AOIResult result;
    
    // Query spatial hash for entities within FAR radius (200m)
    // Convert fixed-point to float for spatial hash query
    float viewerX = viewerPos.x * Constants::FIXED_TO_FLOAT;
    float viewerZ = viewerPos.z * Constants::FIXED_TO_FLOAT;
    
    // Use spatial hash for O(1) range query
    auto nearby = spatialHash.query(viewerX, viewerZ, Constants::AOI_RADIUS_FAR);
    
    // Reserve capacity to avoid reallocations
    result.visibleEntities.reserve(nearby.size());
    
    for (EntityID entity : nearby) {
        // Don't include self
        if (entity == viewerEntity) continue;
        
        // Check if entity has position component
        const Position* pos = registry.try_get<Position>(entity);
        if (!pos) continue;
        
        // Calculate actual distance (using fixed-point for consistency)
        float distSq = viewerPos.distanceSqTo(*pos);
        float dist = std::sqrt(distSq) * Constants::FIXED_TO_FLOAT;
        
        // Only include entities within FAR radius
        if (dist <= Constants::AOI_RADIUS_FAR) {
            result.visibleEntities.push_back(entity);
        }
    }
    
    return result;
}

int AreaOfInterestSystem::getUpdatePriority(const Position& viewerPos, const Position& targetPos) const {
    // Calculate distance using fixed-point math for consistency
    // distanceSqTo() returns distance-squared in fixed-point units
    // We need to convert to float first (meters squared), then sqrt to get distance in meters
    float distSq = viewerPos.distanceSqTo(targetPos) * Constants::FIXED_TO_FLOAT;
    float dist = std::sqrt(distSq);
    
    // Return priority based on distance tiers
    if (dist <= Constants::AOI_RADIUS_NEAR) {
        return 0;  // Full rate (20Hz) for nearby entities
    }
    if (dist <= Constants::AOI_RADIUS_MID) {
        return 1;  // Half rate (10Hz) for mid-range entities
    }
    // Quarter rate (5Hz) for far entities (within AOI_RADIUS_FAR)
    return 2;
}

void AreaOfInterestSystem::updatePlayerAOI(ConnectionID connId, const std::vector<EntityID>& visible) {
    auto& state = playerAOIStates_[connId];
    state.lastVisible.clear();
    for (EntityID e : visible) {
        state.lastVisible.insert(e);
    }
}

AOIResult AreaOfInterestSystem::getAOIChanges(ConnectionID connId, const std::vector<EntityID>& currentlyVisible) {
    AOIResult result;
    result.visibleEntities = currentlyVisible;  // Copy current visible list
    
    auto it = playerAOIStates_.find(connId);
    if (it == playerAOIStates_.end()) {
        // First update for this player - everything is "entered"
        result.enteredEntities = currentlyVisible;
        updatePlayerAOI(connId, currentlyVisible);
        return result;
    }
    
    const auto& lastVisible = it->second.lastVisible;
    
    // Find entered entities (in current but not in last)
    for (EntityID e : currentlyVisible) {
        if (lastVisible.find(e) == lastVisible.end()) {
            result.enteredEntities.push_back(e);
        }
    }
    
    // Find left entities (in last but not in current)
    for (EntityID e : lastVisible) {
        if (std::find(currentlyVisible.begin(), currentlyVisible.end(), e) == currentlyVisible.end()) {
            result.leftEntities.push_back(e);
        }
    }
    
    // Update stored state
    updatePlayerAOI(connId, currentlyVisible);
    
    return result;
}

void AreaOfInterestSystem::removePlayer(ConnectionID connId) {
    playerAOIStates_.erase(connId);
}

} // namespace DarkAges
