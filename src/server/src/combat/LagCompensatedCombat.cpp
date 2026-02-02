// [COMBAT_AGENT] Lag-compensated combat system implementation - Phase 3C
// Validates hits against historical positions using the lag compensator

#include "combat/LagCompensatedCombat.hpp"
#include "Constants.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace DarkAges {

// ============================================================================
// Constructor
// ============================================================================

LagCompensatedCombat::LagCompensatedCombat(CombatSystem& combat, LagCompensator& lagComp)
    : combatSystem_(combat), lagCompensator_(lagComp) {}

// ============================================================================
// Main Attack Processing
// ============================================================================

std::vector<HitResult> LagCompensatedCombat::processAttackWithRewind(
    Registry& registry, const LagCompensatedAttack& attack) {
    
    std::vector<HitResult> results;
    
    // Check if attacker can attack (cooldown, dead, etc.)
    if (!combatSystem_.canAttack(registry, attack.attacker, attack.serverTimestamp)) {
        HitResult result;
        result.hit = false;
        result.hitType = "cooldown";
        results.push_back(result);
        return results;
    }
    
    // Check if rewind is valid (latency within acceptable limits)
    bool useRewind = isRewindValid(attack.rttMs);
    
    // Calculate when the attack was initiated
    uint32_t attackTime = calculateAttackTime(attack.clientTimestamp, attack.rttMs);
    
    // Log if we're not using rewind due to high latency
    if (!useRewind) {
        // For high latency, fall back to current-time processing
        // This is a QoS degradation for high-ping players
        HitResult result = combatSystem_.processAttack(registry, attack.attacker, 
                                                       attack.input, attack.serverTimestamp);
        results.push_back(result);
        return results;
    }
    
    // Find valid targets at historical positions
    auto targets = findHistoricalTargets(registry, attack.attacker, attackTime);
    
    // Validate each potential hit at historical position
    for (EntityID target : targets) {
        bool validHit = false;
        
        switch (attack.input.type) {
            case AttackInput::MELEE:
                validHit = validateMeleeHitAtTime(registry, attack.attacker, target, attackTime);
                break;
                
            case AttackInput::RANGED:
                validHit = validateRangedHitAtTime(registry, attack.attacker, target,
                                                   attack.input.aimDirection, attackTime);
                break;
                
            case AttackInput::ABILITY:
                // Abilities have their own validation - pass through for now
                // In full implementation, ability system would handle this
                validHit = true;  // Assume valid for ability attacks
                break;
        }
        
        if (validHit) {
            // Apply damage at CURRENT time (damage happens now, not in past)
            // This is critical: we validate at historical time but apply at current time
            bool isCritical = false;
            int16_t baseDamage = (attack.input.type == AttackInput::MELEE) 
                ? combatSystem_.getConfig().baseMeleeDamage 
                : combatSystem_.getConfig().baseRangedDamage;
            
            // Note: We need to expose calculateDamage in CombatSystem or use applyDamage
            // For now, we'll use applyDamage with base damage
            int16_t damage = baseDamage / 100;  // Simplified - just use base
            
            if (combatSystem_.applyDamage(registry, target, attack.attacker, 
                                         damage, attack.serverTimestamp)) {
                HitResult result;
                result.hit = true;
                result.target = target;
                result.damageDealt = damage;
                result.isCritical = isCritical;
                result.hitType = (attack.input.type == AttackInput::MELEE) ? "melee" : "ranged";
                
                // Record current position as hit location (where they are NOW)
                if (const Position* pos = registry.try_get<Position>(target)) {
                    result.hitLocation = *pos;
                }
                
                results.push_back(result);
                
                // Melee attacks only hit one target - stop after first hit
                if (attack.input.type == AttackInput::MELEE) {
                    break;
                }
            }
        }
    }
    
    // Update attacker's cooldown (record that they attacked)
    if (CombatState* combat = registry.try_get<CombatState>(attack.attacker)) {
        combat->lastAttackTime = attack.serverTimestamp;
    }
    
    // If no hits, record a miss
    if (results.empty()) {
        HitResult result;
        result.hit = false;
        result.hitType = "miss";
        results.push_back(result);
    }
    
    return results;
}

