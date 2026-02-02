#pragma once

#include "Constants.hpp"
#include <cstdint>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <cmath>

// [PHYSICS_AGENT] Core ECS types and components
// All components are POD (Plain Old Data) for cache efficiency

namespace DarkAges {

using EntityID = entt::entity;
using Registry = entt::registry;
using ConnectionID = uint32_t;  // Network connection handle

// ============================================================================
// TRANSFORM COMPONENTS
// ============================================================================

// [PHYSICS_AGENT] Position using fixed-point for determinism
struct Position {
    Constants::Fixed x{0};
    Constants::Fixed y{0};
    Constants::Fixed z{0};
    uint32_t timestamp_ms{0};  // Server tick timestamp
    
    // Helper methods for float conversion
    [[nodiscard]] glm::vec3 toVec3() const {
        return glm::vec3(
            x * Constants::FIXED_TO_FLOAT,
            y * Constants::FIXED_TO_FLOAT,
            z * Constants::FIXED_TO_FLOAT
        );
    }
    
    static Position fromVec3(const glm::vec3& v, uint32_t timestamp = 0) {
        return Position{
            static_cast<Constants::Fixed>(v.x * Constants::FLOAT_TO_FIXED),
            static_cast<Constants::Fixed>(v.y * Constants::FLOAT_TO_FIXED),
            static_cast<Constants::Fixed>(v.z * Constants::FLOAT_TO_FIXED),
            timestamp
        };
    }
    
    // Distance squared (for performance - avoid sqrt when possible)
    [[nodiscard]] Constants::Fixed distanceSqTo(const Position& other) const {
        const Constants::Fixed dx = x - other.x;
        const Constants::Fixed dy = y - other.y;
        const Constants::Fixed dz = z - other.z;
        return (dx * dx + dy * dy + dz * dz) / Constants::FIXED_PRECISION;
    }
};

// [PHYSICS_AGENT] Velocity (also fixed-point)
struct Velocity {
    Constants::Fixed dx{0};
    Constants::Fixed dy{0};
    Constants::Fixed dz{0};
    
    [[nodiscard]] float speed() const {
        float fx = dx * Constants::FIXED_TO_FLOAT;
        float fy = dy * Constants::FIXED_TO_FLOAT;
        float fz = dz * Constants::FIXED_TO_FLOAT;
        return std::sqrt(fx * fx + fy * fy + fz * fz);
    }
    
    [[nodiscard]] float speedSq() const {
        float fx = dx * Constants::FIXED_TO_FLOAT;
        float fy = dy * Constants::FIXED_TO_FLOAT;
        float fz = dz * Constants::FIXED_TO_FLOAT;
        return fx * fx + fy * fy + fz * fz;
    }
};

// [PHYSICS_AGENT] Rotation (yaw only for top-down/3rd person)
struct Rotation {
    float yaw{0.0f};    // Radians, 0 = +Z, PI/2 = +X
    float pitch{0.0f};  // Radians, for looking up/down
};

// ============================================================================
// INPUT COMPONENT
// ============================================================================

// [NETWORK_AGENT] Bit-packed input state from client
struct InputState {
    // Bit flags (1 byte total)
    uint8_t forward : 1;
    uint8_t backward : 1;
    uint8_t left : 1;
    uint8_t right : 1;
    uint8_t jump : 1;
    uint8_t attack : 1;
    uint8_t block : 1;
    uint8_t sprint : 1;
    
    // Camera rotation
    float yaw{0.0f};
    float pitch{0.0f};
    
    // Networking metadata
    uint32_t sequence{0};      // Monotonic counter for reconciliation
    uint32_t timestamp_ms{0};  // Client send time
    uint32_t targetEntity{0};  // Target entity ID for abilities/combat
    
    // Initialize all bits to 0
    InputState() : forward(0), backward(0), left(0), right(0), 
                   jump(0), attack(0), block(0), sprint(0) {}
    
    // Helper to check if any movement input is active
    [[nodiscard]] bool hasMovementInput() const {
        return forward || backward || left || right;
    }
    
