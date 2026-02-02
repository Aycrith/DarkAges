// [ZONE_AGENT] Zone orchestration and multi-zone management
// Handles zone startup/shutdown, player assignment, and migration detection

#include "zones/ZoneOrchestrator.hpp"
#include "Constants.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DarkAges {

// ============================================================================
// ZoneDefinition Implementation
// ============================================================================

bool ZoneDefinition::containsPosition(float x, float z) const {
    // Exclude aura buffer from main zone
    // Core region is the natural zone bounds (shrunk by buffer on interior edges)
    // For zones with buffer, we shrink by buffer amount
    // For zones at world edges, they don't extend beyond world bounds
    // We use < instead of <= to avoid overlap at exact boundaries
    float buffer = Constants::AURA_BUFFER_METERS;
    
    // Check if this zone has buffer on each edge by checking if min/max are "round" numbers
    // Zones at world edges have natural bounds, interior zones have buffer
    // Simple heuristic: if maxX-minX is larger than expected, we have buffer on edges
    
    // For now, use simple logic: shrink by buffer, use strict < for upper bounds
    float coreMinX = minX + buffer;
    float coreMaxX = maxX - buffer;
    float coreMinZ = minZ + buffer;
    float coreMaxZ = maxZ - buffer;
    
    return x > coreMinX && x < coreMaxX &&
           z > coreMinZ && z < coreMaxZ;
}

bool ZoneDefinition::isInAuraBuffer(float x, float z) const {
    float buffer = Constants::AURA_BUFFER_METERS;
    
    // In main bounds but outside core
    bool inMainBounds = x >= minX && x <= maxX && z >= minZ && z <= maxZ;
    bool inCore = x >= (minX + buffer) && x <= (maxX - buffer) &&
                  z >= (minZ + buffer) && z <= (maxZ - buffer);
    
    return inMainBounds && !inCore;
}

float ZoneDefinition::distanceToEdge(float x, float z) const {
    // Check if point is inside the zone
    bool insideX = (x >= minX && x <= maxX);
    bool insideZ = (z >= minZ && z <= maxZ);
    
    if (insideX && insideZ) {
        // Inside - return negative distance to nearest edge
        float distToMinX = x - minX;
        float distToMaxX = maxX - x;
        float distToMinZ = z - minZ;
        float distToMaxZ = maxZ - z;
        
        float minDistX = std::min(distToMinX, distToMaxX);
        float minDistZ = std::min(distToMinZ, distToMaxZ);
        float minDist = std::min(minDistX, minDistZ);
        
        return -minDist;  // Negative when inside
    }
    
    // Outside - return positive distance to nearest edge
    float dx = 0.0f;
    if (x < minX) dx = minX - x;
    else if (x > maxX) dx = x - maxX;
    
    float dz = 0.0f;
    if (z < minZ) dz = minZ - z;
    else if (z > maxZ) dz = z - maxZ;
    
    return std::sqrt(dx * dx + dz * dz);
}

bool ZoneDefinition::calculateOverlap(const ZoneDefinition& other,
                                     float& outMinX, float& outMaxX,
                                     float& outMinZ, float& outMaxZ) const {
    // Calculate intersection of two zones
    outMinX = std::max(minX, other.minX);
    outMaxX = std::min(maxX, other.maxX);
    outMinZ = std::max(minZ, other.minZ);
    outMaxZ = std::min(maxZ, other.maxZ);
    
    // Check if there is actual overlap
    return outMinX < outMaxX && outMinZ < outMaxZ;
}

// ============================================================================
// WorldPartition Implementation
// ============================================================================

