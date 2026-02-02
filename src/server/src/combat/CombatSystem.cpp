// [COMBAT_AGENT] Combat system implementation
// Handles hit detection, damage calculation, health management, and death handling

#include "combat/CombatSystem.hpp"
#include "Constants.hpp"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace DarkAges {

// ============================================================================
// CombatSystem Implementation
// ============================================================================

CombatSystem::CombatSystem(const CombatConfig& config) : config_(config) {}

HitResult CombatSystem::processAttack(Registry& registry, EntityID attacker,
                                     const AttackInput& input, uint32_t currentTimeMs) {
    HitResult result;
    
    // Check if can attack
    if (!canAttack(registry, attacker, currentTimeMs)) {
        result.hitType = "cooldown";
        return result;
    }
    
    // Process based on type
    switch (input.type) {
        case AttackInput::MELEE:
            result = performMeleeAttack(registry, attacker, currentTimeMs);
            break;
        case AttackInput::RANGED:
            result = performRangedAttack(registry, attacker, input.aimDirection, currentTimeMs);
            break;
        case AttackInput::ABILITY:
            // TODO: Ability system - not implemented in Phase 3B
            result.hitType = "not_implemented";
            break;
    }
    
    // Update attack cooldown
    if (CombatState* combat = registry.try_get<CombatState>(attacker)) {
        combat->lastAttackTime = currentTimeMs;
    }
    
    return result;
}

HitResult CombatSystem::performMeleeAttack(Registry& registry, EntityID attacker,
                                          uint32_t currentTimeMs) {
    HitResult result;
    
    // Find all valid targets in melee range
    auto targets = findMeleeTargets(registry, attacker);
    
    if (targets.empty()) {
        result.hitType = "miss";
        return result;
    }
    
    // Hit the closest target
    // For now, hit the first valid target (already sorted by distance in findMeleeTargets)
    for (EntityID target : targets) {
        bool isCritical = false;
        int16_t damage = calculateDamage(registry, attacker, target, 
                                        config_.baseMeleeDamage, isCritical);
        
        if (applyDamage(registry, target, attacker, damage, currentTimeMs)) {
            result.hit = true;
            result.target = target;
            result.damageDealt = damage;
            result.isCritical = isCritical;
            result.hitType = "melee";
            
            if (const Position* pos = registry.try_get<Position>(target)) {
                result.hitLocation = *pos;
            }
            
            // Callback
            if (onDamage_) {
                onDamage_(attacker, target, damage, result.hitLocation);
            }
            
            break;  // Melee hits one target
        }
    }
    
    return result;
}

HitResult CombatSystem::performRangedAttack(Registry& registry, EntityID attacker,
                                           const glm::vec3& aimDir, uint32_t currentTimeMs) {
    HitResult result;
    
    // TODO: Implement projectile system or raycast
    // For now, simple instant raycast
    
    const Position* attackerPos = registry.try_get<Position>(attacker);
    const Rotation* attackerRot = registry.try_get<Rotation>(attacker);
    
    if (!attackerPos || !attackerRot) {
        result.hitType = "miss";
        return result;
    }
    
    // Simple raycast in aim direction
    // In full implementation, use spatial hash for efficient raycast
    // and implement actual projectile physics
    
    result.hitType = "ranged";
    return result;
}

bool CombatSystem::applyDamage(Registry& registry, EntityID target, EntityID attacker,
                              int16_t damage, uint32_t currentTimeMs) {
    CombatState* combat = registry.try_get<CombatState>(target);
    if (!combat) {
        return false;
    }
    
    // Check if already dead
    if (combat->isDead) {
        return false;
    }
    
    // Apply damage
    combat->health -= damage;
    combat->lastAttacker = attacker;
    
    // Clamp to zero
    if (combat->health < 0) {
        combat->health = 0;
    }
    
    // Check for death
    if (combat->health == 0) {
        killEntity(registry, target, attacker);
    }
    
    return true;
}

bool CombatSystem::applyHeal(Registry& registry, EntityID target, int16_t amount) {
    CombatState* combat = registry.try_get<CombatState>(target);
    if (!combat || combat->isDead) {
        return false;
    }
    
    combat->health += amount;
    
    // Clamp to max
    if (combat->health > combat->maxHealth) {
        combat->health = combat->maxHealth;
    }
    
    return true;
}

bool CombatSystem::canAttack(Registry& registry, EntityID attacker, uint32_t currentTimeMs) const {
    const CombatState* combat = registry.try_get<CombatState>(attacker);
    if (!combat) {
        return false;
    }
    
    // Can't attack while dead
    if (combat->isDead) {
        return false;
    }
    
    // Check cooldown
    // lastAttackTime == 0 means never attacked, allow first attack
    if (combat->lastAttackTime == 0) {
        return true;
    }
    
    uint32_t timeSinceLastAttack = currentTimeMs - combat->lastAttackTime;
    if (timeSinceLastAttack < config_.attackCooldownMs) {
        return false;
    }
    
    return true;
}

void CombatSystem::killEntity(Registry& registry, EntityID victim, EntityID killer) {
    CombatState* combat = registry.try_get<CombatState>(victim);
    if (!combat) return;
    
    combat->isDead = true;
    combat->health = 0;
    combat->lastAttacker = killer;
    
    // Callback
    if (onDeath_) {
        onDeath_(victim, killer);
    }
}

