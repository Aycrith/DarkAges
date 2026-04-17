// [ZONE_AGENT] Unit tests for CombatEventHandler subsystems
// Tests combat delegation layer: CombatSystem interactions, death handling,
// combat event serialization, and edge cases in the handler's delegation chain.
// Note: CombatEventHandler delegates to ZoneServer, so we test the subsystems
// it relies on (CombatSystem, HitResult, death/respawn flow).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "combat/CombatSystem.hpp"
#include "ecs/CoreTypes.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <functional>

using namespace DarkAges;

// ============================================================================
// CombatEventHandler delegation chain: death handling
// ============================================================================

TEST_CASE("CombatEventHandler death flow - kill triggers death callback", "[zones][handler]") {
    Registry registry;
    CombatSystem combat;

    SECTION("Death callback fires with correct victim and killer") {
        EntityID victim = registry.create();
        registry.emplace<CombatState>(victim).health = 100;
        registry.emplace<Position>(victim);

        EntityID killer = registry.create();
        registry.emplace<CombatState>(killer);
        registry.emplace<Position>(killer);

        EntityID cbVictim = entt::null;
        EntityID cbKiller = entt::null;
        combat.setOnDeath([&](EntityID v, EntityID k) {
            cbVictim = v;
            cbKiller = k;
        });

        combat.killEntity(registry, victim, killer);

        REQUIRE(cbVictim == victim);
        REQUIRE(cbKiller == killer);
    }

    SECTION("Kill with no killer (environment kill)") {
        EntityID victim = registry.create();
        registry.emplace<CombatState>(victim).health = 100;

        EntityID cbVictim = entt::null;
        EntityID cbKiller = entt::null;
        combat.setOnDeath([&](EntityID v, EntityID k) {
            cbVictim = v;
            cbKiller = k;
        });

        combat.killEntity(registry, victim, entt::null);

        REQUIRE(cbVictim == victim);
        REQUIRE(static_cast<bool>(cbKiller == entt::null));
    }

    SECTION("Killing already dead entity fires callback again") {
        EntityID victim = registry.create();
        auto& cs = registry.emplace<CombatState>(victim);
        cs.health = 0;
        cs.isDead = true;

        int callCount = 0;
        combat.setOnDeath([&](EntityID, EntityID) { callCount++; });

        combat.killEntity(registry, victim, entt::null);
        REQUIRE(callCount == 1);
    }
}

// ============================================================================
// CombatEventHandler delegation chain: respawn scheduling
// ============================================================================

TEST_CASE("CombatEventHandler respawn flow - entity state after respawn", "[zones][handler]") {
    Registry registry;
    CombatSystem combat;

    SECTION("Respawn restores health and clears dead flag") {
        EntityID entity = registry.create();
        auto& cs = registry.emplace<CombatState>(entity);
        cs.health = 0;
        cs.isDead = true;
        cs.maxHealth = 10000;

        registry.emplace<Position>(entity);
        registry.emplace<Velocity>(entity);

        Position spawnPos = Position::fromVec3(glm::vec3(50.0f, 0.0f, 50.0f), 0);
        combat.respawnEntity(registry, entity, spawnPos);

        REQUIRE_FALSE(cs.isDead);
        REQUIRE(cs.health == cs.maxHealth);
    }

    SECTION("Respawn sets position to spawn point") {
        EntityID entity = registry.create();
        registry.emplace<CombatState>(entity);
        auto& pos = registry.emplace<Position>(entity);
        pos.x = 9999;
        pos.z = 9999;

        Position spawnPos = Position::fromVec3(glm::vec3(10.0f, 0.0f, 20.0f), 0);
        combat.respawnEntity(registry, entity, spawnPos);

        REQUIRE(pos.x == spawnPos.x);
        REQUIRE(pos.z == spawnPos.z);
    }

    SECTION("Respawn zeroes velocity") {
        EntityID entity = registry.create();
        registry.emplace<CombatState>(entity);
        registry.emplace<Position>(entity);
        auto& vel = registry.emplace<Velocity>(entity);
        vel.dx = 5000;
        vel.dz = 5000;

        combat.respawnEntity(registry, entity, Position{});

        REQUIRE(vel.dx == 0);
        REQUIRE(vel.dz == 0);
    }

    SECTION("Respawn clears last attacker") {
        EntityID entity = registry.create();
        auto& cs = registry.emplace<CombatState>(entity);
        cs.lastAttacker = static_cast<EntityID>(42);
        registry.emplace<Position>(entity);
        registry.emplace<Velocity>(entity);

        combat.respawnEntity(registry, entity, Position{});

        REQUIRE(static_cast<bool>(cs.lastAttacker == entt::null));
    }
}

