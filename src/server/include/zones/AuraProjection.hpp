#pragma once
#include "ecs/CoreTypes.hpp"
#include "netcode/NetworkManager.hpp"
#include "zones/ZoneDefinition.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <glm/glm.hpp>

namespace DarkAges {

// [ZONE_AGENT] Entity state in aura buffer
struct AuraEntityState {
    EntityID entity;
    uint32_t ownerZoneId;      // Which zone "owns" this entity
    Position lastKnownPosition;
    Velocity lastKnownVelocity;
    uint32_t lastUpdateTick;
    bool isGhost;              // True if this is a ghost copy from another zone
};

// [ZONE_AGENT] Aura projection management
// Maintains entity state in the 50m buffer zone for seamless handoffs
class AuraProjectionManager {
public:
    AuraProjectionManager(uint32_t myZoneId);
    
    // Initialize with adjacent zones
    void initialize(const std::vector<ZoneDefinition>& adjacentZones);
    
    // Check if position is in aura buffer (overlap region)
    bool isInAuraBuffer(const Position& pos) const;
    
    // Check if position is in core zone (not aura)
    bool isInCoreZone(const Position& pos) const;
    
    // Register an entity entering aura from core
    void onEntityEnteringAura(EntityID entity, const Position& pos, 
                             uint32_t fromZoneId);
    
    // Register an entity leaving aura to core of another zone
    void onEntityLeavingAura(EntityID entity, uint32_t toZoneId);
    
    // Update entity state in aura (called every tick)
    void updateEntityState(EntityID entity, const Position& pos, 
                          const Velocity& vel, uint32_t tick);
    
    // Receive entity state from adjacent zone (via Redis)
    void onEntityStateFromAdjacentZone(uint32_t zoneId, EntityID entity,
                                      const Position& pos, const Velocity& vel);
    
    // Get all entities in aura that should be visible to a player
    std::vector<EntityID> getEntitiesInAuraForPlayer(const Position& playerPos,
                                                     float viewRadius) const;
    
    // Check if entity is in this zone's aura
    bool isEntityInAura(EntityID entity) const;
    
    // Get the owning zone for an entity in aura
    uint32_t getEntityOwnerZone(EntityID entity) const;
    
    // Decide if this zone should take ownership of an entity
    bool shouldTakeOwnership(EntityID entity, const Position& pos) const;
    
    // Handle ownership transfer from another zone
    void onOwnershipTransferred(EntityID entity, uint32_t fromZoneId);
    
    // Get list of entities that need to be sent to adjacent zones
    void getEntitiesToSync(std::vector<AuraEntityState>& outEntities) const;
    
    // Remove entity from aura tracking
    void removeEntity(EntityID entity);
    
    // Get distance to closest zone edge
    float getDistanceToZoneEdge(const Position& pos) const;
    
    // Get direction to zone center (for handoff hints)
    glm::vec2 getDirectionToZoneCenter(const Position& pos) const;

private:
    uint32_t myZoneId_;
    ZoneDefinition myZoneDef_;
    std::vector<ZoneDefinition> adjacentZones_;
    
    // Entities in aura buffer
    std::unordered_map<EntityID, AuraEntityState> auraEntities_;
    
    // Track which zone owns each entity
    std::unordered_map<EntityID, uint32_t> entityOwners_;
    
    // Distance threshold for ownership transfer
    static constexpr float OWNERSHIP_TRANSFER_THRESHOLD = 25.0f;  // meters
    
    bool isPositionInMyAura(const Position& pos) const;
    bool isPositionInMyCore(const Position& pos) const;
    uint32_t findClosestZoneCenter(const Position& pos) const;
};

} // namespace DarkAges