// ============================================================================
// Hit Claim Validation
// ============================================================================

bool LagCompensatedCombat::validateHitClaim(Registry& registry, EntityID attacker,
                                           EntityID claimedTarget, const Position& claimedPosition,
                                           uint32_t attackTimestamp, uint32_t rttMs) {
    // Check if rewind is valid
    if (!isRewindValid(rttMs)) {
        return false;  // Can't validate with excessive latency
    }
    
    // Calculate attack time
    uint32_t attackTime = calculateAttackTime(attackTimestamp, rttMs);
    
    // Get target's historical position
    PositionHistoryEntry targetEntry;
    if (!lagCompensator_.getHistoricalPosition(claimedTarget, attackTime, targetEntry)) {
        return false;  // No history for that time
    }
    
    // Check if claimed position matches historical position (within tolerance)
    // Convert fixed-point to float for distance calculation
    float dx = (claimedPosition.x - targetEntry.position.x) * Constants::FIXED_TO_FLOAT;
    float dy = (claimedPosition.y - targetEntry.position.y) * Constants::FIXED_TO_FLOAT;
    float dz = (claimedPosition.z - targetEntry.position.z) * Constants::FIXED_TO_FLOAT;
    float distSq = dx*dx + dy*dy + dz*dz;
    
    // Allow 2m tolerance for:
    // - Movement during latency
    // - Position interpolation errors
    // - Client-side prediction differences
    constexpr float TOLERANCE_METERS = 2.0f;
    
    return distSq <= (TOLERANCE_METERS * TOLERANCE_METERS);
}

// ============================================================================
// Area-of-Effect Rewind
// ============================================================================

void LagCompensatedCombat::rewindEntitiesInArea(Registry& registry, const Position& center,
                                                float radius, uint32_t targetTimestamp,
                                                std::vector<EntityID>& outAffectedEntities,
                                                EntityID excludeEntity) {
    // Clear output vector
    outAffectedEntities.clear();
    
    // Find all entities with position and combat state
    auto view = registry.view<Position, CombatState>();
    
    view.each([&](EntityID entity, const Position& currentPos, const CombatState& combat) {
        // Skip excluded entity (e.g., the caster of an AOE attack)
        if (entity == excludeEntity) return;
        
        // Skip dead entities
        if (combat.isDead) return;
        
        // Get historical position
        PositionHistoryEntry histEntry;
        if (lagCompensator_.getHistoricalPosition(entity, targetTimestamp, histEntry)) {
            // Check if within radius at historical position
            float dx = (histEntry.position.x - center.x) * Constants::FIXED_TO_FLOAT;
            float dy = (histEntry.position.y - center.y) * Constants::FIXED_TO_FLOAT;
            float dz = (histEntry.position.z - center.z) * Constants::FIXED_TO_FLOAT;
            float distSq = dx*dx + dy*dy + dz*dz;
            
            if (distSq <= (radius * radius)) {
                outAffectedEntities.push_back(entity);
            }
        }
    });
}

// ============================================================================
// History Query
// ============================================================================

bool LagCompensatedCombat::hasHistoryForEntityAtTime(EntityID entity, uint32_t timestamp) const {
    PositionHistoryEntry entry;
    return lagCompensator_.getHistoricalPosition(entity, timestamp, entry);
}

// ============================================================================
// Private Helper Methods
// ============================================================================

uint32_t LagCompensatedCombat::calculateAttackTime(uint32_t clientTimestamp, uint32_t rttMs) const {
    // Calculate one-way latency (half of round-trip time)
    uint32_t oneWayLatency = rttMs / 2;
    
    // Clamp to max rewind to prevent excessive rewinds
    if (oneWayLatency > Constants::MAX_REWIND_MS) {
        oneWayLatency = Constants::MAX_REWIND_MS;
    }
    
    // The attack was initiated at clientTimestamp on the client
    // By the time the server receives it, one-way latency has elapsed
    // So the server time when the attack was initiated is: clientTimestamp + oneWayLatency
    // This is the time we need to rewind to for lag compensation
    return clientTimestamp + oneWayLatency;
}

