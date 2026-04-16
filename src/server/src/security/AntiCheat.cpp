#include "security/AntiCheat.hpp"
#include "Constants.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>
#include <unordered_set>

namespace DarkAges {
namespace Security {

// ========================================================================
// BehaviorProfile Implementation (kept here - simple data structure methods)
// ========================================================================

void BehaviorProfile::recordViolation(const CheatDetectionResult& detection) {
    ViolationRecord record;
    record.type = detection.type;
    record.timestamp = detection.timestamp;
    record.confidence = detection.confidence;
    if (detection.description) {
        record.details = detection.description;
    }

    violationHistory.push_back(record);

    // Keep only recent violations
    while (violationHistory.size() > MAX_VIOLATIONS) {
        violationHistory.erase(violationHistory.begin());
    }

    // Update type-specific counters
    switch (detection.type) {
        case CheatType::SPEED_HACK:
            speedViolations++;
            break;
        case CheatType::TELEPORT:
            teleportDetections++;
            break;
        case CheatType::FLY_HACK:
            flyViolations++;
            break;
        default:
            break;
    }

    // Decrease trust based on severity
    int8_t trustDelta = -5;
    switch (detection.severity) {
        case ViolationSeverity::WARNING:
            trustDelta = -5;
            break;
        case ViolationSeverity::SUSPICIOUS:
            trustDelta = -15;
            break;
        case ViolationSeverity::CRITICAL:
            trustDelta = -25;
            break;
        case ViolationSeverity::BAN:
            trustDelta = -100;
            break;
        default:
            trustDelta = -2;
    }

    updateTrustScore(trustDelta);
    consecutiveCleanTicks = 0;
}

void BehaviorProfile::recordCleanMovement() {
    consecutiveCleanTicks++;

    // Recover trust slowly over time
    if (consecutiveCleanTicks % 600 == 0) {  // Every ~10 seconds at 60Hz
        updateTrustScore(1);
    }
}

void BehaviorProfile::updateTrustScore(int8_t delta) {
    int16_t newScore = static_cast<int16_t>(trustScore) + delta;
    trustScore = static_cast<uint8_t>(std::clamp(newScore, static_cast<int16_t>(0), static_cast<int16_t>(100)));
}

bool BehaviorProfile::isNewPlayer(uint32_t currentTimeMs) const {
    // Consider new if account less than 5 minutes old
    return (currentTimeMs - accountCreationTime) < 300000;
}

uint32_t BehaviorProfile::getRecentViolationCount(uint32_t windowMs,
    uint32_t currentTimeMs) const {

    uint32_t count = 0;
    for (const auto& violation : violationHistory) {
        if (currentTimeMs - violation.timestamp <= windowMs) {
            count++;
        }
    }
    return count;
}

// ========================================================================
// AntiCheatSystem Implementation (facade delegating to subsystems)
// ========================================================================

AntiCheatSystem::AntiCheatSystem() = default;

AntiCheatSystem::~AntiCheatSystem() {
    shutdown();
}

bool AntiCheatSystem::initialize() {
    if (initialized_) {
        return true;
    }

    // Propagate config to subsystems
    movementValidator_.setConfig(config_);
    violationTracker_.setConfig(config_);

    // Connect movement validator to violation tracker
    movementValidator_.setViolationTracker(&violationTracker_);

    std::cout << "[ANTICHEAT] System initialized\n";
    std::cout << "[ANTICHEAT] Speed tolerance: " << config_.speedTolerance << "x\n";
    std::cout << "[ANTICHEAT] Max teleport distance: " << config_.maxTeleportDistance << "m\n";
    std::cout << "[ANTICHEAT] Rate limit: " << config_.maxInputsPerSecond << " inputs/sec\n";

    initialized_ = true;
    return true;
}

void AntiCheatSystem::shutdown() {
    if (!initialized_) {
        return;
    }

    // Disconnect movement validator from tracker
    movementValidator_.setViolationTracker(nullptr);

    // Clear profiles in tracker
    // Profiles are managed by ViolationTracker, which clears on destruction
    initialized_ = false;
}

// ========================================================================
// VALIDATION METHODS - Delegating to subsystems
// ========================================================================

CheatDetectionResult AntiCheatSystem::validateMovement(EntityID entity,
    const Position& oldPos, const Position& newPos,
    uint32_t dtMs, const InputState& input,
    Registry& registry) {

    return movementValidator_.validateMovement(entity, oldPos, newPos, dtMs, input, registry);
}

CheatDetectionResult AntiCheatSystem::validatePositionBounds(
    const Position& pos) const {

    return movementValidator_.validatePositionBounds(pos);
}

CheatDetectionResult AntiCheatSystem::validateCombat(EntityID attacker,
    EntityID target, int16_t claimedDamage,
    const Position& claimedHitPos, const Position& attackerPos,
    Registry& registry) {

    CheatDetectionResult result;

    // Validate damage amount
    if (claimedDamage > config_.maxDamagePerHit) {
        result.detected = true;
        result.type = CheatType::DAMAGE_HACK;
        result.severity = ViolationSeverity::BAN;
        result.description = "Damage exceeds maximum allowed";
        result.confidence = 1.0f;
        result.actualValue = static_cast<float>(claimedDamage);
        result.expectedValue = static_cast<float>(config_.maxDamagePerHit);

        if (PlayerInfo* info = registry.try_get<PlayerInfo>(attacker)) {
            violationTracker_.updateBehaviorProfile(info->playerId, result);
        }

        violationTracker_.incrementDetections(CheatType::DAMAGE_HACK);
        return result;
    }

    // Validate hit range
    float hitDistance = movementValidator_.calculateDistance(attackerPos, claimedHitPos);
    float maxRange = config_.maxMeleeRange;  // Could check weapon type for range

    if (hitDistance > maxRange * 1.5f) {  // 50% tolerance for lag
        result.detected = true;
        result.type = CheatType::HITBOX_EXTENSION;
        result.severity = ViolationSeverity::SUSPICIOUS;
        result.description = "Hit range exceeds maximum";
        result.confidence = std::min(1.0f, hitDistance / maxRange - 1.0f);
        result.actualValue = hitDistance;
        result.expectedValue = maxRange;

        if (PlayerInfo* info = registry.try_get<PlayerInfo>(attacker)) {
            violationTracker_.updateBehaviorProfile(info->playerId, result);
        }

        violationTracker_.incrementDetections(CheatType::HITBOX_EXTENSION);
        return result;
    }

    return result;
}

CheatDetectionResult AntiCheatSystem::validateInput(EntityID entity,
    const InputState& input, uint32_t currentTimeMs,
    Registry& registry) {

    CheatDetectionResult result;

    // Validate yaw value (should be within reasonable bounds)
    if (input.yaw < -config_.maxYaw || input.yaw > config_.maxYaw) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::WARNING;
        result.description = "Invalid yaw value";
        result.confidence = 1.0f;
        return result;
    }

