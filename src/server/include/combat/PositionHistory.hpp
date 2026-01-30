#pragma once

#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <vector>
#include <array>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <cstddef>
#include <cstdint>

// [COMBAT_AGENT] Lag compensation system - 2-second position history buffer
// When a player fires at a target, the server rewinds to the target's position
// at the time the shot was fired (accounting for the shooter's latency)

namespace DarkAges {

// ============================================================================
// History entry for a single tick
// ============================================================================
struct PositionHistoryEntry {
    uint32_t timestamp;      // Server tick timestamp (milliseconds)
    Position position;       // Entity position
    Velocity velocity;       // Entity velocity (for interpolation)
    Rotation rotation;       // Entity rotation
};

// ============================================================================
// Circular buffer for position history (2 seconds at 60Hz = 120 entries)
// ============================================================================
class PositionHistory {
public:
    static constexpr size_t HISTORY_SIZE = 120;  // 2 seconds @ 60Hz
    
    PositionHistory();
    
    // Record a new position snapshot
    void record(uint32_t timestamp, const Position& pos, const Velocity& vel, const Rotation& rot);
    
    // Get position at a specific timestamp (for lag compensation)
    // Returns true if position found, false if timestamp is outside history window
    bool getPositionAtTime(uint32_t targetTimestamp, PositionHistoryEntry& outEntry) const;
    
    // Get interpolated position between two history entries
    bool getInterpolatedPosition(uint32_t targetTimestamp, PositionHistoryEntry& outEntry) const;
    
    // Check if timestamp is within our history window
    bool isTimeInHistory(uint32_t timestamp) const;
    
    // Get age of oldest entry in ms
    uint32_t getOldestEntryAge(uint32_t currentTime) const;
    
    // Clear all history
    void clear();
    
    // Get number of stored entries
    size_t size() const { return buffer_.size(); }

private:
    std::vector<PositionHistoryEntry> buffer_;
    mutable std::mutex mutex_;  // Thread-safe access
};

// ============================================================================
// Lag compensation system
// [COMBAT_AGENT] Contract for NetworkAgent:
// - Must provide accurate RTT measurements for hit validation
// - Must convert client attack timestamps to server time
// - See validateHit() for latency compensation requirements
// ============================================================================
class LagCompensator {
public:
    LagCompensator();
    
    // Record position for an entity (call every tick)
    void recordPosition(EntityID entity, uint32_t timestamp, 
                       const Position& pos, const Velocity& vel, const Rotation& rot);
    
    // Get entity position at a specific historical time
    // Used for hit validation - "where was the target when the shot was fired?"
    bool getHistoricalPosition(EntityID entity, uint32_t timestamp, 
                               PositionHistoryEntry& outEntry) const;
    
    // Check if a hit was valid given the attacker's latency
    // attackerEntity: who fired (for logging/debugging)
    // targetEntity: who was hit
    // attackTimestamp: when the attack input was sent (server time)
    // rttMs: attacker's round-trip time
    // claimedHitPos: where the client claims the hit occurred
    // hitRadius: collision radius for the attack
    bool validateHit(EntityID attackerEntity, EntityID targetEntity,
                    uint32_t attackTimestamp, uint32_t rttMs,
                    const Position& claimedHitPos, float hitRadius);
    
    // Get the maximum rewind time allowed
    static constexpr uint32_t getMaxRewindMs() { 
        return Constants::MAX_REWIND_MS;  // 500ms
    }
    
    // Remove entity history (when entity despawns)
    void removeEntity(EntityID entity);
    
    // Update all entities from registry (call every tick)
    void updateAllEntities(Registry& registry, uint32_t currentTimestamp);

private:
    std::unordered_map<EntityID, PositionHistory> entityHistories_;
    mutable std::shared_mutex mutex_;  // Shared read, exclusive write
};

} // namespace DarkAges