bool LagCompensatedCombat::isRewindValid(uint32_t rttMs) const {
    // Don't rewind more than MAX_REWIND_MS (500ms)
    // This prevents:
    // - Excessive memory usage for position history lookups
    // - Gameplay issues with very high latency players
    // - Potential exploits
    return (rttMs / 2) <= Constants::MAX_REWIND_MS;
}

std::vector<EntityID> LagCompensatedCombat::findHistoricalTargets(Registry& registry,
                                                                 EntityID attacker,
                                                                 uint32_t targetTimestamp) {
    std::vector<EntityID> targets;
    
    // Get attacker's position at attack time
    PositionHistoryEntry attackerEntry;
    if (!lagCompensator_.getHistoricalPosition(attacker, targetTimestamp, attackerEntry)) {
        return targets;  // Can't determine attack origin
    }
    
    // Find all potential targets (entities with CombatState)
    auto view = registry.view<Position, CombatState>();
    
    view.each([&](EntityID target, const Position& currentPos, const CombatState& combat) {
        // Skip self
        if (target == attacker) return;
        
        // Skip dead targets
        if (combat.isDead) return;
        
        // Get target's historical position
        PositionHistoryEntry targetEntry;
        if (!lagCompensator_.getHistoricalPosition(target, targetTimestamp, targetEntry)) {
            return;  // No history for this target
        }
        
        // Check range using AOI_RADIUS_NEAR (50m) as max search distance
        // This is an optimization - we don't need to check entities far away
        float dx = (targetEntry.position.x - attackerEntry.position.x) * Constants::FIXED_TO_FLOAT;
        float dy = (targetEntry.position.y - attackerEntry.position.y) * Constants::FIXED_TO_FLOAT;
        float dz = (targetEntry.position.z - attackerEntry.position.z) * Constants::FIXED_TO_FLOAT;
        float distSq = dx*dx + dy*dy + dz*dz;
        
        if (distSq <= (Constants::AOI_RADIUS_NEAR * Constants::AOI_RADIUS_NEAR)) {
            targets.push_back(target);
        }
    });
    
    return targets;
}

bool LagCompensatedCombat::validateMeleeHitAtTime(Registry& registry, EntityID attacker,
                                                 EntityID target, uint32_t targetTimestamp) {
    // Get historical positions for both attacker and target
    PositionHistoryEntry attackerEntry, targetEntry;
    
    if (!lagCompensator_.getHistoricalPosition(attacker, targetTimestamp, attackerEntry)) {
        return false;  // No attacker history
    }
    
    if (!lagCompensator_.getHistoricalPosition(target, targetTimestamp, targetEntry)) {
        return false;  // No target history
    }
    
    // Calculate distance at historical time
    float dx = (targetEntry.position.x - attackerEntry.position.x) * Constants::FIXED_TO_FLOAT;
    float dy = (targetEntry.position.y - attackerEntry.position.y) * Constants::FIXED_TO_FLOAT;
    float dz = (targetEntry.position.z - attackerEntry.position.z) * Constants::FIXED_TO_FLOAT;
    float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    // Check melee range (2.5m default)
    const float MELEE_RANGE = combatSystem_.getConfig().meleeRange;
    if (dist > MELEE_RANGE) {
        return false;  // Too far at that time
    }
    
    // Check angle (target must be in front of attacker)
    // Note: We don't store rotation history, so we use current rotation with tolerance
    // In a full implementation, we'd store rotation history too
    const Rotation* attackerRot = registry.try_get<Rotation>(attacker);
    if (attackerRot) {
        float yaw = attackerRot->yaw;
        glm::vec3 attackerForward = getForwardVector(yaw);
        glm::vec3 toTarget(dx, 0.0f, dz);  // Ignore Y for horizontal angle
        
        float toTargetLen = std::sqrt(dx*dx + dz*dz);
        if (toTargetLen > 0.001f) {
            toTarget /= toTargetLen;
            float dotProduct = glm::dot(attackerForward, toTarget);
            // Clamp to avoid acos domain errors
            dotProduct = std::max(-1.0f, std::min(1.0f, dotProduct));
            float angle = std::acos(dotProduct) * 180.0f / 3.14159265359f;
            
            // Check against melee angle (60 degree cone, so 30 degrees each side)
            const float MELEE_HALF_ANGLE = combatSystem_.getConfig().meleeAngle / 2.0f;
            if (angle > MELEE_HALF_ANGLE) {
                return false;  // Target not in front cone
            }
        }
    }
    
    return true;
}

