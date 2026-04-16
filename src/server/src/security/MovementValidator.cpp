#include "security/MovementValidator.hpp"
#include "security/ViolationTracker.hpp"
#include "Constants.hpp"
#include <cmath>
#include <chrono>
#include <algorithm>
#include <unordered_set>

namespace DarkAges {
namespace Security {

MovementValidator::MovementValidator() = default;

CheatDetectionResult MovementValidator::validateMovement(EntityID entity,
    const Position& oldPos, const Position& newPos,
    uint32_t dtMs, const InputState& input,
    Registry& registry) {

    // Validate position bounds first
    auto result = validatePositionBounds(newPos);
    if (result.detected) return result;

    // Check for teleport
    result = detectTeleport(entity, oldPos, newPos, registry);
    if (result.detected) return result;

    // Check for speed hack
    result = detectSpeedHack(entity, oldPos, newPos, dtMs, input, registry);
    if (result.detected) return result;

    // Check for fly hack (if we have velocity component)
    if (Velocity* vel = registry.try_get<Velocity>(entity)) {
        result = detectFlyHack(entity, newPos, *vel, input, registry);
        if (result.detected) return result;
    }

    // Check for no-clip
    result = detectNoClip(entity, oldPos, newPos, registry);
    if (result.detected) return result;

    // Record clean movement for trust score
    if (tracker_) {
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
            tracker_->recordCleanMovement(info->playerId);
        }
    }

    return CheatDetectionResult{};  // No cheat detected
}

CheatDetectionResult MovementValidator::detectSpeedHack(EntityID entity,
    const Position& oldPos, const Position& newPos,
    uint32_t dtMs, const InputState& input, Registry& registry) {

    CheatDetectionResult result;

    if (dtMs == 0) return result;

    // Calculate actual speed
    float actualSpeed = calculateSpeed(oldPos, newPos, dtMs);

    // Calculate max allowed speed based on input
    float maxSpeed = calculateMaxAllowedSpeed(input);
    float tolerance = maxSpeed * (config_.speedTolerance - 1.0f);

    // Get behavior profile for trust-based adjustments
    float trustMultiplier = 1.0f;
    if (tracker_) {
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
            if (auto* profile = tracker_->getProfile(info->playerId)) {
                // More lenient for trusted players
                if (profile->isTrusted()) {
                    trustMultiplier = 1.1f;
                }
                // Stricter for suspicious players
                else if (profile->isSuspicious()) {
                    trustMultiplier = 0.8f;
                }
                // New player leniency
                if (profile->isNewPlayer(static_cast<uint32_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count() / 1000000))) {
                    trustMultiplier *= 1.2f;
                }
            }
        }
    }

    if (actualSpeed > (maxSpeed + tolerance) * trustMultiplier) {
        result.detected = true;
        result.type = CheatType::SPEED_HACK;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Speed hack detected";
        result.correctedPosition = oldPos;  // Revert to old position
        result.confidence = std::min(1.0f, (actualSpeed - maxSpeed) / maxSpeed);
        result.actualValue = actualSpeed;
        result.expectedValue = maxSpeed;
        result.timestamp = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() / 1000000);

        // Update profile and statistics via tracker
        if (tracker_) {
            if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
                tracker_->updateBehaviorProfile(info->playerId, result);
            }
            tracker_->incrementDetections(CheatType::SPEED_HACK);
            tracker_->reportViolation(registry.get<PlayerInfo>(entity).playerId, result);
        }
    }

    return result;
}

CheatDetectionResult MovementValidator::detectTeleport(EntityID entity,
    const Position& oldPos, const Position& newPos,
    Registry& registry) {

    CheatDetectionResult result;

    float distance = calculateDistance(oldPos, newPos);
    float maxTeleport = config_.maxTeleportDistance;

    if (distance > maxTeleport) {
        result.detected = true;
        result.type = CheatType::TELEPORT;
        result.severity = config_.instantBanOnTeleport ?
            ViolationSeverity::BAN : ViolationSeverity::CRITICAL;
        result.description = "Teleport hack detected";
        result.correctedPosition = oldPos;
        result.confidence = 1.0f;
        result.actualValue = distance;
        result.expectedValue = maxTeleport;
        result.timestamp = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() / 1000000);

        if (tracker_) {
            if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
                tracker_->updateBehaviorProfile(info->playerId, result);
            }
            tracker_->incrementDetections(CheatType::TELEPORT);
            tracker_->reportViolation(registry.get<PlayerInfo>(entity).playerId, result);
        }
    }

    return result;
}

