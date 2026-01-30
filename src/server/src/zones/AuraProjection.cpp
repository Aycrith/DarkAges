#include "zones/AuraProjection.hpp"
#include "Constants.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace DarkAges {

AuraProjectionManager::AuraProjectionManager(uint32_t myZoneId) 
    : myZoneId_(myZoneId) {}

void AuraProjectionManager::initialize(const std::vector<ZoneDefinition>& adjacentZones) {
    adjacentZones_ = adjacentZones;
    
    // Set up my zone definition (would be passed in or looked up)
    // For now, placeholder - caller should set myZoneDef_ via a setter or pass it
}

bool AuraProjectionManager::isInAuraBuffer(const Position& pos) const {
    return isPositionInMyAura(pos) && !isPositionInMyCore(pos);
}

bool AuraProjectionManager::isInCoreZone(const Position& pos) const {
    return isPositionInMyCore(pos);
}

void AuraProjectionManager::onEntityEnteringAura(EntityID entity, const Position& pos,
                                                uint32_t fromZoneId) {
    AuraEntityState state;
    state.entity = entity;
    state.ownerZoneId = fromZoneId;
    state.lastKnownPosition = pos;
    state.lastKnownVelocity = Velocity{};
    state.lastUpdateTick = 0;
    state.isGhost = (fromZoneId != myZoneId_);
    
    auraEntities_[entity] = state;
    entityOwners_[entity] = fromZoneId;
    
    std::cout << "[AURA] Entity " << static_cast<uint32_t>(entity) 
              << " entered aura from zone " << fromZoneId << std::endl;
}

void AuraProjectionManager::onEntityLeavingAura(EntityID entity, uint32_t toZoneId) {
    auto it = auraEntities_.find(entity);
    if (it != auraEntities_.end()) {
        std::cout << "[AURA] Entity " << static_cast<uint32_t>(entity) 
                  << " left aura to zone " << toZoneId << std::endl;
        
        auraEntities_.erase(it);
        entityOwners_.erase(entity);
    }
}

void AuraProjectionManager::updateEntityState(EntityID entity, const Position& pos,
                                             const Velocity& vel, uint32_t tick) {
    auto it = auraEntities_.find(entity);
    if (it != auraEntities_.end()) {
        it->second.lastKnownPosition = pos;
        it->second.lastKnownVelocity = vel;
        it->second.lastUpdateTick = tick;
    }
}

void AuraProjectionManager::onEntityStateFromAdjacentZone(uint32_t zoneId, EntityID entity,
                                                         const Position& pos, 
                                                         const Velocity& vel) {
    // Another zone is telling us about an entity in the shared aura
    auto it = auraEntities_.find(entity);
    if (it != auraEntities_.end()) {
        // Update ghost copy
        it->second.lastKnownPosition = pos;
        it->second.lastKnownVelocity = vel;
        it->second.lastUpdateTick = 0;  // Will be set by caller
        it->second.ownerZoneId = zoneId;
        it->second.isGhost = true;
    } else {
        // New entity entering from adjacent zone
        AuraEntityState state;
        state.entity = entity;
        state.ownerZoneId = zoneId;
        state.lastKnownPosition = pos;
        state.lastKnownVelocity = vel;
        state.lastUpdateTick = 0;
        state.isGhost = true;
        
        auraEntities_[entity] = state;
        entityOwners_[entity] = zoneId;
    }
}

std::vector<EntityID> AuraProjectionManager::getEntitiesInAuraForPlayer(
    const Position& playerPos, float viewRadius) const {
    
    std::vector<EntityID> result;
    
    float px = playerPos.x * Constants::FIXED_TO_FLOAT;
    float pz = playerPos.z * Constants::FIXED_TO_FLOAT;
    
    for (const auto& [entity, state] : auraEntities_) {
        float ex = state.lastKnownPosition.x * Constants::FIXED_TO_FLOAT;
        float ez = state.lastKnownPosition.z * Constants::FIXED_TO_FLOAT;
        
        float dx = ex - px;
        float dz = ez - pz;
        float distSq = dx * dx + dz * dz;
        
        if (distSq <= (viewRadius * viewRadius)) {
            result.push_back(entity);
        }
    }
    
    return result;
}

