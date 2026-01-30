#pragma once

#include "ecs/CoreTypes.hpp"
#include "physics/SpatialHash.hpp"
#include "combat/PositionHistory.hpp"
#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include <cstdint>

// [COMBAT_AGENT] Combat system core for DarkAges MMO
// Handles hit detection, damage calculation, health management, and death handling

namespace DarkAges {

// ============================================================================
// Combat Configuration
// ============================================================================

// [COMBAT_AGENT] Combat configuration
struct CombatConfig {
    // Damage values (fixed-point: 1000 = 100 damage)
    int16_t baseMeleeDamage = 2500;      // 250 damage
    int16_t baseRangedDamage = 1500;     // 150 damage
    int16_t criticalMultiplier = 150;    // 1.5x (150%)
    
    // Attack timing
    uint32_t attackCooldownMs = 500;     // 0.5s between attacks
    float meleeRange = 2.5f;             // meters
    float meleeAngle = 60.0f;            // degrees (cone in front)
    
    // Team settings
    bool allowFriendlyFire = false;
    
    // Critical hit chance (percentage: 10 = 10%)
    uint8_t criticalChance = 10;
    
    // Damage variance (percentage: 10 = ±10%)
    uint8_t damageVariance = 10;
};

// ============================================================================
// Hit Result
// ============================================================================

// [COMBAT_AGENT] Hit result from an attack
struct HitResult {
    bool hit{false};
    EntityID target{entt::null};
    int16_t damageDealt{0};
    bool isCritical{false};
    Position hitLocation{0, 0, 0, 0};
    const char* hitType{nullptr};  // "melee", "ranged", "miss", "cooldown", "blocked"
};

// ============================================================================
// Attack Input
// ============================================================================

// [COMBAT_AGENT] Attack input from client
struct AttackInput {
    enum Type { MELEE, RANGED, ABILITY } type;
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t targetEntity;  // 0 = no target (area attack)
    glm::vec3 aimDirection;
    uint8_t abilityId{0};   // For ability attacks
    
    AttackInput() : type(MELEE), sequence(0), timestamp(0), 
                    targetEntity(0), aimDirection(0, 0, 1), abilityId(0) {}
};

// ============================================================================
// Combat System
// ============================================================================

// [COMBAT_AGENT] Main combat system
class CombatSystem {
public:
    using DeathCallback = std::function<void(EntityID victim, EntityID killer)>;
    using DamageCallback = std::function<void(EntityID attacker, EntityID target, 
                                               int16_t damage, const Position& location)>;

public:
    CombatSystem(const CombatConfig& config = CombatConfig{});
    
    // Process an attack input
    HitResult processAttack(Registry& registry, EntityID attacker, 
                           const AttackInput& input, uint32_t currentTimeMs);
    
    // Melee attack (cone in front of attacker)
    HitResult performMeleeAttack(Registry& registry, EntityID attacker,
                                 uint32_t currentTimeMs);
    
    // Ranged attack (raycast/Projectile)
    HitResult performRangedAttack(Registry& registry, EntityID attacker,
                                  const glm::vec3& aimDir, uint32_t currentTimeMs);
    
    // Apply damage to target
    bool applyDamage(Registry& registry, EntityID target, EntityID attacker,
                    int16_t damage, uint32_t currentTimeMs);
    
    // Heal target
    bool applyHeal(Registry& registry, EntityID target, int16_t amount);
    
    // Check if entity can attack (cooldown, dead, etc.)
    bool canAttack(Registry& registry, EntityID attacker, uint32_t currentTimeMs) const;
    
    // Set callbacks
    void setOnDeath(DeathCallback callback) { onDeath_ = std::move(callback); }
    void setOnDamage(DamageCallback callback) { onDamage_ = std::move(callback); }
    
    // Death handling
    void killEntity(Registry& registry, EntityID victim, EntityID killer);
    void respawnEntity(Registry& registry, EntityID entity, const Position& spawnPos);
    
    // Getters
    const CombatConfig& getConfig() const { return config_; }
    void setConfig(const CombatConfig& config) { config_ = config; }

// Testing support - normally private, made public for unit tests
    // Find targets in melee cone
    std::vector<EntityID> findMeleeTargets(Registry& registry, EntityID attacker);
    
    // Calculate damage with modifiers (normally private)
    int16_t calculateDamage(Registry& registry, EntityID attacker, EntityID target,
                           int16_t baseDamage, bool& outCritical) const;
    
private:
    
    // Check if target is in melee range and cone
    bool isInMeleeRange(Registry& registry, EntityID attacker, EntityID target) const;
    
    // Check teams (friendly fire)
    bool canDamage(Registry& registry, EntityID attacker, EntityID target) const;
    
    // Get forward vector from rotation
    glm::vec3 getForwardVector(float yaw) const;
    
    // Death callback
    void onEntityDied(Registry& registry, EntityID victim, EntityID killer);

private:
    CombatConfig config_;
    DeathCallback onDeath_;
    DamageCallback onDamage_;
};

// ============================================================================
// Health Regeneration System
// ============================================================================

// [COMBAT_AGENT] Health regeneration system
class HealthRegenSystem {
public:
    static constexpr uint32_t REGEN_INTERVAL_MS = 1000;  // 1 second
    static constexpr int16_t REGEN_AMOUNT = 50;          // 5 HP per second (fixed-point)
    
    void update(Registry& registry, uint32_t currentTimeMs);

private:
    uint32_t lastRegenTime_{0};
};

} // namespace DarkAges
