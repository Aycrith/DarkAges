// [ZONE_AGENT] Spatial replication optimizer implementation
// Implements distance-based culling and variable update rates

#include "zones/ReplicationOptimizer.hpp"
#include "Constants.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DarkAges {

ReplicationOptimizer::ReplicationOptimizer(const Config& config) 
    : config_(config) {
}

ReplicationOptimizer::ReplicationOptimizer() 
    : config_(Config{}) {
}

std::vector<ReplicationPriority> ReplicationOptimizer::calculatePriorities(
    Registry& registry,
    const SpatialHash& spatialHash,
    EntityID viewerEntity,
    const Position& viewerPos,
    uint32_t /*currentTick*/) {
    
    std::vector<ReplicationPriority> priorities;
    priorities.reserve(64);  // Pre-allocate to avoid reallocations
    
    // Convert viewer position to float for distance calculations
    float viewerX = viewerPos.x * Constants::FIXED_TO_FLOAT;
    float viewerZ = viewerPos.z * Constants::FIXED_TO_FLOAT;
    
    // Query all entities within far radius using spatial hash
    // This is O(1) with spatial hash vs O(n) with naive iteration
    auto nearby = spatialHash.query(viewerX, viewerZ, config_.farRadius);
    
    for (EntityID entity : nearby) {
        // Skip the viewer's own entity
        if (entity == viewerEntity) continue;
        
        // Check if entity has a position component
        const Position* pos = registry.try_get<Position>(entity);
        if (!pos) continue;
        
        // Calculate distance (convert from fixed-point to float)
        float dx = (pos->x - viewerPos.x) * Constants::FIXED_TO_FLOAT;
        float dz = (pos->z - viewerPos.z) * Constants::FIXED_TO_FLOAT;
        float distSq = dx * dx + dz * dz;
        float dist = std::sqrt(distSq);
        
        // Skip entities beyond far radius (spatial hash query may return some extra)
        if (dist > config_.farRadius) continue;
        
        ReplicationPriority rp;
        rp.entity = entity;
        rp.priority = calculatePriority(dist);
        rp.distanceSq = distSq;
        
        // Check if this is a new entity that hasn't been replicated yet
        // New entities get a full update with all fields
        rp.forceFullUpdate = true;  // Will be checked against per-client tracking
        
        priorities.push_back(rp);
    }
    
    // Sort by priority (lower number = higher priority), then by distance
    // This ensures closest entities get priority for bandwidth allocation
    std::sort(priorities.begin(), priorities.end(),
        [](const ReplicationPriority& a, const ReplicationPriority& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.distanceSq < b.distanceSq;
        });
    
    // Apply max entities limit to protect bandwidth
    if (priorities.size() > config_.maxEntitiesPerSnapshot) {
        priorities.resize(config_.maxEntitiesPerSnapshot);
    }
    
    return priorities;
}

int ReplicationOptimizer::calculatePriority(float distance) const {
    if (distance <= config_.nearRadius) return 0;  // High priority - near
    if (distance <= config_.midRadius) return 1;   // Medium priority - mid
    return 2;                                       // Low priority - far
}

uint32_t ReplicationOptimizer::getUpdateInterval(int priority) const {
    // Calculate how many ticks between updates for each priority level
    // TICK_RATE_HZ = 60, so intervals are:
    // Priority 0 (near): 60/20 = 3 ticks -> 20Hz
    // Priority 1 (mid): 60/10 = 6 ticks -> 10Hz
    // Priority 2 (far): 60/5 = 12 ticks -> 5Hz
    switch (priority) {
        case 0: return Constants::TICK_RATE_HZ / config_.nearRate;
        case 1: return Constants::TICK_RATE_HZ / config_.midRate;
        case 2: return Constants::TICK_RATE_HZ / config_.farRate;
        default: return Constants::TICK_RATE_HZ / config_.farRate;
    }
}