    // Validate pitch value
    if (input.pitch < -config_.maxPitch || input.pitch > config_.maxPitch) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::WARNING;
        result.description = "Invalid pitch value";
        result.confidence = 1.0f;
        return result;
    }

    // Check for impossible input combinations
    if (input.forward && input.backward) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::INFO;
        result.description = "Conflicting input detected";
        result.confidence = 0.5f;
        return result;
    }

    if (input.left && input.right) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::INFO;
        result.description = "Conflicting input detected";
        result.confidence = 0.5f;
        return result;
    }

    return result;
}

CheatDetectionResult AntiCheatSystem::checkRateLimit(EntityID entity,
    uint32_t currentTimeMs, Registry& registry) {

    CheatDetectionResult result;

    AntiCheatState* cheatState = registry.try_get<AntiCheatState>(entity);
    if (!cheatState) return result;

    // Check if time window has passed
    if (currentTimeMs - cheatState->inputWindowStart > config_.inputWindowMs) {
        // New window
        cheatState->inputWindowStart = currentTimeMs;
        cheatState->inputCount = 1;
        return result;
    }

    // Increment count
    cheatState->inputCount++;

    // Check against limit (with burst allowance)
    if (cheatState->inputCount > config_.maxInputsPerSecond + config_.inputBurstAllowance) {
        result.detected = true;
        result.type = CheatType::PACKET_FLOODING;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Input rate limit exceeded";
        result.confidence = std::min(1.0f,
            static_cast<float>(cheatState->inputCount - config_.maxInputsPerSecond) /
            config_.maxInputsPerSecond);

        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
            violationTracker_.updateBehaviorProfile(info->playerId, result);
        }

        violationTracker_.incrementDetections(CheatType::PACKET_FLOODING);
        violationTracker_.reportViolation(registry.get<PlayerInfo>(entity).playerId, result);
    }

    return result;
}

// ========================================================================
// BEHAVIOR TRACKING - Delegating to ViolationTracker
// ========================================================================