bool AuraProjectionManager::isEntityInAura(EntityID entity) const {
    return auraEntities_.find(entity) != auraEntities_.end();
}

uint32_t AuraProjectionManager::getEntityOwnerZone(EntityID entity) const {
    auto it = entityOwners_.find(entity);
    if (it != entityOwners_.end()) {
        return it->second;
    }
    return 0;
}

bool AuraProjectionManager::shouldTakeOwnership(EntityID entity, 
                                               const Position& pos) const {
    // Check if entity is closer to our core than to adjacent zones
    uint32_t closestZone = findClosestZoneCenter(pos);
    
    if (closestZone != myZoneId_) {
        return false;
    }
    
    // Also check distance to edge - must be well within our zone
    float distToEdge = getDistanceToZoneEdge(pos);
    
    return distToEdge > OWNERSHIP_TRANSFER_THRESHOLD;
}

void AuraProjectionManager::onOwnershipTransferred(EntityID entity, 
                                                   uint32_t fromZoneId) {
    auto it = auraEntities_.find(entity);
    if (it != auraEntities_.end()) {
        it->second.ownerZoneId = myZoneId_;
        it->second.isGhost = false;
        entityOwners_[entity] = myZoneId_;
        
        std::cout << "[AURA] Ownership of entity " << static_cast<uint32_t>(entity) 
                  << " transferred from zone " << fromZoneId 
                  << " to zone " << myZoneId_ << std::endl;
    }
}

void AuraProjectionManager::getEntitiesToSync(
    std::vector<AuraEntityState>& outEntities) const {
    
    for (const auto& [entity, state] : auraEntities_) {
        // Only sync entities we own (not ghosts)
        if (!state.isGhost && state.ownerZoneId == myZoneId_) {
            outEntities.push_back(state);
        }
    }
}

void AuraProjectionManager::removeEntity(EntityID entity) {
    auraEntities_.erase(entity);
    entityOwners_.erase(entity);
}

float AuraProjectionManager::getDistanceToZoneEdge(const Position& pos) const {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    // Distance to each edge
    float distLeft = x - myZoneDef_.minX;
    float distRight = myZoneDef_.maxX - x;
    float distBottom = z - myZoneDef_.minZ;
    float distTop = myZoneDef_.maxZ - z;
    
    // Return minimum distance to any edge
    return std::min({distLeft, distRight, distBottom, distTop});
}

glm::vec2 AuraProjectionManager::getDirectionToZoneCenter(const Position& pos) const {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    float dx = myZoneDef_.centerX - x;
    float dz = myZoneDef_.centerZ - z;
    
    float len = std::sqrt(dx * dx + dz * dz);
    if (len > 0.001f) {
        return glm::vec2(dx / len, dz / len);
    }
    
    return glm::vec2(0, 0);
}

bool AuraProjectionManager::isPositionInMyAura(const Position& pos) const {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    return x >= myZoneDef_.minX && x <= myZoneDef_.maxX &&
           z >= myZoneDef_.minZ && z <= myZoneDef_.maxZ;
}

bool AuraProjectionManager::isPositionInMyCore(const Position& pos) const {
    return myZoneDef_.containsPosition(
        pos.x * Constants::FIXED_TO_FLOAT,
        pos.z * Constants::FIXED_TO_FLOAT);
}

uint32_t AuraProjectionManager::findClosestZoneCenter(const Position& pos) const {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    uint32_t closestZone = myZoneId_;
    float closestDistSq = std::numeric_limits<float>::max();
    
    // Check distance to my center
    float myDx = x - myZoneDef_.centerX;
    float myDz = z - myZoneDef_.centerZ;
    closestDistSq = myDx * myDx + myDz * myDz;
    
    // Check distance to adjacent zone centers
    for (const auto& adj : adjacentZones_) {
        float dx = x - adj.centerX;
        float dz = z - adj.centerZ;
        float distSq = dx * dx + dz * dz;
        
        if (distSq < closestDistSq) {
            closestDistSq = distSq;
            closestZone = adj.zoneId;
        }
    }
    
    return closestZone;
}

} // namespace DarkAges