std::vector<ZoneDefinition> WorldPartition::createGrid(
    uint32_t zonesX, uint32_t zonesZ,
    float worldMinX, float worldMaxX,
    float worldMinZ, float worldMaxZ) {
    
    std::vector<ZoneDefinition> zones;
    zones.reserve(zonesX * zonesZ);
    
    float zoneWidth = (worldMaxX - worldMinX) / zonesX;
    float zoneHeight = (worldMaxZ - worldMinZ) / zonesZ;
    
    // Include aura buffer in zone size (zones overlap)
    float buffer = Constants::AURA_BUFFER_METERS;
    
    for (uint32_t z = 0; z < zonesZ; z++) {
        for (uint32_t x = 0; x < zonesX; x++) {
            ZoneDefinition def;
            def.zoneId = z * zonesX + x + 1;  // 1-based IDs
            def.zoneName = "Zone_" + std::to_string(def.zoneId);
            def.shape = ZoneShape::RECTANGLE;
            
            // Core bounds (with buffer overlap on shared edges)
            // Only add buffer to interior edges, not world boundaries
            def.minX = worldMinX + x * zoneWidth - (x > 0 ? buffer : 0);
            def.maxX = worldMinX + (x + 1) * zoneWidth + (x < zonesX - 1 ? buffer : 0);
            def.minZ = worldMinZ + z * zoneHeight - (z > 0 ? buffer : 0);
            def.maxZ = worldMinZ + (z + 1) * zoneHeight + (z < zonesZ - 1 ? buffer : 0);
            
            def.minY = Constants::WORLD_MIN_Y;
            def.maxY = Constants::WORLD_MAX_Y;
            
            def.centerX = (def.minX + def.maxX) / 2.0f;
            def.centerZ = (def.minZ + def.maxZ) / 2.0f;
            
            def.host = "localhost";
            def.port = Constants::DEFAULT_SERVER_PORT + def.zoneId;
            
            // Add adjacent zones (4-connectivity)
            if (x > 0) def.adjacentZones.push_back(def.zoneId - 1);           // Left
            if (x < zonesX - 1) def.adjacentZones.push_back(def.zoneId + 1);  // Right
            if (z > 0) def.adjacentZones.push_back(def.zoneId - zonesX);      // Bottom
            if (z < zonesZ - 1) def.adjacentZones.push_back(def.zoneId + zonesX); // Top
            
            zones.push_back(def);
        }
    }
    
    return zones;
}

std::vector<ZoneDefinition> WorldPartition::createHexagonalGrid(
    uint32_t rings,
    float zoneRadius,
    float centerX, float centerZ) {
    
    std::vector<ZoneDefinition> zones;
    
    // Hex grid using axial coordinates
    // Center zone is at (0, 0)
    float hexWidth = zoneRadius * std::sqrt(3.0f);
    float hexHeight = zoneRadius * 2.0f;
    
    auto addZone = [&](int q, int r) {
        ZoneDefinition def;
        def.zoneId = static_cast<uint32_t>(zones.size() + 1);
        def.zoneName = "Zone_" + std::to_string(def.zoneId);
        def.shape = ZoneShape::HEXAGON;
        
        // Convert axial to world coordinates
        float worldX = centerX + hexWidth * (q + r / 2.0f);
        float worldZ = centerZ + hexHeight * 0.75f * r;
        
        // Bounding box for hexagon
        def.minX = worldX - hexWidth / 2;
        def.maxX = worldX + hexWidth / 2;
        def.minZ = worldZ - zoneRadius;
        def.maxZ = worldZ + zoneRadius;
        def.minY = Constants::WORLD_MIN_Y;
        def.maxY = Constants::WORLD_MAX_Y;
        
        def.centerX = worldX;
        def.centerZ = worldZ;
        
        def.host = "localhost";
        def.port = Constants::DEFAULT_SERVER_PORT + def.zoneId;
        
        zones.push_back(def);
    };
    
    // Add center
    addZone(0, 0);
    
    // Add rings
    for (uint32_t ring = 1; ring <= rings; ring++) {
        int q = -static_cast<int>(ring);
        int r = static_cast<int>(ring);
        
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < static_cast<int>(ring); j++) {
                addZone(q, r);
                
                // Move in one of 6 directions
                switch (i) {
                    case 0: q++; r--; break;  // Up-right
                    case 1: q++;       break;  // Right
                    case 2: r--;       break;  // Down-right
                    case 3: q--; r++; break;  // Down-left
                    case 4: q--;       break;  // Left
                    case 5: r++;       break;  // Up-left
                }
            }
        }
    }
    
    // Calculate adjacencies for hex grid
    // In hex grid, each zone has up to 6 neighbors
    for (size_t i = 0; i < zones.size(); i++) {
        for (size_t j = i + 1; j < zones.size(); j++) {
            float dist = std::sqrt(
                std::pow(zones[i].centerX - zones[j].centerX, 2) +
                std::pow(zones[i].centerZ - zones[j].centerZ, 2)
            );
            
            // If centers are close, they're adjacent
            if (dist < hexWidth * 1.1f) {
                zones[i].adjacentZones.push_back(zones[j].zoneId);
                zones[j].adjacentZones.push_back(zones[i].zoneId);
            }
        }
    }
    
    return zones;
}