void AntiCheatSystem::updateBehaviorProfile(uint64_t playerId,
    const CheatDetectionResult& detection) {

    violationTracker_.updateBehaviorProfile(playerId, detection);
}

void AntiCheatSystem::recordCleanMovement(uint64_t playerId) {
    violationTracker_.recordCleanMovement(playerId);
}

BehaviorProfile* AntiCheatSystem::getProfile(uint64_t playerId) {
    return violationTracker_.getProfile(playerId);
}

const BehaviorProfile* AntiCheatSystem::getProfile(uint64_t playerId) const {
    return violationTracker_.getProfile(playerId);
}

void AntiCheatSystem::removeProfile(uint64_t playerId) {
    violationTracker_.removeProfile(playerId);
}

// ========================================================================
// ACTIONS - Delegating to ViolationTracker
// ========================================================================

void AntiCheatSystem::banPlayer(uint64_t playerId, const char* reason,
    uint32_t durationMinutes) {

    violationTracker_.banPlayer(playerId, reason, durationMinutes);
}

void AntiCheatSystem::kickPlayer(uint64_t playerId, const char* reason) {
    violationTracker_.kickPlayer(playerId, reason);
}

void AntiCheatSystem::flagPlayerForReview(uint64_t playerId, const char* reason) {
    violationTracker_.flagPlayerForReview(playerId, reason);
}

// ========================================================================
// ServerAuthority Implementation (unchanged - separate static class)
// ========================================================================

void ServerAuthority::sendPositionCorrection(ConnectionID conn,
    const Position& serverPos, const Velocity& serverVel,
    uint32_t lastProcessedInput) {

    // This would serialize a position correction packet and send via NetworkManager
    // Placeholder implementation
    (void)conn;
    (void)serverPos;
    (void)serverVel;
    (void)lastProcessedInput;
}

void ServerAuthority::sendStateSync(ConnectionID conn, EntityID entity,
    Registry& registry) {

    // Send full entity state for major resyncs
    (void)conn;
    (void)entity;
    (void)registry;
}

void ServerAuthority::sendAuthorityCorrection(ConnectionID conn,
    const Position& serverPos, const Position& clientClaimedPos,
    const char* reason) {

    (void)conn;
    (void)serverPos;
    (void)clientClaimedPos;
    (void)reason;
}

bool ServerAuthority::validateClientClaim(const Position& serverPos,
    const Position& claimedPos, const Velocity& serverVel,
    const Velocity& claimedVel, float tolerance) {

    float dx = (serverPos.x - claimedPos.x) * Constants::FIXED_TO_FLOAT;
    float dy = (serverPos.y - claimedPos.y) * Constants::FIXED_TO_FLOAT;
    float dz = (serverPos.z - claimedPos.z) * Constants::FIXED_TO_FLOAT;

    float positionError = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (positionError > tolerance) {
        return false;
    }

    // Also check velocity if needed
    (void)serverVel;
    (void)claimedVel;

    return true;
}

float ServerAuthority::calculateCorrectionMagnitude(const Position& serverPos,
    const Position& clientPos) {

    float dx = (serverPos.x - clientPos.x) * Constants::FIXED_TO_FLOAT;
    float dy = (serverPos.y - clientPos.y) * Constants::FIXED_TO_FLOAT;
    float dz = (serverPos.z - clientPos.z) * Constants::FIXED_TO_FLOAT;

    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

bool ServerAuthority::needsCorrection(const Position& serverPos,
    const Position& clientPos, float tolerance) {

    return calculateCorrectionMagnitude(serverPos, clientPos) > tolerance;
}

// ========================================================================
// StatisticalAnalyzer Implementation (unchanged - placeholder static class)
// ========================================================================

bool StatisticalAnalyzer::analyzeAimPattern(EntityID entity,
    const std::vector<Rotation>& aimHistory) {
    (void)entity;
    (void)aimHistory;
    // Placeholder - would analyze aim smoothness, snap patterns, etc.
    return false;
}

float StatisticalAnalyzer::calculateMovementConsistency(
    const std::vector<Position>& positionHistory) {
    (void)positionHistory;
    // Placeholder - would calculate variance in movement patterns
    return 1.0f;
}

bool StatisticalAnalyzer::detectImpossibleReactions(
    const std::vector<uint32_t>& reactionTimeSamples) {
    (void)reactionTimeSamples;
    // Placeholder - would detect inhuman reaction times
    return false;
}

} // namespace Security
} // namespace DarkAges