CheatDetectionResult MovementValidator::detectFlyHack(EntityID entity,
    const Position& pos, const Velocity& vel,
    const InputState& input, Registry& registry) {

    CheatDetectionResult result;

    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float vy = vel.dy * Constants::FIXED_TO_FLOAT;

    // Check if Y position is valid (not flying above world)
    if (y > Constants::WORLD_MAX_Y) {
        result.detected = true;
        result.type = CheatType::FLY_HACK;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Fly hack detected - above world bounds";
        result.confidence = 1.0f;

        // Clamp Y to valid range
        result.correctedPosition = pos;
        result.correctedPosition.y = static_cast<Constants::Fixed>(
            Constants::WORLD_MAX_Y * Constants::FLOAT_TO_FIXED);

        if (tracker_) {
            if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
                tracker_->updateBehaviorProfile(info->playerId, result);
            }
            tracker_->incrementDetections(CheatType::FLY_HACK);
        }
        return result;
    }

    // Check vertical velocity without jump input
    if (vy > config_.maxVerticalSpeedNoJump && !input.jump) {
        if (!isGrounded(entity, pos, registry)) {
            // Check how long they've been airborne
            if (AntiCheatState* cheatState = registry.try_get<AntiCheatState>(entity)) {
                if (cheatState->lastValidationTime > 0) {
                    uint32_t airTime = static_cast<uint32_t>(
                        std::chrono::steady_clock::now().time_since_epoch().count() / 1000000)
                        - cheatState->lastValidationTime;

                    if (airTime > config_.maxAirTimeMs) {
                        result.detected = true;
                        result.type = CheatType::FLY_HACK;
                        result.severity = ViolationSeverity::SUSPICIOUS;
                        result.description = "Possible fly hack - extended air time";
                        result.confidence = std::min(1.0f,
                            static_cast<float>(airTime) / (config_.maxAirTimeMs * 2.0f));

                        if (tracker_) {
                            if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
                                tracker_->updateBehaviorProfile(info->playerId, result);
                            }
                            tracker_->incrementDetections(CheatType::FLY_HACK);
                        }
                        return result;
                    }
                }
            }
        }
    }

    return result;
}

CheatDetectionResult MovementValidator::detectNoClip(EntityID entity,
    const Position& oldPos, const Position& newPos,
    Registry& registry) {

    CheatDetectionResult result;

    // Graceful degradation: if spatial hash not available, skip detection
    if (!spatialHash_) {
        return result;
    }

    // Get moving entity's bounding volume
    const BoundingVolume* movingVolume = registry.try_get<BoundingVolume>(entity);
    float entityRadius = movingVolume ? movingVolume->radius : 0.5f;
    float entityHeight = movingVolume ? movingVolume->height : 1.8f;

    // Convert Fixed to float for spatial hash query
    float newX = newPos.x * Constants::FIXED_TO_FLOAT;
    float newZ = newPos.z * Constants::FIXED_TO_FLOAT;
    float newY = newPos.y * Constants::FIXED_TO_FLOAT;

    // Query nearby entities from spatial hash
    float queryRadius = entityRadius + 2.0f;
    auto nearby = spatialHash_->query(newX, newZ, queryRadius);

    // Build set of static entity IDs for quick lookup
    auto staticView = registry.view<StaticTag>();
    std::unordered_set<EntityID> staticEntities(staticView.begin(), staticView.end());

    // Check each nearby entity for collision
    for (EntityID other : nearby) {
        // Skip self
        if (other == entity) continue;

        // Check if entity is static world geometry
        if (staticEntities.find(other) == staticEntities.end()) continue;

        // Check bounding volume
        const BoundingVolume* staticVolume = registry.try_get<BoundingVolume>(other);
        if (!staticVolume) continue;

        // Get static entity position
        const Position* staticPos = registry.try_get<Position>(other);
        if (!staticPos) continue;

        // Check Y overlap (vertical intersection)
        float staticY = staticPos->y * Constants::FIXED_TO_FLOAT;
        float staticHeight = staticVolume->height;

        float newBottom = newY;
        float newTop = newY + entityHeight;
        float staticBottom = staticY;
        float staticTop = staticY + staticHeight;

        bool verticalOverlap = (newBottom < staticTop) && (newTop > staticBottom);
        if (!verticalOverlap) continue;

        // Check XZ plane intersection
        float dx = newX - (staticPos->x * Constants::FIXED_TO_FLOAT);
        float dz = newZ - (staticPos->z * Constants::FIXED_TO_FLOAT);
        float distSq = dx * dx + dz * dz;
        float combinedRadius = entityRadius + staticVolume->radius;

        if (distSq < (combinedRadius * combinedRadius)) {
            // No-clip detected: entity inside static geometry
            result.detected = true;
            result.type = CheatType::NO_CLIP;
            result.severity = ViolationSeverity::CRITICAL;
            result.description = "No-clip detected: walking through world geometry";
            result.correctedPosition = oldPos;
            result.confidence = 1.0f;
            result.actualValue = std::sqrt(distSq);
            result.expectedValue = combinedRadius;
            result.timestamp = static_cast<uint32_t>(
                std::chrono::steady_clock::now().time_since_epoch().count() / 1000000);

            if (tracker_) {
                if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
                    tracker_->updateBehaviorProfile(info->playerId, result);
                }
                tracker_->incrementDetections(CheatType::NO_CLIP);
                tracker_->reportViolation(registry.get<PlayerInfo>(entity).playerId, result);
            }
            return result;
        }
    }

    return result;
}

