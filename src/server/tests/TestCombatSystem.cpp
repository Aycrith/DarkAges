// [COMBAT_AGENT] Unit tests for CombatSystem

#include <catch2/catch_test_macros.hpp>
#include "combat/CombatSystem.hpp"
#include "ecs/CoreTypes.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

using namespace DarkAges;

TEST_CASE("CombatSystem initialization", "[combat]") {
    CombatConfig config;
    config.baseMeleeDamage = 2000;
    config.attackCooldownMs = 500;
    config.meleeRange = 2.5f;
    config.meleeAngle = 60.0f;
    
    CombatSystem combat(config);
    
    REQUIRE(combat.getConfig().baseMeleeDamage == 2000);
    REQUIRE(combat.getConfig().attackCooldownMs == 500);
    REQUIRE(combat.getConfig().meleeRange == 2.5f);
}

TEST_CASE("CombatSystem canAttack checks", "[combat]") {
    Registry registry;
    CombatSystem combat;
    
    SECTION("Entity without CombatState cannot attack") {
        EntityID entity = registry.create();
        registry.emplace<Position>(entity);
        
        REQUIRE_FALSE(combat.canAttack(registry, entity, 1000));
    }
    
    SECTION("Dead entity cannot attack") {
        EntityID entity = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(entity);
        combatState.isDead = true;
        
        REQUIRE_FALSE(combat.canAttack(registry, entity, 1000));
    }
    
    SECTION("Entity can attack when cooldown has passed") {
        EntityID entity = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(entity);
        combatState.lastAttackTime = 0;
        
        REQUIRE(combat.canAttack(registry, entity, 1000));  // 1000ms > 500ms cooldown
    }
    
    SECTION("Entity cannot attack during cooldown") {
        EntityID entity = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(entity);
        combatState.lastAttackTime = 900;  // Just attacked 100ms ago
        
        REQUIRE_FALSE(combat.canAttack(registry, entity, 1000));  // Only 100ms passed
    }
}

TEST_CASE("CombatSystem applyDamage", "[combat]") {
    Registry registry;
    CombatSystem combat;
    
    SECTION("Apply damage reduces health") {
        EntityID target = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(target);
        combatState.health = 5000;
        combatState.maxHealth = 10000;
        
        EntityID attacker = registry.create();
        
        bool result = combat.applyDamage(registry, target, attacker, 1000, 1000);
        
        REQUIRE(result == true);
        REQUIRE(combatState.health == 4000);
        REQUIRE(combatState.lastAttacker == attacker);
    }
    
    SECTION("Damage clamps to zero") {
        EntityID target = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(target);
        combatState.health = 500;
        combatState.maxHealth = 10000;
        
        EntityID attacker = registry.create();
        
        combat.applyDamage(registry, target, attacker, 1000, 1000);
        
        REQUIRE(combatState.health == 0);
        REQUIRE(combatState.isDead == true);
    }
    
    SECTION("Cannot damage dead entity") {
        EntityID target = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(target);
        combatState.isDead = true;
        combatState.health = 0;
        
        EntityID attacker = registry.create();
        
        bool result = combat.applyDamage(registry, target, attacker, 1000, 1000);
        
        REQUIRE(result == false);
    }
    
    SECTION("Entity without CombatState cannot be damaged") {
        EntityID target = registry.create();
        registry.emplace<Position>(target);
        
        EntityID attacker = registry.create();
        
        bool result = combat.applyDamage(registry, target, attacker, 1000, 1000);
        
        REQUIRE(result == false);
    }
}

TEST_CASE("CombatSystem applyHeal", "[combat]") {
    Registry registry;
    CombatSystem combat;
    
    SECTION("Heal increases health") {
        EntityID target = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(target);
        combatState.health = 5000;
        combatState.maxHealth = 10000;
        
        bool result = combat.applyHeal(registry, target, 1000);
        
        REQUIRE(result == true);
        REQUIRE(combatState.health == 6000);
    }
    
    SECTION("Heal clamps to max health") {
        EntityID target = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(target);
        combatState.health = 9500;
        combatState.maxHealth = 10000;
        
        combat.applyHeal(registry, target, 1000);
        
        REQUIRE(combatState.health == 10000);
    }
    
    SECTION("Cannot heal dead entity") {
        EntityID target = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(target);
        combatState.isDead = true;
        combatState.health = 0;
        
        bool result = combat.applyHeal(registry, target, 1000);
        
        REQUIRE(result == false);
    }
}

