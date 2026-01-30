#pragma once
#include "Constants.hpp"
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>

namespace DarkAges {

// Zone shape types
enum class ZoneShape {
    RECTANGLE,  // Axis-aligned bounding box
    HEXAGON,    // Hexagonal (optimal for sphere packing)
    CIRCLE      // Circular (simplest)
};

// Definition of a single zone
struct ZoneDefinition {
    uint32_t zoneId;
    std::string zoneName;
    ZoneShape shape;
    
    // World bounds
    float minX, maxX;
    float minY, maxY;
    float minZ, maxZ;
    
    // Center point
    float centerX, centerZ;
    
    // Adjacent zones (for handoff)
    std::vector<uint32_t> adjacentZones;
    
    // Network config
    std::string host;
    uint16_t port;
    
    // Check if position is within this zone (excluding buffer)
    bool containsPosition(float x, float z) const;
    
    // Check if position is within aura buffer (overlap region)
    bool isInAuraBuffer(float x, float z) const;
    
    // Get distance to zone edge (negative if inside)
    float distanceToEdge(float x, float z) const;
    
    // Calculate overlap region with another zone
    bool calculateOverlap(const ZoneDefinition& other, 
                         float& outMinX, float& outMaxX,
                         float& outMinZ, float& outMaxZ) const;
};

// World partition into zones
class WorldPartition {
public:
    // Create a grid of rectangular zones
    static std::vector<ZoneDefinition> createGrid(
        uint32_t zonesX, uint32_t zonesZ,
        float worldMinX, float worldMaxX,
        float worldMinZ, float worldMaxZ);
    
    // Create hexagonal tiling (optimal for MMO)
    static std::vector<ZoneDefinition> createHexagonalGrid(
        uint32_t rings,  // Number of rings around center
        float zoneRadius,
        float centerX, float centerZ);
    
    // Find which zone contains a position
    static ZoneDefinition* findZoneForPosition(
        std::vector<ZoneDefinition>& zones, float x, float z);
    
    // Find all zones that might see this position (including aura buffers)
    static std::vector<uint32_t> findZonesWithAuraOverlap(
        const std::vector<ZoneDefinition>& zones, float x, float z);
};

} // namespace DarkAges
