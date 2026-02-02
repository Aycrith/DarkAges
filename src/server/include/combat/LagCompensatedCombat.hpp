#pragma once

#include "combat/CombatSystem.hpp"
#include "combat/PositionHistory.hpp"
#include "ecs/CoreTypes.hpp"
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// [COMBAT_AGENT] Lag-compensated combat system - Phase 3C
// Integrates lag compensation with combat system to validate hits at historical positions.
// When a player attacks, the server rewinds all potential targets to their positions
// at the time the attack was initiated (accounting for attacker's latency).

namespace DarkAges {

// ============================================================================
// Attack request with timing info for lag compensation
// ============================================================================

// [COMBAT_AGENT] Attack request with timing info for lag compensation
struct LagCompensatedAttack {
    EntityID attacker;
    AttackInput input;
    uint32_t clientTimestamp;    // When client sent the attack
    uint32_t serverTimestamp;    // When server received it
    uint32_t rttMs;              // Attacker's ping (round-trip time)
};

// ============================================================================
// Lag-Compensated Combat System
// ============================================================================

// [COMBAT_AGENT] Lag-compensated combat system
// Validates hits against historical positions using the lag compensator
// [COMBAT_AGENT] Contract for NetworkAgent:
// - Must provide accurate RTT measurements in LagCompensatedAttack::rttMs
// - Must provide client timestamps for accurate rewind calculation
// - Must not process attacks with RTT > MAX_REWIND_MS without fallback
class LagCompensatedCombat {
public:
    LagCompensatedCombat(CombatSystem& combat, LagCompensator& lagComp);
    
    // Process an attack with lag compensation
    // This rewinds targets to their positions when the attack was initiated
    // [COMBAT_AGENT] Process flow:
    // 1. Calculate attack time (client time + one-way latency)
    // 2. Find potential targets at historical positions
    // 3. Validate each hit against historical position
    // 4. Apply damage at CURRENT time (damage happens now, not in past)
    std::vector<HitResult> processAttackWithRewind(Registry& registry,
                                                   const LagCompensatedAttack& attack);
    
    // Validate a specific hit claim from client
    // Client says "I hit entity X at position Y"
    // Server validates: "Was X at position Y at the time you fired?"
    // [COMBAT_AGENT] Returns true if claimed position matches historical position
    // within tolerance (2 meters default)
    bool validateHitClaim(Registry& registry, EntityID attacker, EntityID claimedTarget,
                         const Position& claimedPosition, uint32_t attackTimestamp,
                         uint32_t rttMs);
    
    // Perform full rewind for all entities in an area
    // Used for area-of-effect attacks
    // [COMBAT_AGENT] Contract for Ability System:
    // - Call this before processing AOE damage
    // - Pass affected entities to applyDamage() individually
    // - optionally exclude an entity (typically the caster) using excludeEntity parameter
    void rewindEntitiesInArea(Registry& registry, const Position& center, float radius,
                             uint32_t targetTimestamp,
                             std::vector<EntityID>& outAffectedEntities,
                             EntityID excludeEntity = entt::null);
    
    // Check if we have position history for an entity at a given time
    // Useful for debugging and logging
    bool hasHistoryForEntityAtTime(EntityID entity, uint32_t timestamp) const;
    
    // Get the maximum allowed rewind time in milliseconds
    static uint32_t getMaxRewindMs() { return LagCompensator::getMaxRewindMs(); }

private:
    // Calculate server timestamp when attack was initiated
    // attackTime = clientTimestamp + (rttMs / 2)
    uint32_t calculateAttackTime(uint32_t clientTimestamp, uint32_t rttMs) const;
    
    // Check if rewind is within acceptable limits
    // Rejects rewinds > MAX_REWIND_MS (500ms)
    bool isRewindValid(uint32_t rttMs) const;
    
    // Find valid targets at historical positions
    // Only returns entities within AOI_RADIUS_NEAR at the historical time
    std::vector<EntityID> findHistoricalTargets(Registry& registry, EntityID attacker,
                                               uint32_t targetTimestamp);
    
    // Validate melee hit against historical position
    // Checks range (2.5m) and angle (60 degree cone) at historical time
    bool validateMeleeHitAtTime(Registry& registry, EntityID attacker, EntityID target,
                               uint32_t targetTimestamp);
    
    // Validate ranged hit (ray-sphere intersection) against historical position
    // Uses 0.5m collision radius for target
    bool validateRangedHitAtTime(Registry& registry, EntityID attacker, EntityID target,
                                const glm::vec3& aimDir, uint32_t targetTimestamp);
    
    // Get forward vector from yaw angle
    glm::vec3 getForwardVector(float yaw) const;
    
    // Check if target is valid (not dead, not self, can be damaged)
    bool isValidTarget(Registry& registry, EntityID attacker, EntityID target,
                      uint32_t currentTimeMs) const;

private:
    CombatSystem& combatSystem_;
    LagCompensator& lagCompensator_;
};

} // namespace DarkAges
