#pragma once

#include "ecs/CoreTypes.hpp"
#include <entt/entt.hpp>
#include <cstdint>

// [PHYSICS_AGENT] Kinematic character movement system
// Server-authoritative movement with anti-cheat validation

namespace DarkAges {

class MovementSystem {
public:
    static constexpr float MAX_SPEED = Constants::MAX_PLAYER_SPEED;
    static constexpr float SPRINT_MULT = Constants::SPRINT_SPEED_MULTIPLIER;
    static constexpr float ACCELERATION = Constants::ACCELERATION;
    static constexpr float DT = Constants::DT_SECONDS;
    
    struct MovementResult {
        bool valid{true};
        Position correctedPosition;
        Velocity correctedVelocity;
        bool antiCheatTriggered{false};
        const char* violationReason{nullptr};
    };

public:
    // Main update - processes all entities with Position, Velocity, InputState
    void update(Registry& registry, uint32_t currentTimeMs);
    
    // Apply input to single entity (returns movement result for validation)
    MovementResult applyInput(Registry& registry, EntityID entity, 
                              const InputState& input, uint32_t currentTimeMs);
    
    // Server-side validation (anti-cheat)
    [[nodiscard]] bool validateMovement(const Position& oldPos, const Position& newPos,
                                       uint32_t dtMs, float maxSpeed) const;
    
    // Calculate maximum allowed distance for given time window
    [[nodiscard]] float calculateMaxDistance(float maxSpeed, uint32_t dtMs) const {
        return maxSpeed * (dtMs / 1000.0f) * Constants::SPEED_TOLERANCE;
    }
    
    // Clamp position to world bounds
    void clampPositionToWorldBounds(Position& pos) const;
    
    // Resolve collision between two entities (soft collision - no physics)
    void resolveSoftCollision(Position& posA, const Position& posB, 
                             float radiusA, float radiusB);

private:
    // Apply physics step based on input
    void processPhysics(Velocity& vel, const InputState& input);
    
    // Apply velocity to position
    void integrate(Position& pos, const Velocity& vel);
    
    // Apply ground friction/deceleration
    void applyFriction(Velocity& vel, bool hasInput);
};

// [SECURITY_AGENT] Anti-cheat movement validator
class MovementValidator {
public:
    struct ValidationResult {
        bool accepted{true};
        bool suspicious{false};
        Position correctedPosition;
        const char* reason{nullptr};
    };
    
    // Validate proposed movement
    ValidationResult validate(Registry& registry, EntityID entity,
                             const Position& proposedPos,
                             const AntiCheatState& cheatState,
                             uint32_t currentTimeMs);
    
    // Update anti-cheat tracking
    void recordValidMovement(AntiCheatState& cheatState, const Position& pos, 
                            uint32_t timestamp);
    
    // Check if entity should be kicked
    [[nodiscard]] bool shouldKick(const AntiCheatState& cheatState) const;

private:
    // Check for teleport (instant large movement)
    [[nodiscard]] bool isTeleport(const Position& from, const Position& to) const;
    
    // Check for speed hack
    [[nodiscard]] bool isSpeedHack(float speed, uint32_t dtMs) const;
};

} // namespace DarkAges