    // Get input direction vector (normalized, before rotation)
    [[nodiscard]] glm::vec3 getInputDirection() const {
        glm::vec3 dir(0.0f);
        if (forward)  dir.z -= 1.0f;
        if (backward) dir.z += 1.0f;
        if (left)     dir.x -= 1.0f;
        if (right)    dir.x += 1.0f;
        
        if (glm::length(dir) > 0.0f) {
            dir = glm::normalize(dir);
        }
        return dir;
    }
};

// ============================================================================
// COMBAT COMPONENTS
// ============================================================================

// [COMBAT_AGENT] Combat state
struct CombatState {
    int16_t health{10000};      // 0-10000 (100.0 health)
    int16_t maxHealth{10000};
    uint8_t teamId{0};
    uint8_t classType{0};
    EntityID lastAttacker{entt::null};
    uint32_t lastAttackTime{0};
    bool isDead{false};
    
    [[nodiscard]] float healthPercent() const {
        return static_cast<float>(health) / static_cast<float>(maxHealth) * 100.0f;
    }
};

// ============================================================================
// SPATIAL COMPONENTS
// ============================================================================

// [PHYSICS_AGENT] Spatial hash cell tracking
struct SpatialCell {
    int32_t cellX{0};
    int32_t cellZ{0};
    uint32_t zoneId{0};
};

// [PHYSICS_AGENT] Bounding volume for collision
struct BoundingVolume {
    float radius{0.5f};      // Cylinder radius
    float height{1.8f};      // Cylinder height
    
    [[nodiscard]] bool intersects(const Position& posA, const Position& posB) const {
        // 2D circle intersection (XZ plane)
        float dx = (posA.x - posB.x) * Constants::FIXED_TO_FLOAT;
        float dz = (posA.z - posB.z) * Constants::FIXED_TO_FLOAT;
        float distSq = dx * dx + dz * dz;
        float minDist = radius * 2.0f;
        return distSq < (minDist * minDist);
    }
};

// ============================================================================
// PLAYER COMPONENTS
// ============================================================================

// [DATABASE_AGENT] Player identification
struct PlayerInfo {
    uint64_t playerId{0};      // Persistent player ID from DB
    uint32_t connectionId{0};  // Network connection handle
    char username[32]{0};
    uint64_t sessionStart{0};
};

// [NETWORK_AGENT] Network connection state
struct NetworkState {
    uint32_t lastInputSequence{0};
    uint32_t lastInputTime{0};
    uint32_t rttMs{0};
    float packetLoss{0.0f};
    uint32_t snapshotSequence{0};
};

// [SECURITY_AGENT] Anti-cheat tracking
struct AntiCheatState {
    Position lastValidPosition;
    uint32_t lastValidationTime{0};
    uint32_t suspiciousMovements{0};
    float maxRecordedSpeed{0.0f};
    uint32_t inputCount{0};
    uint32_t inputWindowStart{0};
};

// [NETWORK_AGENT] Entity state for network serialization (WP-8-6)
enum class EntityType : uint8_t {
    PLAYER = 0,
    PROJECTILE = 1,
    LOOT = 2,
    NPC = 3
};

enum class AnimationState : uint8_t {
    IDLE = 0,
    WALK = 1,
    RUN = 2,
    ATTACK = 3,
    BLOCK = 4,
    DEAD = 5
};

struct EntityState {
    uint32_t id{0};
    EntityType type{EntityType::PLAYER};
    Position position{};  // Uses Fixed default constructor
    Velocity velocity{};  // Uses Fixed default constructor
    Rotation rotation{};  // Uses float default constructor
    uint8_t healthPercent{100};
    AnimationState animState{AnimationState::IDLE};
    uint32_t teamId{0};
};

// ============================================================================
// ABILITY COMPONENTS
// ============================================================================

// [COMBAT_AGENT] Individual ability data
struct Ability {
    uint32_t abilityId{0};
    float cooldownRemaining{0.0f};
    float manaCost{0.0f};
};

// [COMBAT_AGENT] Collection of abilities for an entity
struct Abilities {
    static constexpr uint32_t MAX_ABILITIES = 8;
    Ability abilities[MAX_ABILITIES];
    uint32_t count{0};
};

// [COMBAT_AGENT] Mana/resource pool
struct Mana {
    float current{100.0f};
    float max{100.0f};
    float regenerationRate{1.0f};  // per second
};

// ============================================================================
// ENTITY TAGS (empty types for tagging)
// ============================================================================

struct PlayerTag {};      // Entity is a player
struct NPCTag {};         // Entity is an NPC
struct ProjectileTag {};  // Entity is a projectile
struct StaticTag {};      // Entity is static world geometry

} // namespace DarkAges