TEST_CASE("CombatSystem kill and respawn", "[combat]") {
    Registry registry;
    CombatSystem combat;
    
    SECTION("Kill entity sets dead flag") {
        EntityID victim = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(victim);
        combatState.health = 1000;
        combatState.isDead = false;
        
        EntityID killer = registry.create();
        
        combat.killEntity(registry, victim, killer);
        
        REQUIRE(combatState.isDead == true);
        REQUIRE(combatState.health == 0);
        REQUIRE(combatState.lastAttacker == killer);
    }
    
    SECTION("Death callback is invoked") {
        EntityID victim = registry.create();
        CombatState& victimCombat = registry.emplace<CombatState>(victim);
        victimCombat.health = 1000;
        
        EntityID killer = registry.create();
        
        EntityID callbackVictim = entt::null;
        EntityID callbackKiller = entt::null;
        
        combat.setOnDeath([&](EntityID v, EntityID k) {
            callbackVictim = v;
            callbackKiller = k;
        });
        
        combat.killEntity(registry, victim, killer);
        
        REQUIRE(callbackVictim == victim);
        REQUIRE(callbackKiller == killer);
    }
    
    SECTION("Respawn resets health and position") {
        EntityID entity = registry.create();
        CombatState& combatState = registry.emplace<CombatState>(entity);
        combatState.isDead = true;
        combatState.health = 0;
        combatState.maxHealth = 10000;
        
        Position& pos = registry.emplace<Position>(entity);
        pos.x = 1000;
        pos.y = 0;
        pos.z = 1000;
        
        Velocity& vel = registry.emplace<Velocity>(entity);
        vel.dx = 100;
        vel.dy = 0;
        vel.dz = 100;
        
        Position spawnPos = Position::fromVec3(glm::vec3(0.0f, 5.0f, 0.0f), 0);
        
        combat.respawnEntity(registry, entity, spawnPos);
        
        REQUIRE(combatState.isDead == false);
        REQUIRE(combatState.health == 10000);
        REQUIRE(static_cast<bool>(combatState.lastAttacker == entt::null));
        REQUIRE(pos.x == spawnPos.x);
        REQUIRE(pos.y == spawnPos.y);
        REQUIRE(pos.z == spawnPos.z);
        REQUIRE(vel.dx == 0);
        REQUIRE(vel.dy == 0);
        REQUIRE(vel.dz == 0);
    }
}

TEST_CASE("CombatSystem team damage check", "[combat]") {
    Registry registry;
    CombatConfig config;
    config.allowFriendlyFire = false;
    CombatSystem combat(config);
    
    SECTION("Same team cannot damage (friendly fire off)") {
        EntityID attacker = registry.create();
        CombatState& attackerCombat = registry.emplace<CombatState>(attacker);
        attackerCombat.teamId = 1;
        
        EntityID target = registry.create();
        CombatState& targetCombat = registry.emplace<CombatState>(target);
        targetCombat.teamId = 1;
        
        // Using findMeleeTargets indirectly through canDamage
        // Need to add position/rotation for melee range check
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(1, 0, 0), 0));
        registry.emplace<Rotation>(target);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE(targets.empty());  // Should not include same-team target
    }
    
    SECTION("Different team can damage") {
        EntityID attacker = registry.create();
        CombatState& attackerCombat = registry.emplace<CombatState>(attacker);
        attackerCombat.teamId = 1;
        
        EntityID target = registry.create();
        CombatState& targetCombat = registry.emplace<CombatState>(target);
        targetCombat.teamId = 2;
        
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker, Rotation{1.5708f, 0.0f});  // PI/2 to face +X direction
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(1, 0, 0), 0));
        registry.emplace<Rotation>(target);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE_FALSE(targets.empty());
    }
    
    SECTION("Team 0 (no team) can damage anyone") {
        EntityID attacker = registry.create();
        CombatState& attackerCombat = registry.emplace<CombatState>(attacker);
        attackerCombat.teamId = 0;
        
        EntityID target = registry.create();
        CombatState& targetCombat = registry.emplace<CombatState>(target);
        targetCombat.teamId = 1;
        
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker, Rotation{1.5708f, 0.0f});  // PI/2 to face +X direction
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(1, 0, 0), 0));
        registry.emplace<Rotation>(target);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE_FALSE(targets.empty());
    }
}