ZoneDefinition* WorldPartition::findZoneForPosition(
    std::vector<ZoneDefinition>& zones, float x, float z) {
    
    for (auto& zone : zones) {
        if (zone.containsPosition(x, z)) {
            return &zone;
        }
    }
    return nullptr;
}

std::vector<uint32_t> WorldPartition::findZonesWithAuraOverlap(
    const std::vector<ZoneDefinition>& zones, float x, float z) {
    
    std::vector<uint32_t> result;
    
    for (const auto& zone : zones) {
        // Check if in main bounds (including buffer)
        if (x >= zone.minX && x <= zone.maxX && 
            z >= zone.minZ && z <= zone.maxZ) {
            result.push_back(zone.zoneId);
        }
    }
    
    return result;
}

// ============================================================================
// ZoneOrchestrator Implementation
// ============================================================================

ZoneOrchestrator::ZoneOrchestrator() = default;

ZoneOrchestrator::~ZoneOrchestrator() {
    shutdown();
}

bool ZoneOrchestrator::initialize(const std::vector<ZoneDefinition>& zones,
                                 RedisManager* redis) {
    redis_ = redis;
    zoneDefinitions_ = zones;
    
    std::cout << "[ORCHESTRATOR] Initializing with " << zones.size() << " zones" << std::endl;
    
    // Initialize zone instances
    for (const auto& def : zones) {
        ZoneInstance instance;
        instance.definition = def;
        instance.state = ZoneState::OFFLINE;
        instance.maxPlayers = Constants::MAX_PLAYERS_PER_ZONE;
        zones_[def.zoneId] = std::move(instance);
    }
    
    // Load current state from Redis
    if (redis_ && redis_->isConnected()) {
        loadZoneStateFromRedis();
    }
    
    return true;
}

void ZoneOrchestrator::shutdown() {
    std::cout << "[ORCHESTRATOR] Shutting down all zones..." << std::endl;
    
    for (auto& [zoneId, instance] : zones_) {
        if (instance.state == ZoneState::ONLINE || 
            instance.state == ZoneState::FULL) {
            shutdownZone(zoneId);
        }
    }
    
    zones_.clear();
    playerAssignments_.clear();
}

bool ZoneOrchestrator::startZone(uint32_t zoneId) {
    auto it = zones_.find(zoneId);
    if (it == zones_.end()) {
        std::cerr << "[ORCHESTRATOR] Unknown zone: " << zoneId << std::endl;
        return false;
    }
    
    auto& instance = it->second;
    
    if (instance.state != ZoneState::OFFLINE) {
        std::cout << "[ORCHESTRATOR] Zone " << zoneId << " already running" << std::endl;
        return true;
    }
    
    std::cout << "[ORCHESTRATOR] Starting zone " << zoneId << std::endl;
    
    instance.state = ZoneState::STARTING;
    
    // Spawn process
    if (!spawnZoneProcess(zoneId)) {
        instance.state = ZoneState::OFFLINE;
        return false;
    }
    
    // Wait for process to start (in real impl, wait for health check)
    instance.state = ZoneState::ONLINE;
    instance.lastHeartbeat = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    
    publishZoneState(zoneId);
    
    if (onZoneStarted_) {
        onZoneStarted_(zoneId);
    }
    
    return true;
}

