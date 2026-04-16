#pragma once

#include "security/AntiCheatTypes.hpp"
#include "security/AntiCheatConfig.hpp"
#include "physics/SpatialHash.hpp"
#include <cstdint>

// [SECURITY_AGENT] Movement validation subsystem
// Extracted from AntiCheatSystem for cohesion and testability

namespace DarkAges {
namespace Security {

// Forward declarations
class ViolationTracker;

// [SECURITY_AGENT] Validates player movement for cheat detection
// Handles speed hacks, teleport hacks, fly hacks, no-clip, and position bounds
class MovementValidator {
public:
    MovementValidator();

    // Set configuration
    void setConfig(const AntiCheatConfig& config) { config_ = config; }

    // Set spatial hash for collision detection
    void setSpatialHash(const SpatialHash* hash) { spatialHash_ = hash; }

    // Set violation tracker for reporting detections
    void setViolationTracker(ViolationTracker* tracker) { tracker_ = tracker; }

    // Validate full movement (orchestrates all movement checks)
    [[nodiscard]] CheatDetectionResult validateMovement(EntityID entity,
        const Position& oldPos, const Position& newPos,
        uint32_t dtMs, const InputState& input,
        Registry& registry);

    // Individual detection methods
    [[nodiscard]] CheatDetectionResult detectSpeedHack(EntityID entity,
        const Position& oldPos, const Position& newPos,
        uint32_t dtMs, const InputState& input, Registry& registry);

    [[nodiscard]] CheatDetectionResult detectTeleport(EntityID entity,
        const Position& oldPos, const Position& newPos,
        Registry& registry);

    [[nodiscard]] CheatDetectionResult detectFlyHack(EntityID entity,
        const Position& pos, const Velocity& vel,
        const InputState& input, Registry& registry);

    [[nodiscard]] CheatDetectionResult detectNoClip(EntityID entity,
        const Position& oldPos, const Position& newPos,
        Registry& registry);

    // Validate position is within world bounds
    [[nodiscard]] CheatDetectionResult validatePositionBounds(
        const Position& pos) const;

    // Utility methods
    [[nodiscard]] float calculateMaxAllowedSpeed(const InputState& input) const;
    [[nodiscard]] float calculateDistance(const Position& a, const Position& b) const;
    [[nodiscard]] float calculateSpeed(const Position& from, const Position& to,
        uint32_t dtMs) const;
    [[nodiscard]] bool isPositionValid(const Position& pos) const;
    [[nodiscard]] bool isWithinWorldBounds(const Position& pos) const;
    [[nodiscard]] bool wouldCollideWithWorld(const Position& pos) const;
    [[nodiscard]] bool isGrounded(EntityID entity, const Position& pos,
        Registry& registry) const;

    void applyPositionCorrection(EntityID entity, const Position& correctedPos,
        Registry& registry);

private:
    AntiCheatConfig config_;
    const SpatialHash* spatialHash_{nullptr};
    ViolationTracker* tracker_{nullptr};
};

} // namespace Security
} // namespace DarkAges
