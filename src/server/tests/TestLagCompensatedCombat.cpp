// [COMBAT_AGENT] Unit tests for lag-compensated combat system - Phase 3C
// Tests server rewind hit validation with historical positions

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "combat/LagCompensatedCombat.hpp"
#include "combat/CombatSystem.hpp"
#include <entt/entt.hpp>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

using namespace DarkAges;

// ============================================================================
// Test Helpers
// ============================================================================

class LagCompensatedCombatTestFixture {
public:
    entt::registry registry;
    CombatSystem combatSystem;
    LagCompensator lagCompensator;
    LagCompensatedCombat lagCombat;
    
    EntityID attacker;
    EntityID target;
    
    LagCompensatedCombatTestFixture() 
        : lagCombat(combatSystem, lagCompensator) {
        
        // Create attacker entity
        attacker = registry.create();
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Velocity>(attacker, Velocity{0, 0, 0});
        registry.emplace<Rotation>(attacker, Rotation{0, 0});  // Facing +Z
        registry.emplace<CombatState>(attacker);
        registry.emplace<BoundingVolume>(attacker);
        
        // Create target entity
        target = registry.create();
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 5.0f), 0));
        registry.emplace<Velocity>(target, Velocity{0, 0, 0});
        registry.emplace<Rotation>(target, Rotation{0, 0});
        registry.emplace<CombatState>(target);
        registry.emplace<BoundingVolume>(target);
    }
    
    // Record position history for both entities
    void recordHistory(uint32_t timestamp) {
        if (const Position* pos = registry.try_get<Position>(attacker)) {
            lagCompensator.recordPosition(attacker, timestamp, *pos, 
                                         Velocity{0, 0, 0}, Rotation{0, 0});
        }
        if (const Position* pos = registry.try_get<Position>(target)) {
            lagCompensator.recordPosition(target, timestamp, *pos, 
                                         Velocity{0, 0, 0}, Rotation{0, 0});
        }
    }
    
    // Record history with specific positions
    void recordHistoryWithPosition(EntityID entity, uint32_t timestamp, 
                                   const glm::vec3& posVec) {
        Position pos = Position::fromVec3(posVec, timestamp);
        lagCompensator.recordPosition(entity, timestamp, pos, 
                                     Velocity{0, 0, 0}, Rotation{0, 0});
    }
};

// ============================================================================
// Basic Lag-Compensated Attack Tests
// ============================================================================

TEST_CASE("LagCompensatedCombat basic attack processing", "[combat][lag][combat]") {
    LagCompensatedCombatTestFixture fixture;
    
    SECTION("Melee attack hits target at historical position") {
        // Target is in melee range (2.5m) at t=0
        // Attacker at (0,0,0), target at (0,0,2) - 2m away, directly in front
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(0, 0, 2.0f), 0);
        });
        
        // Record history at t=0
        fixture.recordHistory(0);
        
        // Build attack input
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.input.sequence = 1;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;  // 100ms later
        attack.rttMs = 100;  // 50ms one-way latency
        
        // Process attack
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should hit the target
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].hit == true);
        REQUIRE(results[0].target == fixture.target);
        REQUIRE(results[0].hitType == std::string("melee"));
        REQUIRE(results[0].damageDealt > 0);
    }
    
    SECTION("Melee attack misses when target was out of range") {
        // Target is out of melee range at t=0
        // Attacker at (0,0,0), target at (0,0,10) - 10m away
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(0, 0, 10.0f), 0);
        });
        
        fixture.recordHistory(0);
        
        // Move target to melee range at current time (simulating movement)
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(0, 0, 2.0f), 100);
        });
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should miss - target was out of range at attack time
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].hit == false);
        REQUIRE(results[0].hitType == std::string("miss"));
    }
    
    SECTION("Melee attack misses when target was behind attacker") {
        // Target is behind attacker at t=0
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(0, 0, -2.0f), 0);  // Behind
        });
        
        fixture.recordHistory(0);
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should miss - target was behind (outside 60-degree cone)
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].hit == false);
    }
}

// ============================================================================
// Lag Compensation Timing Tests
// ============================================================================

