// [PHYSICS_AGENT] Spatial Hash implementation
// O(1) spatial queries for collision detection and AOI

#include "physics/SpatialHash.hpp"
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace DarkAges {

SpatialHash::SpatialHash() : SpatialHash(DEFAULT_CELL_SIZE) {}

SpatialHash::SpatialHash(float cellSize) 
    : cellSize_(cellSize)
    , invCellSize_(1.0f / cellSize) 
{
    queryBuffer_.reserve(QUERY_BUFFER_RESERVE);
}

void SpatialHash::clear() {
    // Clear entities from all cells but keep cell structure
    for (auto& [coord, cell] : grid_) {
        cell.clear();
    }
}

SpatialHash::CellCoord SpatialHash::getCellCoord(float x, float z) const {
    return {
        static_cast<int32_t>(std::floor(x * invCellSize_)),
        static_cast<int32_t>(std::floor(z * invCellSize_))
    };
}

SpatialHash::Cell& SpatialHash::getCell(const CellCoord& coord) {
    return grid_[coord];  // Creates if doesn't exist
}

const SpatialHash::Cell* SpatialHash::getCell(const CellCoord& coord) const {
    auto it = grid_.find(coord);
    if (it != grid_.end()) {
        return &it->second;
    }
    return nullptr;
}

void SpatialHash::insert(EntityID entity, float x, float z) {
    CellCoord coord = getCellCoord(x, z);
    Cell& cell = getCell(coord);
    
    // Check for duplicates (shouldn't happen in normal operation)
    auto it = std::find(cell.entities.begin(), cell.entities.end(), entity);
    if (it == cell.entities.end()) {
        cell.entities.push_back(entity);
    }
}

void SpatialHash::insert(EntityID entity, const Position& pos) {
    insert(entity, 
           pos.x * Constants::FIXED_TO_FLOAT,
           pos.z * Constants::FIXED_TO_FLOAT);
}

void SpatialHash::remove(EntityID entity) {
    // This is expensive - we have to search all cells
    // In practice, use update() instead which knows old position
    for (auto& [coord, cell] : grid_) {
        auto it = std::remove(cell.entities.begin(), cell.entities.end(), entity);
        if (it != cell.entities.end()) {
            cell.entities.erase(it, cell.entities.end());
            return;
        }
    }
}

void SpatialHash::update(EntityID entity, float oldX, float oldZ, float newX, float newZ) {
    CellCoord oldCoord = getCellCoord(oldX, oldZ);
    CellCoord newCoord = getCellCoord(newX, newZ);
    
    if (oldCoord == newCoord) {
        return;  // Still in same cell
    }
    
    // Remove from old cell
    auto oldIt = grid_.find(oldCoord);
    if (oldIt != grid_.end()) {
        auto& entities = oldIt->second.entities;
        auto entIt = std::remove(entities.begin(), entities.end(), entity);
        entities.erase(entIt, entities.end());
    }
    
    // Insert into new cell
    insert(entity, newX, newZ);
}

std::span<const EntityID> SpatialHash::query(float x, float z, float radius) const {
    queryBuffer_.clear();
    
    int32_t radiusCells = static_cast<int32_t>(std::ceil(radius * invCellSize_));
    CellCoord center = getCellCoord(x, z);
    
    // Iterate over all cells within radius
    for (int32_t dx = -radiusCells; dx <= radiusCells; ++dx) {
        for (int32_t dz = -radiusCells; dz <= radiusCells; ++dz) {
            CellCoord cellCoord{center.x + dx, center.z + dz};
            
            const Cell* cell = getCell(cellCoord);
            if (cell) {
                queryBuffer_.insert(queryBuffer_.end(), 
                                   cell->entities.begin(), 
                                   cell->entities.end());
            }
        }
    }
    
    return std::span<const EntityID>(queryBuffer_);
}

std::span<const EntityID> SpatialHash::query(const Position& pos, float radius) const {
    return query(pos.x * Constants::FIXED_TO_FLOAT,
                 pos.z * Constants::FIXED_TO_FLOAT,
                 radius);
}

std::span<const EntityID> SpatialHash::queryAdjacent(float x, float z) const {
    // Query 3x3 grid of cells (current + 8 neighbors)
    return query(x, z, cellSize_ * 1.5f);
}

size_t SpatialHash::getTotalEntityCount() const {
    size_t count = 0;
    for (const auto& [coord, cell] : grid_) {
        count += cell.entities.size();
    }
    return count;
}

float SpatialHash::getAverageEntitiesPerCell() const {
    if (grid_.empty()) {
        return 0.0f;
    }
    return static_cast<float>(getTotalEntityCount()) / static_cast<float>(grid_.size());
}

// ============================================================================
// BroadPhaseSystem
// ============================================================================

void BroadPhaseSystem::update(Registry& registry, SpatialHash& spatialHash) {
    spatialHash.clear();
    
    auto view = registry.view<Position>();
    view.each([&](EntityID entity, const Position& pos) {
        spatialHash.insert(entity, pos);
    });
}

void BroadPhaseSystem::findPotentialPairs(Registry& registry, 
                                          const SpatialHash& spatialHash,
                                          std::vector<std::pair<EntityID, EntityID>>& outPairs) {
    outPairs.clear();
    
    auto view = registry.view<Position, BoundingVolume>();
    
    view.each([&](EntityID entityA, const Position& posA, const BoundingVolume& bvA) {
        // Query nearby entities
        auto nearby = spatialHash.query(posA, bvA.radius * 2.0f);
        
        for (EntityID entityB : nearby) {
            // Only process each pair once (entityA < entityB)
            if (entityA >= entityB) {
                continue;
            }
            
            // Check collision layers, teams, etc.
            if (!canCollide(registry, entityA, entityB)) {
                continue;
            }
            
            // Check actual bounding volume overlap
            if (const Position* posB = registry.try_get<Position>(entityB)) {
                if (const BoundingVolume* bvB = registry.try_get<BoundingVolume>(entityB)) {
                    if (bvA.intersects(posA, *posB) || bvB->intersects(*posB, posA)) {
                        outPairs.emplace_back(entityA, entityB);
                    }
                }
            }
        }
    });
}

bool BroadPhaseSystem::canCollide(Registry& registry, EntityID a, EntityID b) const {
    // TODO: Implement layer masks, team checks, etc.
    // For now, all entities can collide
    (void)registry;
    (void)a;
    (void)b;
    return true;
}

} // namespace DarkAges