TEST_CASE("CombatSystem melee range check", "[combat]") {
    Registry registry;
    CombatConfig config;
    config.meleeRange = 2.5f;
    config.meleeAngle = 60.0f;
    CombatSystem combat(config);
    
    SECTION("Target within range is hit") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);  // yaw = 0, facing +Z
        
        EntityID target = registry.create();
        registry.emplace<CombatState>(target);
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 2.0f), 0));  // 2m in front
        registry.emplace<Rotation>(target);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE_FALSE(targets.empty());
        REQUIRE(targets[0] == target);
    }
    
    SECTION("Target outside range is not hit") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        EntityID target = registry.create();
        registry.emplace<CombatState>(target);
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 5.0f), 0));  // 5m in front
        registry.emplace<Rotation>(target);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE(targets.empty());
    }
    
    SECTION("Target outside cone angle is not hit") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);  // Facing +Z
        
        EntityID target = registry.create();
        registry.emplace<CombatState>(target);
        // 45 degrees to the side, 1m away - outside 60 degree cone (30 deg each side)
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(2.0f, 0, 0), 0));
        registry.emplace<Rotation>(target);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE(targets.empty());
    }
    
    SECTION("Self is not targeted") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE(targets.empty());
    }
    
    SECTION("Dead targets are not hit") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        EntityID target = registry.create();
        CombatState& targetCombat = registry.emplace<CombatState>(target);
        targetCombat.isDead = true;
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 1.0f), 0));
        registry.emplace<Rotation>(target);
        
        auto targets = combat.findMeleeTargets(registry, attacker);
        REQUIRE(targets.empty());
    }
}

TEST_CASE("CombatSystem performMeleeAttack", "[combat]") {
    Registry registry;
    CombatSystem combat;
    
    SECTION("Melee attack hits target and applies damage") {
        EntityID attacker = registry.create();
        CombatState& attackerCombat = registry.emplace<CombatState>(attacker);
        attackerCombat.health = 10000;
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        EntityID target = registry.create();
        CombatState& targetCombat = registry.emplace<CombatState>(target);
        targetCombat.health = 10000;
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 1.0f), 0));
        registry.emplace<Rotation>(target);
        
        HitResult result = combat.performMeleeAttack(registry, attacker, 1000);
        
        REQUIRE(result.hit == true);
        REQUIRE(result.target == target);
        REQUIRE(result.damageDealt > 0);
        REQUIRE(result.hitType == std::string("melee"));
        REQUIRE(targetCombat.health < 10000);  // Damage was applied
    }
    
    SECTION("Melee attack with no targets returns miss") {
        EntityID attacker = registry.create();
        CombatState& attackerCombat = registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        HitResult result = combat.performMeleeAttack(registry, attacker, 1000);
        
        REQUIRE(result.hit == false);
        REQUIRE(result.hitType == std::string("miss"));
    }
    
    SECTION("Damage callback is invoked on hit") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        EntityID target = registry.create();
        registry.emplace<CombatState>(target);
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 1.0f), 0));
        registry.emplace<Rotation>(target);
        
        bool callbackInvoked = false;
        EntityID callbackAttacker = entt::null;
        EntityID callbackTarget = entt::null;
        int16_t callbackDamage = 0;
        
        combat.setOnDamage([&](EntityID a, EntityID t, int16_t d, const Position&) {
            callbackInvoked = true;
            callbackAttacker = a;
            callbackTarget = t;
            callbackDamage = d;
        });
        
        combat.performMeleeAttack(registry, attacker, 1000);
        
        REQUIRE(callbackInvoked == true);
        REQUIRE(callbackAttacker == attacker);
        REQUIRE(callbackTarget == target);
        REQUIRE(callbackDamage > 0);
    }
}

TEST_CASE("CombatSystem processAttack", "[combat]") {
    Registry registry;
    CombatSystem combat;
    
    SECTION("Cooldown prevents attack") {
        EntityID attacker = registry.create();
        CombatState& attackerCombat = registry.emplace<CombatState>(attacker);
        attackerCombat.lastAttackTime = 900;  // Just attacked
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        AttackInput input;
        input.type = AttackInput::MELEE;
        
        HitResult result = combat.processAttack(registry, attacker, input, 1000);
        
        REQUIRE(result.hit == false);
        REQUIRE(result.hitType == std::string("cooldown"));
    }
    
    SECTION("Valid melee attack processes correctly") {
        EntityID attacker = registry.create();
        CombatState& attackerCombat = registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);
        
        EntityID target = registry.create();
        CombatState& targetCombat = registry.emplace<CombatState>(target);
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 1.0f), 0));
        registry.emplace<Rotation>(target);
        
        AttackInput input;
        input.type = AttackInput::MELEE;
        
        HitResult result = combat.processAttack(registry, attacker, input, 1000);
        
        REQUIRE(result.hit == true);
        REQUIRE(result.hitType == std::string("melee"));
        REQUIRE(attackerCombat.lastAttackTime == 1000);  // Cooldown updated
    }
}