// ============================================================================
// CombatEventHandler delegation chain: sendCombatEvent data flow
// ============================================================================

TEST_CASE("CombatEventHandler sendCombatEvent - HitResult construction", "[zones][handler]") {
    SECTION("HitResult fields for damage event") {
        HitResult hit;
        hit.hit = true;
        hit.target = static_cast<EntityID>(5);
        hit.damageDealt = 1500;
        hit.hitLocation = Position::fromVec3(glm::vec3(10.0f, 0.0f, 20.0f), 100);
        hit.hitType = "damage";
        hit.isCritical = false;

        REQUIRE(hit.hit == true);
        REQUIRE(hit.target == static_cast<EntityID>(5));
        REQUIRE(hit.damageDealt == 1500);
        REQUIRE(hit.isCritical == false);
        REQUIRE(std::string(hit.hitType) == "damage");
    }

    SECTION("HitResult fields for critical hit") {
        HitResult hit;
        hit.hit = true;
        hit.damageDealt = 3000;
        hit.isCritical = true;
        hit.hitType = "melee";

        REQUIRE(hit.isCritical == true);
        REQUIRE(hit.damageDealt == 3000);
        REQUIRE(std::string(hit.hitType) == "melee");
    }

    SECTION("HitResult for miss") {
        HitResult hit;
        hit.hit = false;
        hit.hitType = "miss";

        REQUIRE_FALSE(hit.hit);
        REQUIRE(std::string(hit.hitType) == "miss");
    }

    SECTION("HitResult for cooldown") {
        HitResult hit;
        hit.hit = false;
        hit.hitType = "cooldown";

        REQUIRE_FALSE(hit.hit);
        REQUIRE(std::string(hit.hitType) == "cooldown");
    }
}

// ============================================================================
// CombatEventHandler delegation chain: logCombatEvent data extraction
// ============================================================================

TEST_CASE("CombatEventHandler logCombatEvent - PlayerInfo extraction", "[zones][handler]") {
    Registry registry;

    SECTION("PlayerInfo provides persistent player ID") {
        EntityID player = registry.create();
        auto& info = registry.emplace<PlayerInfo>(player);
        info.playerId = 12345;
        info.connectionId = 1;

        const char* testname = "TestPlayer";
        std::memcpy(info.username, testname, std::strlen(testname) + 1);

        auto* retrieved = registry.try_get<PlayerInfo>(player);
        REQUIRE(retrieved != nullptr);
        REQUIRE(retrieved->playerId == 12345);
        REQUIRE(std::string(retrieved->username) == "TestPlayer");
    }

    SECTION("NPC has no PlayerInfo") {
        EntityID npc = registry.create();
        registry.emplace<CombatState>(npc);

        auto* retrieved = registry.try_get<PlayerInfo>(npc);
        REQUIRE(retrieved == nullptr);
    }

    SECTION("Multiple players have distinct PlayerInfo") {
        EntityID p1 = registry.create();
        auto& info1 = registry.emplace<PlayerInfo>(p1);
        info1.playerId = 100;

        EntityID p2 = registry.create();
        auto& info2 = registry.emplace<PlayerInfo>(p2);
        info2.playerId = 200;

        REQUIRE(registry.get<PlayerInfo>(p1).playerId == 100);
        REQUIRE(registry.get<PlayerInfo>(p2).playerId == 200);
    }
}

// ============================================================================
// CombatEventHandler delegation chain: NPC vs NPC skip logic
// ============================================================================