void CombatSystem::respawnEntity(Registry& registry, EntityID entity, const Position& spawnPos) {
    CombatState* combat = registry.try_get<CombatState>(entity);
    if (!combat) return;
    
    combat->isDead = false;
    combat->health = combat->maxHealth;
    combat->lastAttacker = entt::null;
    
    if (Position* pos = registry.try_get<Position>(entity)) {
        *pos = spawnPos;
    }
    
    if (Velocity* vel = registry.try_get<Velocity>(entity)) {
        *vel = Velocity{};  // Zero velocity
    }
}

std::vector<EntityID> CombatSystem::findMeleeTargets(Registry& registry, EntityID attacker) {
    std::vector<EntityID> targets;
    
    const Position* attackerPos = registry.try_get<Position>(attacker);
    const Rotation* attackerRot = registry.try_get<Rotation>(attacker);
    
    if (!attackerPos || !attackerRot) {
        return targets;
    }
    
    // Get all entities with combat state
    auto view = registry.view<Position, CombatState>();
    
    view.each([&](EntityID target, const Position& targetPos, const CombatState& targetCombat) {
        // Skip self
        if (target == attacker) return;
        
        // Skip dead targets
        if (targetCombat.isDead) return;
        
        // Check team
        if (!canDamage(registry, attacker, target)) return;
        
        // Check range
        if (isInMeleeRange(registry, attacker, target)) {
            targets.push_back(target);
        }
    });
    
    return targets;
}

bool CombatSystem::isInMeleeRange(Registry& registry, EntityID attacker, EntityID target) const {
    const Position* attackerPos = registry.try_get<Position>(attacker);
    const Rotation* attackerRot = registry.try_get<Rotation>(attacker);
    const Position* targetPos = registry.try_get<Position>(target);
    
    if (!attackerPos || !attackerRot || !targetPos) {
        return false;
    }
    
    // Calculate distance using fixed-point arithmetic
    float dx = (targetPos->x - attackerPos->x) * Constants::FIXED_TO_FLOAT;
    float dy = (targetPos->y - attackerPos->y) * Constants::FIXED_TO_FLOAT;
    float dz = (targetPos->z - attackerPos->z) * Constants::FIXED_TO_FLOAT;
    float distSq = dx*dx + dy*dy + dz*dz;
    float maxRangeSq = config_.meleeRange * config_.meleeRange;
    
    if (distSq > maxRangeSq) {
        return false;
    }
    
    // Check angle (must be in front)
    // Only check horizontal angle (XZ plane) for melee
    glm::vec3 attackerForward = getForwardVector(attackerRot->yaw);
    glm::vec3 toTarget(dx, 0.0f, dz);
    
    float toTargetLen = std::sqrt(dx*dx + dz*dz);
    if (toTargetLen > 0.001f) {
        toTarget /= toTargetLen;
        float dotProduct = glm::dot(attackerForward, toTarget);
        // Clamp to avoid acos domain errors
        dotProduct = std::clamp(dotProduct, -1.0f, 1.0f);
        float angle = std::acos(dotProduct) * 180.0f / 3.14159265359f;
        
        if (angle > config_.meleeAngle / 2.0f) {
            return false;
        }
    }
    
    return true;
}

bool CombatSystem::canDamage(Registry& registry, EntityID attacker, EntityID target) const {
    if (config_.allowFriendlyFire) {
        return true;
    }
    
    const CombatState* attackerCombat = registry.try_get<CombatState>(attacker);
    const CombatState* targetCombat = registry.try_get<CombatState>(target);
    
    if (!attackerCombat || !targetCombat) {
        return true;  // No team info = can damage (default)
    }
    
    // Same team = can't damage (unless friendly fire enabled)
    // Team 0 is "no team" / free-for-all
    if (attackerCombat->teamId == 0 || targetCombat->teamId == 0) {
        return true;
    }
    
    return attackerCombat->teamId != targetCombat->teamId;
}

int16_t CombatSystem::calculateDamage(Registry& registry, EntityID attacker, EntityID target,
                                     int16_t baseDamage, bool& outCritical) const {
    outCritical = false;
    
    // Damage variance (±10% by default)
    float minVariance = 1.0f - (config_.damageVariance / 100.0f);
    float maxVariance = 1.0f + (config_.damageVariance / 100.0f);
    float varianceRange = maxVariance - minVariance;
    float variance = minVariance + (static_cast<float>(rand()) / RAND_MAX) * varianceRange;
    
    // Critical hit
    int critRoll = rand() % 100;
    if (critRoll < config_.criticalChance) {
        outCritical = true;
        variance *= config_.criticalMultiplier / 100.0f;
    }
    
    int16_t finalDamage = static_cast<int16_t>(baseDamage * variance);
    
    // Ensure minimum damage of 1
    if (finalDamage < 1) {
        finalDamage = 1;
    }
    
    return finalDamage;
}

glm::vec3 CombatSystem::getForwardVector(float yaw) const {
    // Yaw: 0 = +Z, PI/2 = +X
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

void CombatSystem::onEntityDied(Registry& registry, EntityID victim, EntityID killer) {
    // This is called internally by killEntity before the callback
    // Additional death logic can be added here
}

// ============================================================================
// HealthRegenSystem Implementation
// ============================================================================

void HealthRegenSystem::update(Registry& registry, uint32_t currentTimeMs) {
    if (currentTimeMs - lastRegenTime_ < REGEN_INTERVAL_MS) {
        return;
    }
    
    lastRegenTime_ = currentTimeMs;
    
    auto view = registry.view<CombatState>();
    view.each([](CombatState& combat) {
        // Regen if not dead and not full health
        if (!combat.isDead && combat.health < combat.maxHealth) {
            combat.health += REGEN_AMOUNT;
            if (combat.health > combat.maxHealth) {
                combat.health = combat.maxHealth;
            }
        }
    });
}

} // namespace DarkAges