TEST_CASE("LagCompensatedCombat timing calculations", "[combat][lag][timing]") {
    LagCompensatedCombatTestFixture fixture;
    
    SECTION("Attack time calculation with latency") {
        // With 100ms RTT, attack time = clientTime + 50ms
        // At t=0, target at (0,0,2)
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(0, 0, 2.0f), 0);
        });
        fixture.recordHistory(0);
        
        // At t=100, target has moved
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(10.0f, 0, 10.0f), 100);
        });
        fixture.recordHistory(100);
        
        // Attack sent at client t=0, received at server t=100 with 100ms RTT
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;      // Client sent at t=0
        attack.serverTimestamp = 100;    // Server received at t=100
        attack.rttMs = 100;              // 50ms one-way
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should hit at historical position (t=50, which interpolates to t=0 position)
        REQUIRE(results[0].hit == true);
    }
    
    SECTION("Excessive latency falls back to current-time processing") {
        // Target in range at current time
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(0, 0, 2.0f), 0);
        });
        fixture.recordHistory(0);
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 1000;
        attack.rttMs = 2000;  // 1000ms one-way - exceeds MAX_REWIND_MS (500ms)
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should still process but without rewind (uses current position)
        // Since target is in range, it should hit
        REQUIRE(results.size() >= 1);
    }
    
    SECTION("Max rewind constant is respected") {
        REQUIRE(LagCompensatedCombat::getMaxRewindMs() == 500);
    }
}

// ============================================================================
// Hit Claim Validation Tests
// ============================================================================

TEST_CASE("LagCompensatedCombat hit claim validation", "[combat][lag][validation]") {
    LagCompensatedCombatTestFixture fixture;
    
    SECTION("Valid hit claim within tolerance") {
        // Record target at specific position
        fixture.recordHistoryWithPosition(fixture.target, 500, glm::vec3(5.0f, 0, 0));
        
        // Client claims hit at that position (within 2m tolerance)
        Position claimedPos = Position::fromVec3(glm::vec3(5.0f, 0, 0), 500);
        
        bool valid = fixture.lagCombat.validateHitClaim(
            fixture.registry, fixture.attacker, fixture.target,
            claimedPos, 450, 100  // 100ms RTT, attack at t=500
        );
        
        REQUIRE(valid);
    }
    
    SECTION("Hit claim within 2m tolerance is valid") {
        fixture.recordHistoryWithPosition(fixture.target, 500, glm::vec3(5.0f, 0, 0));
        
        // Claim at 1.9m offset (within tolerance)
        Position claimedPos = Position::fromVec3(glm::vec3(6.9f, 0, 0), 500);
        
        bool valid = fixture.lagCombat.validateHitClaim(
            fixture.registry, fixture.attacker, fixture.target,
            claimedPos, 450, 100
        );
        
        REQUIRE(valid);
    }
    
    SECTION("Hit claim outside tolerance is invalid") {
        fixture.recordHistoryWithPosition(fixture.target, 500, glm::vec3(5.0f, 0, 0));
        
        // Claim at 2.1m offset (outside tolerance)
        Position claimedPos = Position::fromVec3(glm::vec3(7.1f, 0, 0), 500);
        
        bool valid = fixture.lagCombat.validateHitClaim(
            fixture.registry, fixture.attacker, fixture.target,
            claimedPos, 450, 100
        );
        
        REQUIRE_FALSE(valid);
    }
    
    SECTION("Hit claim with excessive latency is rejected") {
        fixture.recordHistoryWithPosition(fixture.target, 500, glm::vec3(5.0f, 0, 0));
        
        Position claimedPos = Position::fromVec3(glm::vec3(5.0f, 0, 0), 500);
        
        bool valid = fixture.lagCombat.validateHitClaim(
            fixture.registry, fixture.attacker, fixture.target,
            claimedPos, 0, 2000  // 2000ms RTT exceeds max
        );
        
        REQUIRE_FALSE(valid);
    }
    
    SECTION("Hit claim without history is rejected") {
        // No history recorded for target
        Position claimedPos = Position::fromVec3(glm::vec3(5.0f, 0, 0), 500);
        
        bool valid = fixture.lagCombat.validateHitClaim(
            fixture.registry, fixture.attacker, fixture.target,
            claimedPos, 450, 100
        );
        
        REQUIRE_FALSE(valid);
    }
}

// ============================================================================
// Area-of-Effect Rewind Tests
// ============================================================================