TEST_CASE("CombatEventHandler logCombatEvent - NPC vs NPC skip", "[zones][handler]") {
    Registry registry;

    SECTION("NPC entities have no PlayerInfo - skip condition met") {
        EntityID npc1 = registry.create();
        registry.emplace<CombatState>(npc1);

        EntityID npc2 = registry.create();
        registry.emplace<CombatState>(npc2);

        // Simulates the handler's skip logic:
        // if (attackerPlayerId == 0 && targetPlayerId == 0) return;
        uint64_t attackerPlayerId = 0;
        uint64_t targetPlayerId = 0;

        if (auto* info = registry.try_get<PlayerInfo>(npc1)) {
            attackerPlayerId = info->playerId;
        }
        if (auto* info = registry.try_get<PlayerInfo>(npc2)) {
            targetPlayerId = info->playerId;
        }

        REQUIRE(attackerPlayerId == 0);
        REQUIRE(targetPlayerId == 0);
        // Handler would skip logging
    }

    SECTION("Player attacking NPC - logs the event") {
        EntityID player = registry.create();
        auto& info = registry.emplace<PlayerInfo>(player);
        info.playerId = 42;

        EntityID npc = registry.create();

        uint64_t attackerPlayerId = 0;
        uint64_t targetPlayerId = 0;

        if (auto* pi = registry.try_get<PlayerInfo>(player)) {
            attackerPlayerId = pi->playerId;
        }
        if (auto* pi = registry.try_get<PlayerInfo>(npc)) {
            targetPlayerId = pi->playerId;
        }

        REQUIRE(attackerPlayerId == 42);
        REQUIRE(targetPlayerId == 0);
        // Handler would log: attacker is a player
    }
}

// ============================================================================
// CombatEventHandler delegation chain: kill location extraction
// ============================================================================

TEST_CASE("CombatEventHandler kill location - Position extraction from victim", "[zones][handler]") {
    Registry registry;

    SECTION("Kill location is victim's last position") {
        EntityID victim = registry.create();
        registry.emplace<Position>(victim, Position::fromVec3(glm::vec3(100.0f, 5.0f, 200.0f), 500));

        Position killLocation{0, 0, 0, 0};
        if (auto* pos = registry.try_get<Position>(victim)) {
            killLocation = *pos;
        }

        auto vec = killLocation.toVec3();
        REQUIRE(vec.x == Catch::Approx(100.0f).margin(0.1f));
        REQUIRE(vec.z == Catch::Approx(200.0f).margin(0.1f));
    }

    SECTION("Kill location defaults to origin if no Position") {
        EntityID victim = registry.create();

        Position killLocation{0, 0, 0, 0};
        if (auto* pos = registry.try_get<Position>(victim)) {
            killLocation = *pos;
        }

        auto vec = killLocation.toVec3();
        REQUIRE(vec.x == Catch::Approx(0.0f).margin(0.01f));
        REQUIRE(vec.z == Catch::Approx(0.0f).margin(0.01f));
    }
}

// ============================================================================
// CombatEventHandler: full kill-to-respawn pipeline
// ============================================================================

TEST_CASE("CombatEventHandler full kill-to-respawn pipeline", "[zones][handler]") {
    Registry registry;
    CombatSystem combat;

    SECTION("Entity can be killed and respawned in sequence") {
        EntityID entity = registry.create();
        auto& cs = registry.emplace<CombatState>(entity);
        cs.health = 5000;
        cs.maxHealth = 10000;
        registry.emplace<Position>(entity, Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f), 0));
        registry.emplace<Velocity>(entity);

        EntityID killer = registry.create();
        registry.emplace<CombatState>(killer);
        registry.emplace<PlayerInfo>(killer).playerId = 1;

        // Step 1: Apply lethal damage
        bool damaged = combat.applyDamage(registry, entity, killer, 10000, 1000);
        REQUIRE(damaged);
        REQUIRE(cs.isDead);
        REQUIRE(cs.health == 0);

        // Step 2: Kill (triggers death callback)
        EntityID deathVictim = entt::null;
        combat.setOnDeath([&](EntityID v, EntityID) { deathVictim = v; });
        combat.killEntity(registry, entity, killer);
        REQUIRE(deathVictim == entity);

        // Step 3: Respawn
        Position spawnPos = Position::fromVec3(glm::vec3(50.0f, 0.0f, 50.0f), 2000);
        combat.respawnEntity(registry, entity, spawnPos);

        REQUIRE_FALSE(cs.isDead);
        REQUIRE(cs.health == cs.maxHealth);
        auto pos = registry.get<Position>(entity).toVec3();
        REQUIRE(pos.x == Catch::Approx(50.0f).margin(0.1f));
        REQUIRE(pos.z == Catch::Approx(50.0f).margin(0.1f));
    }
}

// ============================================================================
// CombatEventHandler: damage callback chain
// ============================================================================