bool LagCompensatedCombat::validateRangedHitAtTime(Registry& registry, EntityID attacker,
                                                  EntityID target, const glm::vec3& aimDir,
                                                  uint32_t targetTimestamp) {
    // Get historical positions for both attacker and target
    PositionHistoryEntry attackerEntry, targetEntry;
    
    if (!lagCompensator_.getHistoricalPosition(attacker, targetTimestamp, attackerEntry)) {
        return false;  // No attacker history
    }
    
    if (!lagCompensator_.getHistoricalPosition(target, targetTimestamp, targetEntry)) {
        return false;  // No target history
    }
    
    // Convert to float vectors
    glm::vec3 attackerPos(
        attackerEntry.position.x * Constants::FIXED_TO_FLOAT,
        attackerEntry.position.y * Constants::FIXED_TO_FLOAT,
        attackerEntry.position.z * Constants::FIXED_TO_FLOAT
    );
    
    glm::vec3 targetPos(
        targetEntry.position.x * Constants::FIXED_TO_FLOAT,
        targetEntry.position.y * Constants::FIXED_TO_FLOAT,
        targetEntry.position.z * Constants::FIXED_TO_FLOAT
    );
    
    // Validate aim direction (should be normalized)
    glm::vec3 normalizedAim = aimDir;
    if (glm::length(normalizedAim) < 0.001f) {
        return false;  // Invalid aim direction
    }
    normalizedAim = glm::normalize(normalizedAim);
    
    // Ray-sphere intersection
    // Ray: P = attackerPos + t * aimDir
    // Sphere: |P - targetPos|^2 = r^2
    // Solve for t
    
    glm::vec3 oc = attackerPos - targetPos;
    float a = glm::dot(normalizedAim, normalizedAim);  // Should be 1.0 if normalized
    float b = 2.0f * glm::dot(oc, normalizedAim);
    float c = glm::dot(oc, oc) - (0.5f * 0.5f);  // 0.5m radius for player collision
    
    float discriminant = b*b - 4*a*c;
    
    if (discriminant < 0) {
        return false;  // No intersection - ray misses target sphere
    }
    
    // Calculate intersection distance
    float sqrtDisc = std::sqrt(discriminant);
    float t1 = (-b - sqrtDisc) / (2.0f * a);
    float t2 = (-b + sqrtDisc) / (2.0f * a);
    
    // We need intersection in front of attacker (t > 0)
    // Either intersection point is valid
    if (t1 >= 0 || t2 >= 0) {
        return true;
    }
    
    // Both intersections are behind attacker
    return false;
}

glm::vec3 LagCompensatedCombat::getForwardVector(float yaw) const {
    // Yaw: 0 = +Z, PI/2 = +X
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

bool LagCompensatedCombat::isValidTarget(Registry& registry, EntityID attacker, EntityID target,
                                        uint32_t currentTimeMs) const {
    // Check if target exists
    if (!registry.valid(target)) {
        return false;
    }
    
    // Skip self
    if (target == attacker) {
        return false;
    }
    
    // Check combat state
    const CombatState* combat = registry.try_get<CombatState>(target);
    if (!combat) {
        return false;  // No combat component
    }
    
    // Skip dead targets
    if (combat->isDead) {
        return false;
    }
    
    return true;
}

} // namespace DarkAges
