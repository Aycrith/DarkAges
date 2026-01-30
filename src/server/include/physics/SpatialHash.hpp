#pragma once

#include "ecs/CoreTypes.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <span>
#include <cstddef>

// [PHYSICS_AGENT] Spatial Hash for O(1) nearby entity queries
// Used for broad-phase collision detection and AOI management

namespace DarkAges {

class SpatialHash {
public:
    static constexpr float DEFAULT_CELL_SIZE = Constants::SPATIAL_HASH_CELL_SIZE;
    
    // Hash key for 2D grid cell
    struct CellCoord {
        int32_t x{0};
        int32_t z{0};
        
        bool operator==(const CellCoord& other) const {
            return x == other.x && z == other.z;
        }
    };
    
    // Custom hash function for CellCoord
    struct CellHash {
        size_t operator()(const CellCoord& c) const {
            // Combine x and z into single 64-bit hash
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) |
                           static_cast<uint32_t>(c.z);
            // FNV-1a inspired hash
            return std::hash<uint64_t>{}(key);
        }
    };
    
    // Cell contents
    struct Cell {
        std::vector<EntityID> entities;
        // Pre-allocated buffer to avoid reallocations during tick
        static constexpr size_t PREALLOCATED_SIZE = 16;
        
        Cell() {
            entities.reserve(PREALLOCATED_SIZE);
        }
        
        void clear() {
            entities.clear();
            // Keep capacity to avoid reallocation
        }
    };

private:
    float cellSize_{DEFAULT_CELL_SIZE};
    float invCellSize_{1.0f / DEFAULT_CELL_SIZE};
    
    // Main grid storage
    std::unordered_map<CellCoord, Cell, CellHash> grid_;
    
    // Reusable query result buffer (avoids allocation during query)
    mutable std::vector<EntityID> queryBuffer_;
    static constexpr size_t QUERY_BUFFER_RESERVE = 256;

public:
    SpatialHash();
    explicit SpatialHash(float cellSize);
    
    // Clear all cells (called at start of each frame)
    void clear();
    
    // Insert entity into appropriate cell
    void insert(EntityID entity, float x, float z);
    void insert(EntityID entity, const Position& pos);
    
    // Query all entities within radius of point
    // Returns span into internal buffer - copy if needed across multiple calls
    [[nodiscard]] std::span<const EntityID> query(float x, float z, float radius) const;
    [[nodiscard]] std::span<const EntityID> query(const Position& pos, float radius) const;
    
    // Query adjacent cells (for collision detection)
    // Returns entities in same cell and 8 neighbors
    [[nodiscard]] std::span<const EntityID> queryAdjacent(float x, float z) const;
    
    // Get cell coordinate for position
    [[nodiscard]] CellCoord getCellCoord(float x, float z) const;
    
    // Get cell at coordinate (creates if doesn't exist)
    [[nodiscard]] Cell& getCell(const CellCoord& coord);
    [[nodiscard]] const Cell* getCell(const CellCoord& coord) const;
    
    // Remove entity (called when entity destroyed or moved)
    void remove(EntityID entity);
    
    // Update entity position (remove from old cell, insert to new)
    void update(EntityID entity, float oldX, float oldZ, float newX, float newZ);
    
    // Statistics
    [[nodiscard]] size_t getCellCount() const { return grid_.size(); }
    [[nodiscard]] size_t getTotalEntityCount() const;
    [[nodiscard]] float getAverageEntitiesPerCell() const;
    
    // Performance budget check
    [[nodiscard]] bool isWithinBudget() const {
        return getTotalEntityCount() <= Constants::MAX_ENTITIES_PER_ZONE;
    }
};

// [PHYSICS_AGENT] Broad-phase collision system
class BroadPhaseSystem {
public:
    // Update spatial hash from all entities with Position
    void update(Registry& registry, SpatialHash& spatialHash);
    
    // Get potential collision pairs
    // Callback is invoked for each potential pair (both entities)
    void findPotentialPairs(Registry& registry, const SpatialHash& spatialHash,
                           std::vector<std::pair<EntityID, EntityID>>& outPairs);
    
    // Check if two entities can collide (layer masks, teams, etc.)
    [[nodiscard]] bool canCollide(Registry& registry, EntityID a, EntityID b) const;
};

} // namespace DarkAges