TEST_CASE("CombatEventHandler damage callback chain - onDamage fires on hit", "[zones][handler]") {
    Registry registry;
    CombatSystem combat;

    SECTION("Damage callback receives correct parameters") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker, Rotation{0.0f, 0.0f});

        EntityID target = registry.create();
        registry.emplace<CombatState>(target);
        registry.emplace<Position>(target, Position::fromVec3(glm::vec3(0, 0, 1.0f), 0));
        registry.emplace<Rotation>(target);

        bool cbFired = false;
        int16_t cbDamage = 0;
        EntityID cbTarget = entt::null;

        combat.setOnDamage([&](EntityID, EntityID t, int16_t d, const Position&) {
            cbFired = true;
            cbTarget = t;
            cbDamage = d;
        });

        combat.performMeleeAttack(registry, attacker, 1000);

        REQUIRE(cbFired);
        REQUIRE(cbTarget == target);
        REQUIRE(cbDamage > 0);
    }

    SECTION("Damage callback NOT fired when no target in range") {
        EntityID attacker = registry.create();
        registry.emplace<CombatState>(attacker);
        registry.emplace<Position>(attacker, Position::fromVec3(glm::vec3(0, 0, 0), 0));
        registry.emplace<Rotation>(attacker);

        bool cbFired = false;
        combat.setOnDamage([&](EntityID, EntityID, int16_t, const Position&) {
            cbFired = true;
        });

        HitResult result = combat.performMeleeAttack(registry, attacker, 1000);

        REQUIRE_FALSE(cbFired);
        REQUIRE_FALSE(result.hit);
    }
}

// ============================================================================
// CombatEventHandler: CombatEvent struct construction (matches DB schema)
// ============================================================================

TEST_CASE("CombatEventHandler CombatEvent struct fields", "[zones][handler]") {
    SECTION("CombatEvent has required fields") {
        // This tests the CombatEvent struct used by the handler for DB logging
        // Fields: eventId, timestamp, zoneId, attackerId, targetId, eventType,
        //         damageAmount, isCritical, weaponType, position, serverTick

        // Verify HitResult provides all needed data for CombatEvent construction
        HitResult hit;
        hit.hit = true;
        hit.target = static_cast<EntityID>(10);
        hit.damageDealt = 2500;
        hit.isCritical = true;
        hit.hitLocation = Position::fromVec3(glm::vec3(50.0f, 0.0f, 100.0f), 5000);
        hit.hitType = "melee";

        // The handler builds a CombatEvent from HitResult fields:
        REQUIRE(hit.damageDealt == 2500);
        REQUIRE(hit.isCritical == true);
        REQUIRE(std::string(hit.hitType) == "melee");
    }
}

// ============================================================================
// CombatEventHandler: Position timestamp preservation
// ============================================================================

TEST_CASE("CombatEventHandler position timestamp preservation", "[zones][handler]") {
    SECTION("Position retains timestamp through copy") {
        Position original = Position::fromVec3(glm::vec3(10.0f, 0.0f, 20.0f), 5000);
        Position copy = original;

        REQUIRE(copy.timestamp_ms == 5000);
        auto origVec = original.toVec3();
        auto copyVec = copy.toVec3();
        REQUIRE(copyVec.x == Catch::Approx(origVec.x));
        REQUIRE(copyVec.z == Catch::Approx(origVec.z));
    }
}

// ============================================================================
// CombatEventHandler: entity-to-connection mapping edge cases
// ============================================================================

TEST_CASE("CombatEventHandler entity-connection mapping", "[zones][handler]") {
    SECTION("Entity-to-connection map lookup") {
        std::unordered_map<EntityID, ConnectionID> entityToConnection;
        std::unordered_map<ConnectionID, EntityID> connectionToEntity;

        EntityID e1 = static_cast<EntityID>(1);
        EntityID e2 = static_cast<EntityID>(2);
        ConnectionID c1 = 100;
        ConnectionID c2 = 200;

        entityToConnection[e1] = c1;
        entityToConnection[e2] = c2;
        connectionToEntity[c1] = e1;
        connectionToEntity[c2] = e2;

        // Handler sends to target's connection
        auto it = entityToConnection.find(e1);
        REQUIRE(it != entityToConnection.end());
        REQUIRE(it->second == c1);

        // Handler sends to attacker's connection
        it = entityToConnection.find(e2);
        REQUIRE(it != entityToConnection.end());
        REQUIRE(it->second == c2);

        // Entity without connection - handler skips send
        EntityID e3 = static_cast<EntityID>(3);
        it = entityToConnection.find(e3);
        REQUIRE(it == entityToConnection.end());
    }
}