bool ZoneOrchestrator::shutdownZone(uint32_t zoneId) {
    auto it = zones_.find(zoneId);
    if (it == zones_.end()) return false;
    
    auto& instance = it->second;
    
    if (instance.state == ZoneState::OFFLINE) return true;
    
    std::cout << "[ORCHESTRATOR] Shutting down zone " << zoneId << std::endl;
    
    instance.state = ZoneState::SHUTTING_DOWN;
    
    // In real implementation, signal process to shutdown gracefully
    // and wait for all players to migrate
    
    instance.state = ZoneState::OFFLINE;
    instance.playerCount = 0;
    instance.processId = 0;
    
    publishZoneState(zoneId);
    
    if (onZoneShutdown_) {
        onZoneShutdown_(zoneId);
    }
    
    return true;
}

uint32_t ZoneOrchestrator::assignPlayerToZone(uint64_t playerId, float x, float z) {
    // Find which zone should contain this position
    ZoneDefinition* zone = WorldPartition::findZoneForPosition(zoneDefinitions_, x, z);
    
    if (!zone) {
        std::cerr << "[ORCHESTRATOR] No zone found for position (" << x << ", " << z << ")" << std::endl;
        return 0;
    }
    
    uint32_t zoneId = zone->zoneId;
    
    // Start zone if not running
    auto it = zones_.find(zoneId);
    if (it == zones_.end() || it->second.state == ZoneState::OFFLINE) {
        if (!startZone(zoneId)) {
            return 0;
        }
    }
    
    // Check capacity
    if (it->second.state == ZoneState::FULL) {
        // Find adjacent zone with capacity
        for (uint32_t adjacentId : zone->adjacentZones) {
            auto adjIt = zones_.find(adjacentId);
            if (adjIt != zones_.end() && adjIt->second.hasCapacity()) {
                zoneId = adjacentId;
                it = adjIt;
                break;
            }
        }
        
        if (it->second.state == ZoneState::FULL) {
            std::cerr << "[ORCHESTRATOR] All zones at capacity for player " << playerId << std::endl;
            return 0;
        }
    }
    
    // Assign player
    playerAssignments_[playerId] = zoneId;
    it->second.playerCount++;
    
    // Check if zone is now full
    if (it->second.playerCount >= it->second.maxPlayers) {
        it->second.state = ZoneState::FULL;
    }
    
    std::cout << "[ORCHESTRATOR] Assigned player " << playerId 
              << " to zone " << zoneId 
              << " (count: " << it->second.playerCount << ")" << std::endl;
    
    publishZoneState(zoneId);
    
    return zoneId;
}

uint32_t ZoneOrchestrator::getPlayerZone(uint64_t playerId) const {
    auto it = playerAssignments_.find(playerId);
    if (it != playerAssignments_.end()) {
        return it->second;
    }
    return 0;
}

void ZoneOrchestrator::removePlayer(uint64_t playerId) {
    auto it = playerAssignments_.find(playerId);
    if (it != playerAssignments_.end()) {
        uint32_t zoneId = it->second;
        auto zoneIt = zones_.find(zoneId);
        if (zoneIt != zones_.end()) {
            zoneIt->second.playerCount--;
            if (zoneIt->second.playerCount < zoneIt->second.maxPlayers && 
                zoneIt->second.state == ZoneState::FULL) {
                zoneIt->second.state = ZoneState::ONLINE;
            }
            publishZoneState(zoneId);
        }
        playerAssignments_.erase(it);
    }
}