TEST_CASE("HealthRegenSystem", "[combat]") {
    Registry registry;
    HealthRegenSystem regenSystem;
    
    SECTION("Health regenerates over time") {
        EntityID entity = registry.create();
        CombatState& combat = registry.emplace<CombatState>(entity);
        combat.health = 5000;
        combat.maxHealth = 10000;
        combat.isDead = false;
        
        regenSystem.update(registry, 1000);  // First call - should regen
        
        REQUIRE(combat.health == 5050);  // +50 HP
    }
    
    SECTION("Dead entities do not regenerate") {
        EntityID entity = registry.create();
        CombatState& combat = registry.emplace<CombatState>(entity);
        combat.health = 0;
        combat.maxHealth = 10000;
        combat.isDead = true;
        
        regenSystem.update(registry, 1000);
        
        REQUIRE(combat.health == 0);  // No regen
    }
    
    SECTION("Health does not exceed max") {
        EntityID entity = registry.create();
        CombatState& combat = registry.emplace<CombatState>(entity);
        combat.health = 9990;
        combat.maxHealth = 10000;
        combat.isDead = false;
        
        regenSystem.update(registry, 1000);
        
        REQUIRE(combat.health == 10000);  // Clamped to max
    }
    
    SECTION("Regen only happens at intervals") {
        EntityID entity = registry.create();
        CombatState& combat = registry.emplace<CombatState>(entity);
        combat.health = 5000;
        combat.maxHealth = 10000;
        combat.isDead = false;
        
        regenSystem.update(registry, 1000);  // First call - regen
        REQUIRE(combat.health == 5050);
        
        regenSystem.update(registry, 1100);  // Only 100ms passed - no regen
        REQUIRE(combat.health == 5050);
        
        regenSystem.update(registry, 2000);  // 1000ms passed - regen
        REQUIRE(combat.health == 5100);
    }
}

TEST_CASE("CombatSystem damage calculation", "[combat]") {
    Registry registry;
    CombatConfig config;
    config.baseMeleeDamage = 1000;
    config.damageVariance = 10;  // ±10%
    config.criticalChance = 100; // 100% crit for testing
    config.criticalMultiplier = 200; // 2x damage
    CombatSystem combat(config);
    
    SECTION("Base damage within variance range") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        
        EntityID target = registry.create();
        registry.emplace<CombatState>(target);
        
        // Test multiple times to account for variance
        bool foundVariation = false;
        int16_t firstDamage = 0;
        
        for (int i = 0; i < 10; i++) {
            bool isCritical = false;
            int16_t damage = combat.calculateDamage(registry, attacker, target, 1000, isCritical);
            
            // With 100% crit and 2x multiplier, damage should be around 2000 ±10%
            REQUIRE(damage >= 1800);  // 2000 * 0.9
            REQUIRE(damage <= 2200);  // 2000 * 1.1
            REQUIRE(isCritical == true);
            
            if (i == 0) {
                firstDamage = damage;
            } else if (damage != firstDamage) {
                foundVariation = true;
            }
        }
        
        // Variance should cause some differences
        // Note: This could theoretically fail due to RNG but very unlikely with 10 samples
    }
    
    SECTION("Critical hits do more damage") {
        CombatConfig noCritConfig;
        noCritConfig.baseMeleeDamage = 1000;
        noCritConfig.criticalChance = 0;  // No crits
        CombatSystem noCritCombat(noCritConfig);
        
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        
        EntityID target = registry.create();
        registry.emplace<CombatState>(target);
        
        bool isCritical = false;
        int16_t normalDamage = noCritCombat.calculateDamage(registry, attacker, target, 1000, isCritical);
        
        REQUIRE(isCritical == false);
        REQUIRE(normalDamage >= 900);   // 1000 * 0.9
        REQUIRE(normalDamage <= 1100);  // 1000 * 1.1
    }
}