std::vector<EntityID> ReplicationOptimizer::filterByUpdateRate(
    const std::vector<ReplicationPriority>& priorities,
    uint32_t currentTick) {
    
    std::vector<EntityID> toUpdate;
    toUpdate.reserve(priorities.size());
    
    for (const auto& rp : priorities) {
        uint32_t updateInterval = getUpdateInterval(rp.priority);
        
        // Check if it's time to update this priority level
        // currentTick % updateInterval == 0 ensures entities are updated
        // at their designated rate (e.g., every 3 ticks for 20Hz)
        if (currentTick % updateInterval == 0) {
            toUpdate.push_back(rp.entity);
        }
    }
    
    return toUpdate;
}

std::vector<Protocol::EntityStateData> ReplicationOptimizer::buildEntityStates(
    Registry& registry,
    const std::vector<EntityID>& entities,
    uint32_t currentTick) {
    
    std::vector<Protocol::EntityStateData> states;
    states.reserve(entities.size());
    
    for (EntityID entity : entities) {
        Protocol::EntityStateData data{};
        data.entity = entity;
        
        // Get position (required for all entities)
        if (const Position* pos = registry.try_get<Position>(entity)) {
            data.position = *pos;
        }
        
        // Get velocity
        if (const Velocity* vel = registry.try_get<Velocity>(entity)) {
            data.velocity = *vel;
        }
        
        // Get rotation
        if (const Rotation* rot = registry.try_get<Rotation>(entity)) {
            data.rotation = *rot;
        }
        
        // Get health percentage from combat state
        if (const CombatState* combat = registry.try_get<CombatState>(entity)) {
            data.healthPercent = static_cast<uint8_t>(combat->healthPercent());
        } else {
            data.healthPercent = 100;  // Default to full health
        }
        
        // Animation state - placeholder (would come from animation system)
        data.animState = 0;  // ANIM_IDLE
        
        // Determine entity type for client-side handling
        if (registry.all_of<PlayerTag>(entity)) {
            data.entityType = 0;  // Player
        } else if (registry.all_of<ProjectileTag>(entity)) {
            data.entityType = 1;  // Projectile
        } else if (registry.all_of<NPCTag>(entity)) {
            data.entityType = 3;  // NPC
        } else {
            data.entityType = 2;  // Loot/other
        }
        
        // Timestamp is implicit in the snapshot tick
        states.push_back(data);
    }
    
    return states;
}

void ReplicationOptimizer::cullNonEssentialFields(Protocol::EntityStateData& data, int priority) {
    // Apply data reduction based on priority level to save bandwidth:
    // - Low priority entities (far): position only, no velocity/rotation/anim
    // - Medium priority entities (mid): everything except animation state
    // - High priority entities (near): full data
    
    if (priority >= 2) {
        // Far entities - position only
        data.velocity = Velocity{};
        data.rotation = Rotation{};
        data.animState = 0;
        // Keep health for damage indication at distance
    } else if (priority >= 1) {
        // Medium entities - no animation state
        data.animState = 0;
    }
    // Near entities (priority 0) get full update - no culling
}

void ReplicationOptimizer::markEntityUpdated(ConnectionID connId, EntityID entity, uint32_t tick) {
    lastUpdateTick_[connId][entity] = tick;
}

bool ReplicationOptimizer::needsUpdate(ConnectionID connId, EntityID entity, 
                                       uint32_t currentTick, int priority) const {
    auto connIt = lastUpdateTick_.find(connId);
    if (connIt == lastUpdateTick_.end()) {
        return true;  // New client - needs all updates
    }
    
    auto entityIt = connIt->second.find(entity);
    if (entityIt == connIt->second.end()) {
        return true;  // New entity for this client - needs update
    }
    
    uint32_t lastTick = entityIt->second;
    uint32_t ticksPerUpdate = getUpdateInterval(priority);
    
    // Check if enough ticks have passed since last update
    return (currentTick - lastTick) >= ticksPerUpdate;
}

void ReplicationOptimizer::removeClientTracking(ConnectionID connId) {
    lastUpdateTick_.erase(connId);
}

void ReplicationOptimizer::removeEntityTracking(EntityID entity) {
    // Remove this entity from all clients' tracking maps
    for (auto& [connId, entityMap] : lastUpdateTick_) {
        entityMap.erase(entity);
    }
}

} // namespace DarkAges