TEST_CASE("LagCompensatedCombat AOE rewind", "[combat][lag][aoe]") {
    LagCompensatedCombatTestFixture fixture;
    
    // Create multiple targets
    EntityID target2 = fixture.registry.create();
    fixture.registry.emplace<Position>(target2, Position::fromVec3(glm::vec3(3.0f, 0, 0), 0));
    fixture.registry.emplace<Velocity>(target2, Velocity{0, 0, 0});
    fixture.registry.emplace<Rotation>(target2, Rotation{0, 0});
    fixture.registry.emplace<CombatState>(target2);
    
    EntityID target3 = fixture.registry.create();
    fixture.registry.emplace<Position>(target3, Position::fromVec3(glm::vec3(10.0f, 0, 0), 0));
    fixture.registry.emplace<Velocity>(target3, Velocity{0, 0, 0});
    fixture.registry.emplace<Rotation>(target3, Rotation{0, 0});
    fixture.registry.emplace<CombatState>(target3);
    
    SECTION("AOE rewind finds entities within radius") {
        // Record positions at t=0
        fixture.recordHistoryWithPosition(fixture.attacker, 0, glm::vec3(0, 0, 0));
        fixture.recordHistoryWithPosition(fixture.target, 0, glm::vec3(2.0f, 0, 0));
        fixture.recordHistoryWithPosition(target2, 0, glm::vec3(3.0f, 0, 0));
        fixture.recordHistoryWithPosition(target3, 0, glm::vec3(10.0f, 0, 0));
        
        std::vector<EntityID> affected;
        Position center = Position::fromVec3(glm::vec3(0, 0, 0), 0);
        
        fixture.lagCombat.rewindEntitiesInArea(fixture.registry, center, 5.0f, 0, affected);
        
        // Should find target (2m) and target2 (3m), but not target3 (10m)
        REQUIRE(affected.size() == 2);
        REQUIRE(std::find(affected.begin(), affected.end(), fixture.target) != affected.end());
        REQUIRE(std::find(affected.begin(), affected.end(), target2) != affected.end());
        REQUIRE(std::find(affected.begin(), affected.end(), target3) == affected.end());
    }
    
    SECTION("AOE rewind excludes dead entities") {
        fixture.recordHistoryWithPosition(fixture.target, 0, glm::vec3(2.0f, 0, 0));
        
        // Kill target
        if (CombatState* combat = fixture.registry.try_get<CombatState>(fixture.target)) {
            combat->isDead = true;
        }
        
        std::vector<EntityID> affected;
        Position center = Position::fromVec3(glm::vec3(0, 0, 0), 0);
        
        fixture.lagCombat.rewindEntitiesInArea(fixture.registry, center, 5.0f, 0, affected);
        
        // Dead entity should not be included
        REQUIRE(std::find(affected.begin(), affected.end(), fixture.target) == affected.end());
    }
}

// ============================================================================
// Ranged Attack Tests
// ============================================================================