CheatDetectionResult MovementValidator::validatePositionBounds(
    const Position& pos) const {

    CheatDetectionResult result;

    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;

    // Check world bounds
    if (x < Constants::WORLD_MIN_X || x > Constants::WORLD_MAX_X ||
        y < Constants::WORLD_MIN_Y || y > Constants::WORLD_MAX_Y ||
        z < Constants::WORLD_MIN_Z || z > Constants::WORLD_MAX_Z) {

        result.detected = true;
        result.type = CheatType::POSITION_SPOOFING;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Position out of world bounds";
        result.confidence = 1.0f;

        // Clamp to valid bounds
        result.correctedPosition.x = static_cast<Constants::Fixed>(
            std::clamp(x, Constants::WORLD_MIN_X, Constants::WORLD_MAX_X) *
            Constants::FLOAT_TO_FIXED);
        result.correctedPosition.y = static_cast<Constants::Fixed>(
            std::clamp(y, Constants::WORLD_MIN_Y, Constants::WORLD_MAX_Y) *
            Constants::FLOAT_TO_FIXED);
        result.correctedPosition.z = static_cast<Constants::Fixed>(
            std::clamp(z, Constants::WORLD_MIN_Z, Constants::WORLD_MAX_Z) *
            Constants::FLOAT_TO_FIXED);
    }

    return result;
}

float MovementValidator::calculateMaxAllowedSpeed(const InputState& input) const {
    float baseSpeed = Constants::MAX_PLAYER_SPEED;
    if (input.sprint) {
        baseSpeed = Constants::MAX_SPRINT_SPEED;
    }
    return baseSpeed;
}

float MovementValidator::calculateDistance(const Position& a, const Position& b) const {
    float dx = (a.x - b.x) * Constants::FIXED_TO_FLOAT;
    float dy = (a.y - b.y) * Constants::FIXED_TO_FLOAT;
    float dz = (a.z - b.z) * Constants::FIXED_TO_FLOAT;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

float MovementValidator::calculateSpeed(const Position& from, const Position& to,
    uint32_t dtMs) const {

    if (dtMs == 0) return 0.0f;
    float distance = calculateDistance(from, to);
    return distance / (dtMs / 1000.0f);
}

bool MovementValidator::isPositionValid(const Position& pos) const {
    return isWithinWorldBounds(pos);
}

bool MovementValidator::isWithinWorldBounds(const Position& pos) const {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;

    return x >= Constants::WORLD_MIN_X && x <= Constants::WORLD_MAX_X &&
           y >= Constants::WORLD_MIN_Y && y <= Constants::WORLD_MAX_Y &&
           z >= Constants::WORLD_MIN_Z && z <= Constants::WORLD_MAX_Z;
}

bool MovementValidator::wouldCollideWithWorld(const Position& pos) const {
    // Placeholder - would check spatial hash against world geometry
    (void)pos;
    return false;
}

bool MovementValidator::isGrounded(EntityID entity, const Position& pos,
    Registry& registry) const {
    // Placeholder - would check for ground below player
    (void)entity;
    (void)pos;
    (void)registry;
    return true;  // Conservative default
}

void MovementValidator::applyPositionCorrection(EntityID entity,
    const Position& correctedPos, Registry& registry) {

    if (Position* pos = registry.try_get<Position>(entity)) {
        *pos = correctedPos;
    }
}

} // namespace Security
} // namespace DarkAges
