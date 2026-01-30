// [COMBAT_AGENT] Lag compensation implementation
// 2-second position history buffer for server-side hit validation

#include "combat/PositionHistory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace DarkAges {

// ============================================================================
// PositionHistory Implementation
// ============================================================================

PositionHistory::PositionHistory() {
    buffer_.reserve(HISTORY_SIZE);
}

void PositionHistory::record(uint32_t timestamp, const Position& pos, 
                            const Velocity& vel, const Rotation& rot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    PositionHistoryEntry entry;
    entry.timestamp = timestamp;
    entry.position = pos;
    entry.velocity = vel;
    entry.rotation = rot;
    
    // Add new entry
    buffer_.push_back(entry);
    
    // Remove old entries to maintain 2-second window
    if (buffer_.size() > HISTORY_SIZE) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + (buffer_.size() - HISTORY_SIZE));
    }
    
    // Also remove entries older than 2 seconds
    auto it = buffer_.begin();
    while (it != buffer_.end() && 
           (timestamp - it->timestamp) > Constants::LAG_COMPENSATION_HISTORY_MS) {
        ++it;
    }
    if (it != buffer_.begin()) {
        buffer_.erase(buffer_.begin(), it);
    }
}

bool PositionHistory::getPositionAtTime(uint32_t targetTimestamp, 
                                       PositionHistoryEntry& outEntry) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_.empty()) {
        return false;
    }
    
    // Binary search for exact timestamp
    auto it = std::lower_bound(buffer_.begin(), buffer_.end(), targetTimestamp,
        [](const PositionHistoryEntry& entry, uint32_t ts) {
            return entry.timestamp < ts;
        });
    
    if (it != buffer_.end() && it->timestamp == targetTimestamp) {
        outEntry = *it;
        return true;
    }
    
    // No exact match - need interpolation
    return getInterpolatedPosition(targetTimestamp, outEntry);
}

bool PositionHistory::getInterpolatedPosition(uint32_t targetTimestamp,
                                             PositionHistoryEntry& outEntry) const {
    if (buffer_.size() < 2) {
        return false;
    }
    
    // Find entries bracketing the target timestamp
    const PositionHistoryEntry* before = nullptr;
    const PositionHistoryEntry* after = nullptr;
    
    for (const auto& entry : buffer_) {
        if (entry.timestamp <= targetTimestamp) {
            before = &entry;
        } else if (entry.timestamp > targetTimestamp && after == nullptr) {
            after = &entry;
            break;
        }
    }
    
    if (!before || !after) {
        return false;
    }
    
    // Calculate interpolation factor (0.0 to 1.0)
    uint32_t timeDiff = after->timestamp - before->timestamp;
    if (timeDiff == 0) {
        outEntry = *before;
        return true;
    }
    
    float t = static_cast<float>(targetTimestamp - before->timestamp) / timeDiff;
    t = std::clamp(t, 0.0f, 1.0f);
    
    // Interpolate position (fixed-point arithmetic)
    outEntry.timestamp = targetTimestamp;
    outEntry.position.x = static_cast<Constants::Fixed>(
        before->position.x + static_cast<int32_t>((after->position.x - before->position.x) * t));
    outEntry.position.y = static_cast<Constants::Fixed>(
        before->position.y + static_cast<int32_t>((after->position.y - before->position.y) * t));
    outEntry.position.z = static_cast<Constants::Fixed>(
        before->position.z + static_cast<int32_t>((after->position.z - before->position.z) * t));
    
    // Interpolate velocity
    outEntry.velocity.dx = static_cast<Constants::Fixed>(
        before->velocity.dx + static_cast<int32_t>((after->velocity.dx - before->velocity.dx) * t));
    outEntry.velocity.dy = static_cast<Constants::Fixed>(
        before->velocity.dy + static_cast<int32_t>((after->velocity.dy - before->velocity.dy) * t));
    outEntry.velocity.dz = static_cast<Constants::Fixed>(
        before->velocity.dz + static_cast<int32_t>((after->velocity.dz - before->velocity.dz) * t));
    
    // For rotation, just use 'before' rotation (smooth enough for hit detection)
    outEntry.rotation = before->rotation;
    
    return true;
}

bool PositionHistory::isTimeInHistory(uint32_t timestamp) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_.empty()) {
        return false;
    }
    
    uint32_t oldest = buffer_.front().timestamp;
    uint32_t newest = buffer_.back().timestamp;
    
    return timestamp >= oldest && timestamp <= newest;
}

uint32_t PositionHistory::getOldestEntryAge(uint32_t currentTime) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_.empty()) {
        return 0;
    }
    
    return currentTime - buffer_.front().timestamp;
}

void PositionHistory::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
}

// ============================================================================
// LagCompensator Implementation
// ============================================================================

LagCompensator::LagCompensator() {}

void LagCompensator::recordPosition(EntityID entity, uint32_t timestamp,
                                   const Position& pos, const Velocity& vel, 
                                   const Rotation& rot) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    entityHistories_[entity].record(timestamp, pos, vel, rot);
}

bool LagCompensator::getHistoricalPosition(EntityID entity, uint32_t timestamp,
                                          PositionHistoryEntry& outEntry) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = entityHistories_.find(entity);
    if (it == entityHistories_.end()) {
        return false;
    }
    
    return it->second.getPositionAtTime(timestamp, outEntry);
}

bool LagCompensator::validateHit(EntityID attackerEntity, EntityID targetEntity,
                                uint32_t attackTimestamp, uint32_t rttMs,
                                const Position& claimedHitPos, float hitRadius) {
    // Calculate when the shot was fired from attacker's perspective
    // Server time = attackTimestamp + (rttMs / 2) [one-way latency]
    uint32_t oneWayLatency = rttMs / 2;
    
    // Don't rewind more than MAX_REWIND_MS
    if (oneWayLatency > getMaxRewindMs()) {
        oneWayLatency = getMaxRewindMs();
    }
    
    uint32_t targetTimestamp = attackTimestamp + oneWayLatency;
    
    // Get target's position at that time
    PositionHistoryEntry targetEntry;
    if (!getHistoricalPosition(targetEntity, targetTimestamp, targetEntry)) {
        // Target position not in history - reject hit
        return false;
    }
    
    // Check if hit position is within valid radius of historical position
    float dx = (claimedHitPos.x - targetEntry.position.x) * Constants::FIXED_TO_FLOAT;
    float dy = (claimedHitPos.y - targetEntry.position.y) * Constants::FIXED_TO_FLOAT;
    float dz = (claimedHitPos.z - targetEntry.position.z) * Constants::FIXED_TO_FLOAT;
    float distSq = dx * dx + dy * dy + dz * dz;
    
    // Allow some tolerance for movement during latency
    float tolerance = hitRadius + 1.0f;  // 1m extra tolerance
    
    return distSq <= (tolerance * tolerance);
}

void LagCompensator::removeEntity(EntityID entity) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    entityHistories_.erase(entity);
}

void LagCompensator::updateAllEntities(Registry& registry, uint32_t currentTimestamp) {
    auto view = registry.view<Position, Velocity, Rotation>();
    
    view.each([this, currentTimestamp](EntityID entity, const Position& pos, 
                                      const Velocity& vel, const Rotation& rot) {
        recordPosition(entity, currentTimestamp, pos, vel, rot);
    });
}

} // namespace DarkAges