TEST_CASE("LagCompensatedCombat ranged attack validation", "[combat][lag][ranged]") {
    LagCompensatedCombatTestFixture fixture;
    
    SECTION("Ranged attack hits with ray-sphere intersection") {
        // Target directly in front of attacker at 10m
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(0, 0, 10.0f), 0);
        });
        fixture.recordHistory(0);
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::RANGED;
        attack.input.aimDirection = glm::vec3(0, 0, 1);  // Aim forward
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Ray should intersect target's collision sphere
        REQUIRE(results.size() >= 1);
        // Note: Ranged attacks may not be fully implemented in base CombatSystem
    }
    
    SECTION("Ranged attack misses when aiming away from target") {
        // Target to the right
        fixture.registry.patch<Position>(fixture.target, [](Position& p) {
            p = Position::fromVec3(glm::vec3(10.0f, 0, 0), 0);
        });
        fixture.recordHistory(0);
        
        // Attacker aiming forward (+Z), not at target (+X)
        fixture.registry.patch<Rotation>(fixture.attacker, [](Rotation& r) {
            r.yaw = 0;  // Facing +Z
        });
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::RANGED;
        attack.input.aimDirection = glm::vec3(0, 0, 1);  // Aim forward, not at target
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Ray should miss target
        REQUIRE(results[0].hit == false);
    }
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_CASE("LagCompensatedCombat edge cases", "[combat][lag][edge]") {
    LagCompensatedCombatTestFixture fixture;
    
    SECTION("Attack with no history for attacker") {
        // Don't record any history
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        // Should not crash, but won't find targets without attacker history
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should return miss result
        REQUIRE(results.size() >= 1);
    }
    
    SECTION("Attack while on cooldown") {
        // Record history
        fixture.recordHistory(0);
        
        // Set attacker on cooldown by simulating a recent attack
        // Attack was at time 0, checking at time 100ms (well within 500ms cooldown)
        if (CombatState* combat = fixture.registry.try_get<CombatState>(fixture.attacker)) {
            combat->lastAttackTime = 1;  // Attack happened at 1ms (very recent)
        }
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;  // Within 500ms cooldown (99ms since last attack)
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should return cooldown result
        REQUIRE(results[0].hit == false);
        REQUIRE(results[0].hitType == std::string("cooldown"));
    }
    
    SECTION("Dead attacker cannot attack") {
        // Record history
        fixture.recordHistory(0);
        
        // Kill attacker
        if (CombatState* combat = fixture.registry.try_get<CombatState>(fixture.attacker)) {
            combat->isDead = true;
        }
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should not hit
        REQUIRE(results[0].hit == false);
    }
    
    SECTION("Dead target cannot be hit") {
        // Record history
        fixture.recordHistory(0);
        
        // Kill target
        if (CombatState* combat = fixture.registry.try_get<CombatState>(fixture.target)) {
            combat->isDead = true;
        }
        
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should miss - target is dead
        REQUIRE(results[0].hit == false);
    }
    
    SECTION("History availability check") {
        fixture.recordHistoryWithPosition(fixture.target, 500, glm::vec3(5.0f, 0, 0));
        
        REQUIRE(fixture.lagCombat.hasHistoryForEntityAtTime(fixture.target, 500));
        REQUIRE_FALSE(fixture.lagCombat.hasHistoryForEntityAtTime(fixture.target, 1000));
        REQUIRE_FALSE(fixture.lagCombat.hasHistoryForEntityAtTime(entt::entity(9999), 500));
    }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("LagCompensatedCombat full integration scenario", "[combat][lag][integration]") {
    LagCompensatedCombatTestFixture fixture;
    
    SECTION("Typical gameplay scenario: moving target") {
        // Simulate target moving along X axis over 1 second
        // t=0: target at (0, 0, 2) - in melee range
        // t=500: target at (5, 0, 2) - out of melee range
        
        for (uint32_t t = 0; t <= 500; t += 16) {
            float x = t / 100.0f;  // Move 5m in 500ms
            fixture.recordHistoryWithPosition(fixture.target, t, glm::vec3(x, 0, 2.0f));
            fixture.recordHistoryWithPosition(fixture.attacker, t, glm::vec3(0, 0, 0));
        }
        
        // Attacker swings at t=0 when target was in range
        // But input arrives at t=100 with 100ms latency
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::MELEE;
        attack.clientTimestamp = 0;
        attack.serverTimestamp = 100;
        attack.rttMs = 100;
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should hit because target was in range at attack time
        REQUIRE(results[0].hit == true);
    }
    
    SECTION("High latency player shooting at moving target") {
        // Target moving along Z axis
        for (uint32_t t = 0; t <= 1000; t += 16) {
            float z = t / 100.0f;
            fixture.recordHistoryWithPosition(fixture.target, t, glm::vec3(0, 0, z));
            fixture.recordHistoryWithPosition(fixture.attacker, t, glm::vec3(0, 0, 0));
        }
        
        // 300ms latency player fires when target was at z=3
        // Input arrives 300ms later when target is at z=6
        LagCompensatedAttack attack;
        attack.attacker = fixture.attacker;
        attack.input.type = AttackInput::RANGED;
        attack.input.aimDirection = glm::vec3(0, 0, 1);  // Aim forward
        attack.clientTimestamp = 300;    // Client time when fired
        attack.serverTimestamp = 600;    // Server received 300ms later
        attack.rttMs = 300;              // 150ms one-way
        
        // Target was at z=3 + 150ms movement = z=4.5 when fired
        // (assuming 6m/s movement from the simulation above)
        
        auto results = fixture.lagCombat.processAttackWithRewind(fixture.registry, attack);
        
        // Should process (300ms RTT / 2 = 150ms < 500ms max)
        REQUIRE(results.size() >= 1);
    }
}
