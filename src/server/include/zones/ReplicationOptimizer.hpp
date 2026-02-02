#pragma once

#include "ecs/CoreTypes.hpp"
#include "physics/SpatialHash.hpp"
#include "netcode/NetworkManager.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

// [ZONE_AGENT] Spatial replication optimizer for DarkAges MMO
// Optimizes replication using spatial partitioning - sends only relevant 
// entity updates to each player based on proximity, with distance-based 
// update rate reduction.
//
// Performance Budgets:
// - Near entities (0-50m): Update at 20Hz, full data
// - Mid entities (50-100m): Update at 10Hz, no anim state
// - Far entities (100-200m): Update at 5Hz, position only
// - Beyond 200m: Not replicated

namespace DarkAges {

// Forward declarations
class SpatialHash;

// Per-entity replication priority
struct ReplicationPriority {
    EntityID entity;
    int priority;           // 0=high (20Hz), 1=med (10Hz), 2=low (5Hz)
    float distanceSq;       // For sorting within priority
    bool forceFullUpdate;   // New entity - send all fields
};

// [ZONE_AGENT] Replication Optimizer class
class ReplicationOptimizer {
public:
    // Configuration for priority zones and update rates
    struct Config {
        float nearRadius = Constants::AOI_RADIUS_NEAR;    // 50 meters
        float midRadius = Constants::AOI_RADIUS_MID;      // 100 meters
        float farRadius = Constants::AOI_RADIUS_FAR;      // 200 meters
        
        uint32_t nearRate = 20;        // Hz - full rate
        uint32_t midRate = 10;         // Hz - half rate
        uint32_t farRate = 5;          // Hz - quarter rate
        
        // Maximum entities to replicate per player per tick (bandwidth protection)
        size_t maxEntitiesPerSnapshot = 100;
        
        Config() = default;
    };
    
    explicit ReplicationOptimizer(const Config& config);
    ReplicationOptimizer();
    
    // Calculate which entities should be in snapshot based on viewer position
    // Returns prioritized list of entities sorted by importance
    std::vector<ReplicationPriority> calculatePriorities(
        Registry& registry,
        const SpatialHash& spatialHash,
        EntityID viewerEntity,
        const Position& viewerPos,
        uint32_t currentTick
    );
    
    // Filter entities by update rate based on their priority (call each tick)
    // Returns only entities that should be updated this tick based on their rate
    std::vector<EntityID> filterByUpdateRate(
        const std::vector<ReplicationPriority>& priorities,
        uint32_t currentTick
    );
    
    // Build entity state data for replication
    // Only includes entities that passed the filterByUpdateRate check
    std::vector<Protocol::EntityStateData> buildEntityStates(
        Registry& registry,
        const std::vector<EntityID>& entities,
        uint32_t currentTick
    );
    
    // Apply culling to reduce data for low-priority entities
    // Modifies the state data in-place based on priority level
    static void cullNonEssentialFields(Protocol::EntityStateData& data, int priority);
    
    // Track last update tick per entity per player (for delta-updates)
    void markEntityUpdated(ConnectionID connId, EntityID entity, uint32_t tick);
    
    // Check if entity needs update based on last update time and priority
    [[nodiscard]] bool needsUpdate(ConnectionID connId, EntityID entity, 
                                   uint32_t currentTick, int priority) const;
    
    // Remove tracking data for disconnected client
    void removeClientTracking(ConnectionID connId);
    
    // Remove tracking data for destroyed entity
    void removeEntityTracking(EntityID entity);
    
    // Getters for metrics
    [[nodiscard]] size_t getTrackedClientCount() const { return lastUpdateTick_.size(); }
    [[nodiscard]] const Config& getConfig() const { return config_; }
    
    // Update configuration at runtime (for QoS degradation)
    void setConfig(const Config& config) { config_ = config; }

    // Calculate priority level (0, 1, or 2) based on distance
    [[nodiscard]] int calculatePriority(float distance) const;

private:
    
    // Get tick interval between updates for a given priority level
    [[nodiscard]] uint32_t getUpdateInterval(int priority) const;
    
    Config config_;
    
    // Track last update tick: {connId -> {entity -> tick}}
    // Used to determine if an entity needs to be updated this tick
    std::unordered_map<ConnectionID, std::unordered_map<EntityID, uint32_t>> lastUpdateTick_;
};

} // namespace DarkAges
