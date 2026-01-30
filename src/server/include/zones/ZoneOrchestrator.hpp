#pragma once
#include "zones/ZoneDefinition.hpp"
#include "db/RedisManager.hpp"
#include <memory>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <vector>

namespace DarkAges {

// Zone instance state (running or standby)
enum class ZoneState {
    OFFLINE,      // Not running
    STARTING,     // Booting up
    ONLINE,       // Running and accepting players
    FULL,         // At capacity
    SHUTTING_DOWN // Draining players before shutdown
};

struct ZoneInstance {
    ZoneDefinition definition;
    ZoneState state{ZoneState::OFFLINE};
    uint32_t playerCount{0};
    uint32_t maxPlayers{400};
    uint32_t processId{0};  // OS process ID
    uint64_t lastHeartbeat{0};
    
    // Quick check if zone can accept more players
    bool hasCapacity() const {
        return state == ZoneState::ONLINE && playerCount < maxPlayers;
    }
};

// [ZONE_AGENT] Central zone orchestrator
// Manages multiple zone processes, assigns players to zones
class ZoneOrchestrator {
public:
    using PlayerZoneAssignment = std::pair<uint64_t, uint32_t>;  // playerId -> zoneId
    
public:
    ZoneOrchestrator();
    ~ZoneOrchestrator();
    
    // Initialize with world partition
    bool initialize(const std::vector<ZoneDefinition>& zones, 
                   RedisManager* redis);
    
    // Shutdown all zones
    void shutdown();
    
    // Start a specific zone (spawns new process)
    bool startZone(uint32_t zoneId);
    
    // Gracefully shutdown a zone
    bool shutdownZone(uint32_t zoneId);
    
    // Find best zone for a player based on position
    uint32_t assignPlayerToZone(uint64_t playerId, float x, float z);
    
    // Get zone assignment for a player
    uint32_t getPlayerZone(uint64_t playerId) const;
    
    // Handle player disconnect (remove from assignment)
    void removePlayer(uint64_t playerId);
    
    // Check if player needs to migrate (near zone boundary)
    bool shouldMigratePlayer(uint64_t playerId, float x, float z, 
                            uint32_t& outTargetZone);
    
    // Update zone status from Redis (called periodically)
    void updateZoneStatus();
    
    // Get all online zones
    std::vector<uint32_t> getOnlineZones() const;
    
    // Get zone info
    const ZoneInstance* getZone(uint32_t zoneId) const;
    
    // Get total player count across all zones
    uint32_t getTotalPlayerCount() const;
    
    // Set callbacks
    void setOnZoneStarted(std::function<void(uint32_t)> callback) { onZoneStarted_ = callback; }
    void setOnZoneShutdown(std::function<void(uint32_t)> callback) { onZoneShutdown_ = callback; }
    void setOnPlayerMigration(std::function<void(uint64_t, uint32_t, uint32_t)> callback) {
        onPlayerMigration_ = callback;
    }

private:
    RedisManager* redis_{nullptr};
    std::vector<ZoneDefinition> zoneDefinitions_;
    std::unordered_map<uint32_t, ZoneInstance> zones_;
    std::unordered_map<uint64_t, uint32_t> playerAssignments_;  // playerId -> zoneId
    
    // Callbacks
    std::function<void(uint32_t)> onZoneStarted_;
    std::function<void(uint32_t)> onZoneShutdown_;
    std::function<void(uint64_t, uint32_t, uint32_t)> onPlayerMigration_;  // player, from, to
    
    // Spawn a zone server process
    bool spawnZoneProcess(uint32_t zoneId);
    
    // Publish zone state to Redis
    void publishZoneState(uint32_t zoneId);
    
    // Load zone state from Redis
    void loadZoneStateFromRedis();
};

} // namespace DarkAges