bool ZoneOrchestrator::shouldMigratePlayer(uint64_t playerId, float x, float z,
                                          uint32_t& outTargetZone) {
    uint32_t currentZoneId = getPlayerZone(playerId);
    if (currentZoneId == 0) return false;
    
    auto currentZone = WorldPartition::findZoneForPosition(zoneDefinitions_, x, z);
    
    if (currentZone && currentZone->zoneId != currentZoneId) {
        // Player crossed into new zone
        outTargetZone = currentZone->zoneId;
        
        // Notify callbacks
        if (onPlayerMigration_) {
            onPlayerMigration_(playerId, currentZoneId, outTargetZone);
        }
        
        return true;
    }
    
    // Check if near boundary and should pre-migrate (for seamless handoff)
    auto it = zones_.find(currentZoneId);
    if (it != zones_.end()) {
        float dist = it->second.definition.distanceToEdge(x, z);
        // Negative distance means inside, positive means outside
        // Aura buffer is positive distance from edge going inward
        if (dist < 0 && std::abs(dist) < Constants::ZONE_HANDOFF_THRESHOLD) {
            // Player is near edge - determine migration direction based on movement
            // For now, find adjacent zone that contains the position
            for (uint32_t adjacentId : it->second.definition.adjacentZones) {
                auto adjDef = std::find_if(zoneDefinitions_.begin(), zoneDefinitions_.end(),
                    [adjacentId](const ZoneDefinition& def) { return def.zoneId == adjacentId; });
                
                if (adjDef != zoneDefinitions_.end()) {
                    // Check if position is in this adjacent zone's aura buffer
                    if (adjDef->isInAuraBuffer(x, z) || adjDef->containsPosition(x, z)) {
                        outTargetZone = adjacentId;
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

bool ZoneOrchestrator::spawnZoneProcess(uint32_t zoneId) {
    // In real implementation, spawn new process:
    // ./darkages-server --zone zoneId --port [port]
    
    std::cout << "[ORCHESTRATOR] Spawning zone process: " << zoneId << std::endl;
    
    // Simulate success
    return true;
}

void ZoneOrchestrator::publishZoneState(uint32_t zoneId) {
    if (!redis_ || !redis_->isConnected()) return;
    
    auto it = zones_.find(zoneId);
    if (it == zones_.end()) return;
    
    // Publish to Redis for other services to see
    // Format: zone:{id}:state -> JSON or binary representation
    std::string key = "zone:" + std::to_string(zoneId) + ":orchestrator";
    
    // Simple state encoding: state,playerCount,lastHeartbeat
    std::string value = std::to_string(static_cast<int>(it->second.state)) + "," +
                       std::to_string(it->second.playerCount) + "," +
                       std::to_string(it->second.lastHeartbeat);
    
    redis_->set(key, value, Constants::REDIS_KEY_TTL_SECONDS);
}

void ZoneOrchestrator::loadZoneStateFromRedis() {
    if (!redis_ || !redis_->isConnected()) return;
    
    // In a real implementation, this would query Redis for existing zone states
    // and synchronize with current orchestrator state
    std::cout << "[ORCHESTRATOR] Loading zone states from Redis" << std::endl;
}

void ZoneOrchestrator::updateZoneStatus() {
    // Poll Redis for zone heartbeats and update states
    if (!redis_ || !redis_->isConnected()) return;
    
    uint64_t now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    
    // Check for stale heartbeats
    for (auto& [zoneId, instance] : zones_) {
        if (instance.state == ZoneState::ONLINE || instance.state == ZoneState::FULL) {
            // 30 second timeout
            if (now - instance.lastHeartbeat > 30'000'000'000ULL) {
                std::cerr << "[ORCHESTRATOR] Zone " << zoneId << " heartbeat timeout!" << std::endl;
                instance.state = ZoneState::OFFLINE;
                publishZoneState(zoneId);
            }
        }
    }
}

std::vector<uint32_t> ZoneOrchestrator::getOnlineZones() const {
    std::vector<uint32_t> result;
    for (const auto& [zoneId, instance] : zones_) {
        if (instance.state == ZoneState::ONLINE || instance.state == ZoneState::FULL) {
            result.push_back(zoneId);
        }
    }
    return result;
}

const ZoneInstance* ZoneOrchestrator::getZone(uint32_t zoneId) const {
    auto it = zones_.find(zoneId);
    if (it != zones_.end()) {
        return &it->second;
    }
    return nullptr;
}

uint32_t ZoneOrchestrator::getTotalPlayerCount() const {
    uint32_t total = 0;
    for (const auto& [zoneId, instance] : zones_) {
        total += instance.playerCount;
    }
    return total;
}

} // namespace DarkAges
