// [PHYSICS_AGENT] Kinematic character controller
// Server-authoritative movement with client prediction reconciliation

#include "physics/MovementSystem.hpp"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

namespace DarkAges {

// ============================================================================
// MovementSystem
// ============================================================================

void MovementSystem::update(Registry& registry, uint32_t currentTimeMs) {
    auto view = registry.view<Position, Velocity, InputState>();
    
    view.each([&](Position& pos, Velocity& vel, InputState& input) {
        processPhysics(vel, input);
        integrate(pos, vel);
        pos.timestamp_ms = currentTimeMs;
    });
}

MovementSystem::MovementResult MovementSystem::applyInput(Registry& registry,
                                                          EntityID entity,
                                                          const InputState& input,
                                                          uint32_t currentTimeMs) {
    MovementResult result;
    
    // Get components
    Position* pos = registry.try_get<Position>(entity);
    Velocity* vel = registry.try_get<Velocity>(entity);
    
    if (!pos || !vel) {
        result.valid = false;
        result.violationReason = "Entity missing Position or Velocity component";
        return result;
    }
    
    // Store old position for validation
    Position oldPos = *pos;
    
    // Apply physics
    processPhysics(*vel, input);
    integrate(*pos, *vel);
    
    // Clamp to world bounds
    clampPositionToWorldBounds(*pos);
    
    // Validate movement
    uint32_t dtMs = currentTimeMs - oldPos.timestamp_ms;
    if (dtMs == 0) dtMs = static_cast<uint32_t>(DT * 1000);
    
    float maxSpeed = MAX_SPEED * (input.sprint ? SPRINT_MULT : 1.0f);
    
    if (!validateMovement(oldPos, *pos, dtMs, maxSpeed)) {
        result.valid = false;
        result.antiCheatTriggered = true;
        result.violationReason = "Movement exceeded speed limit";
        result.correctedPosition = oldPos;
        result.correctedVelocity = *vel;
        
        // Revert to old position
        *pos = oldPos;
    } else {
        result.valid = true;
        result.correctedPosition = *pos;
        result.correctedVelocity = *vel;
    }
    
    pos->timestamp_ms = currentTimeMs;
    return result;
}

void MovementSystem::processPhysics(Velocity& vel, const InputState& input) {
    // Get input direction
    glm::vec3 inputDir = input.getInputDirection();
    
    if (glm::length(inputDir) > 0.0f) {
        // Apply rotation from yaw
        float cosYaw = std::cos(input.yaw);
        float sinYaw = std::sin(input.yaw);
        
        glm::vec3 rotatedDir(
            inputDir.x * cosYaw - inputDir.z * sinYaw,
            0.0f,
            inputDir.x * sinYaw + inputDir.z * cosYaw
        );
        
        // Calculate target speed
        float targetSpeed = MAX_SPEED * (input.sprint ? SPRINT_MULT : 1.0f);
        glm::vec3 targetVel = rotatedDir * targetSpeed;
        
        // Smooth acceleration (kinematic)
        glm::vec3 currentVel(
            vel.dx * Constants::FIXED_TO_FLOAT,
            vel.dy * Constants::FIXED_TO_FLOAT,
            vel.dz * Constants::FIXED_TO_FLOAT
        );
        
        glm::vec3 newVel = glm::mix(currentVel, targetVel, ACCELERATION * DT);
        
        // Convert back to fixed-point
        vel.dx = static_cast<Constants::Fixed>(newVel.x * Constants::FLOAT_TO_FIXED);
        vel.dy = static_cast<Constants::Fixed>(newVel.y * Constants::FLOAT_TO_FIXED);
        vel.dz = static_cast<Constants::Fixed>(newVel.z * Constants::FLOAT_TO_FIXED);
    } else {
        // Deceleration when no input
        applyFriction(vel, false);
    }
}

void MovementSystem::integrate(Position& pos, const Velocity& vel) {
    // pos += vel * DT
    float vx = vel.dx * Constants::FIXED_TO_FLOAT * DT;
    float vy = vel.dy * Constants::FIXED_TO_FLOAT * DT;
    float vz = vel.dz * Constants::FIXED_TO_FLOAT * DT;
    
    pos.x += static_cast<Constants::Fixed>(vx * Constants::FLOAT_TO_FIXED);
    pos.y += static_cast<Constants::Fixed>(vy * Constants::FLOAT_TO_FIXED);
    pos.z += static_cast<Constants::Fixed>(vz * Constants::FLOAT_TO_FIXED);
}

void MovementSystem::applyFriction(Velocity& vel, bool hasInput) {
    if (hasInput) return;
    
    // Simple exponential decay
    constexpr float FRICTION_FACTOR = 0.85f;  // ~15% slowdown per tick at 60Hz
    
    vel.dx = static_cast<Constants::Fixed>(vel.dx * FRICTION_FACTOR);
    vel.dz = static_cast<Constants::Fixed>(vel.dz * FRICTION_FACTOR);
    
    // Zero out very small velocities
    constexpr Constants::Fixed MIN_VELOCITY = 10;  // 0.01 m/s
    if (std::abs(vel.dx) < MIN_VELOCITY) vel.dx = 0;
    if (std::abs(vel.dz) < MIN_VELOCITY) vel.dz = 0;
}

bool MovementSystem::validateMovement(const Position& oldPos, const Position& newPos,
                                     uint32_t dtMs, float maxSpeed) const {
    if (dtMs == 0) return true;  // No time elapsed
    
    // Calculate actual distance moved
    float dx = (newPos.x - oldPos.x) * Constants::FIXED_TO_FLOAT;
    float dz = (newPos.z - oldPos.z) * Constants::FIXED_TO_FLOAT;
    float distance = std::sqrt(dx * dx + dz * dz);
    
    // Calculate maximum allowed distance (with tolerance)
    float maxDistance = calculateMaxDistance(maxSpeed, dtMs);
    
    return distance <= maxDistance;
}

void MovementSystem::clampPositionToWorldBounds(Position& pos) const {
    auto clampFixed = [](Constants::Fixed& val, float min, float max) {
        Constants::Fixed minFixed = static_cast<Constants::Fixed>(min * Constants::FLOAT_TO_FIXED);
        Constants::Fixed maxFixed = static_cast<Constants::Fixed>(max * Constants::FLOAT_TO_FIXED);
        val = std::clamp(val, minFixed, maxFixed);
    };
    
    clampFixed(pos.x, Constants::WORLD_MIN_X, Constants::WORLD_MAX_X);
    clampFixed(pos.y, Constants::WORLD_MIN_Y, Constants::WORLD_MAX_Y);
    clampFixed(pos.z, Constants::WORLD_MIN_Z, Constants::WORLD_MAX_Z);
}

void MovementSystem::resolveSoftCollision(Position& posA, const Position& posB,
                                         float radiusA, float radiusB) {
    float dx = (posA.x - posB.x) * Constants::FIXED_TO_FLOAT;
    float dz = (posA.z - posB.z) * Constants::FIXED_TO_FLOAT;
    float distSq = dx * dx + dz * dz;
    
    float minDist = radiusA + radiusB;
    
    if (distSq < minDist * minDist && distSq > 0.0001f) {
        float dist = std::sqrt(distSq);
        float overlap = minDist - dist;
        
        // Push apart by overlap amount
        float pushX = (dx / dist) * overlap;
        float pushZ = (dz / dist) * overlap;
        
        posA.x += static_cast<Constants::Fixed>(pushX * Constants::FLOAT_TO_FIXED);
        posA.z += static_cast<Constants::Fixed>(pushZ * Constants::FLOAT_TO_FIXED);
    }
}

// ============================================================================
// MovementValidator (Anti-cheat)
// ============================================================================

MovementValidator::ValidationResult MovementValidator::validate(Registry& registry,
                                                               EntityID entity,
                                                               const Position& proposedPos,
                                                               const AntiCheatState& cheatState,
                                                               uint32_t currentTimeMs) {
    ValidationResult result;
    result.accepted = true;
    
    const Position* currentPos = registry.try_get<Position>(entity);
    if (!currentPos) {
        result.accepted = false;
        result.reason = "Entity has no position";
        return result;
    }
    
    // Check for teleport
    if (isTeleport(*currentPos, proposedPos)) {
        result.accepted = false;
        result.suspicious = true;
        result.reason = "Teleport detected";
        result.correctedPosition = cheatState.lastValidPosition;
        return result;
    }
    
    // Check for speed hack
    uint32_t dtMs = currentTimeMs - cheatState.lastValidationTime;
    if (dtMs > 0) {
        float dx = (proposedPos.x - currentPos->x) * Constants::FIXED_TO_FLOAT;
        float dz = (proposedPos.z - currentPos->z) * Constants::FIXED_TO_FLOAT;
        float speed = std::sqrt(dx * dx + dz * dz) / (dtMs / 1000.0f);
        
        if (isSpeedHack(speed, dtMs)) {
            result.suspicious = true;
            // Still accept but flag for review
        }
    }
    
    result.correctedPosition = proposedPos;
    return result;
}

void MovementValidator::recordValidMovement(AntiCheatState& cheatState, 
                                           const Position& pos, 
                                           uint32_t timestamp) {
    cheatState.lastValidPosition = pos;
    cheatState.lastValidationTime = timestamp;
}

bool MovementValidator::shouldKick(const AntiCheatState& cheatState) const {
    return cheatState.suspiciousMovements >= Constants::SUSPICIOUS_MOVEMENT_THRESHOLD;
}

bool MovementValidator::isTeleport(const Position& from, const Position& to) const {
    float dx = (to.x - from.x) * Constants::FIXED_TO_FLOAT;
    float dz = (to.z - from.z) * Constants::FIXED_TO_FLOAT;
    float distSq = dx * dx + dz * dz;
    
    return distSq > (Constants::MAX_TELEPORT_DISTANCE * Constants::MAX_TELEPORT_DISTANCE);
}

bool MovementValidator::isSpeedHack(float speed, uint32_t dtMs) const {
    (void)dtMs;
    float maxAllowedSpeed = Constants::MAX_SPRINT_SPEED * Constants::SPEED_TOLERANCE;
    return speed > maxAllowedSpeed;
}

} // namespace DarkAges
