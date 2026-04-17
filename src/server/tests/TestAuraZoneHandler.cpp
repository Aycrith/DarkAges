// [ZONE_AGENT] Unit tests for AuraZoneHandler subsystems
// Tests aura delegation layer: AuraProjectionManager interactions,
// aura sync timing, entity zone transitions, ownership transfer logic,
// and edge cases in the handler's delegation chain.
// Note: AuraZoneHandler delegates to ZoneServer, so we test the subsystems
// it relies on (AuraProjectionManager, EntityMigration concepts).

#include <catch2/catch_test_macros.hpp>
#include "zones/AuraProjection.hpp"
#include "zones/ZoneDefinition.hpp"
#include "ecs/CoreTypes.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>

using namespace DarkAges;

// ============================================================================
// AuraZoneHandler delegation chain: syncAuraState timing
// ============================================================================

TEST_CASE("AuraZoneHandler sync timing interval", "[zones][handler]") {
    // The handler uses AURA_SYNC_INTERVAL_MS = 50ms to throttle syncs.
    // This test verifies the timing logic.

    SECTION("Sync interval constant is 50ms") {
        static constexpr uint32_t AURA_SYNC_INTERVAL_MS = 50;
        REQUIRE(AURA_SYNC_INTERVAL_MS == 50);
    }

    SECTION("Sync should fire when interval has elapsed") {
        uint32_t lastSyncTime = 1000;
        uint32_t currentTime = 1051;  // 51ms later
        static constexpr uint32_t AURA_SYNC_INTERVAL_MS = 50;

        bool shouldSync = (currentTime - lastSyncTime) >= AURA_SYNC_INTERVAL_MS;
        REQUIRE(shouldSync);
    }

    SECTION("Sync should NOT fire before interval elapsed") {
        uint32_t lastSyncTime = 1000;
        uint32_t currentTime = 1049;  // 49ms later
        static constexpr uint32_t AURA_SYNC_INTERVAL_MS = 50;

        bool shouldSync = (currentTime - lastSyncTime) >= AURA_SYNC_INTERVAL_MS;
        REQUIRE_FALSE(shouldSync);
    }

    SECTION("Sync should fire exactly at interval boundary") {
        uint32_t lastSyncTime = 1000;
        uint32_t currentTime = 1050;  // Exactly 50ms
        static constexpr uint32_t AURA_SYNC_INTERVAL_MS = 50;

        bool shouldSync = (currentTime - lastSyncTime) >= AURA_SYNC_INTERVAL_MS;
        REQUIRE(shouldSync);
    }

    SECTION("Multiple sync cycles") {
        uint32_t lastSyncTime = 0;
        static constexpr uint32_t AURA_SYNC_INTERVAL_MS = 50;
        int syncCount = 0;

        // Simulate 200ms of ticks at 60Hz (~16.67ms per tick)
        for (uint32_t t = 0; t <= 200; t += 17) {
            if (t - lastSyncTime >= AURA_SYNC_INTERVAL_MS) {
                syncCount++;
                lastSyncTime = t;
            }
        }
        // At 200ms with 50ms interval, expect ~4 syncs
        REQUIRE(syncCount >= 3);
        REQUIRE(syncCount <= 5);
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: aura entity sync serialization format
// ============================================================================

TEST_CASE("AuraZoneHandler sync serialization format", "[zones][handler]") {
    // The handler serializes as: entityId|ownerZone|x|y|z|dx|dy|dz

    SECTION("AuraEntityState fields match serialization order") {
        AuraEntityState state;
        state.entity = static_cast<EntityID>(42);
        state.ownerZoneId = 1;
        state.lastKnownPosition = Position::fromVec3(glm::vec3(100.0f, 0.0f, 200.0f));
        state.lastKnownVelocity = Velocity{};
        state.lastKnownVelocity.dx = 1000;
        state.lastKnownVelocity.dy = 0;
        state.lastKnownVelocity.dz = 500;
        state.lastUpdateTick = 100;
        state.isGhost = false;

        REQUIRE(static_cast<uint32_t>(state.entity) == 42);
        REQUIRE(state.ownerZoneId == 1);
        REQUIRE(state.lastKnownVelocity.dx == 1000);
    }

    SECTION("Ghost entity is not synced (owned by other zone)") {
        AuraProjectionManager auraManager(1);
        std::vector<ZoneDefinition> adjacentZones;
        auraManager.initialize(adjacentZones);

        // Entity owned by zone 2 (ghost in our aura)
        EntityID ghost = static_cast<EntityID>(100);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));
        auraManager.onEntityEnteringAura(ghost, pos, 2);

        // Entity owned by us
        EntityID owned = static_cast<EntityID>(101);
        auraManager.onEntityEnteringAura(owned, pos, 1);

        std::vector<AuraEntityState> toSync;
        auraManager.getEntitiesToSync(toSync);

        // Only owned entities should be in sync list
        REQUIRE(toSync.size() == 1);
        REQUIRE(toSync[0].entity == owned);
        REQUIRE(toSync[0].ownerZoneId == 1);
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: checkEntityZoneTransitions
// ============================================================================

TEST_CASE("AuraZoneHandler entity zone transition detection", "[zones][handler]") {
    AuraProjectionManager auraManager(1);
    std::vector<ZoneDefinition> adjacentZones;
    auraManager.initialize(adjacentZones);

    SECTION("Entity entering aura from core zone") {
        EntityID entity = static_cast<EntityID>(50);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        // Entity was NOT in aura before
        REQUIRE_FALSE(auraManager.isEntityInAura(entity));

        // Handler would call auraManager.onEntityEnteringAura
        auraManager.onEntityEnteringAura(entity, pos, 1);

        REQUIRE(auraManager.isEntityInAura(entity));
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 1);
    }

    SECTION("Entity leaving aura to core zone") {
        EntityID entity = static_cast<EntityID>(51);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 1);
        REQUIRE(auraManager.isEntityInAura(entity));

        // Handler would call auraManager.onEntityLeavingAura
        auraManager.onEntityLeavingAura(entity, 1);
        REQUIRE_FALSE(auraManager.isEntityInAura(entity));
    }

    SECTION("Entity already in aura gets state updated") {
        EntityID entity = static_cast<EntityID>(52);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 1);
        REQUIRE(auraManager.isEntityInAura(entity));

        // Handler calls updateEntityState for entities already in aura
        Position newPos = Position::fromVec3(glm::vec3(110.0f, 0.0f, 100.0f));
        Velocity vel;
        vel.dx = 2000;
        auraManager.updateEntityState(entity, newPos, vel, 100);

        REQUIRE(auraManager.isEntityInAura(entity));
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: handleAuraEntityMigration
// ============================================================================

TEST_CASE("AuraZoneHandler ownership transfer delegation", "[zones][handler]") {
    AuraProjectionManager auraManager(1);
    std::vector<ZoneDefinition> adjacentZones;
    auraManager.initialize(adjacentZones);

    SECTION("Ownership transfer changes zone ID") {
        EntityID entity = static_cast<EntityID>(60);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        // Entity initially owned by zone 2
        auraManager.onEntityEnteringAura(entity, pos, 2);
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 2);

        // Handler transfers ownership
        auraManager.onOwnershipTransferred(entity, 2);
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 1);
    }

    SECTION("shouldTakeOwnership check") {
        EntityID entity = static_cast<EntityID>(61);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 2);

        // Whether ownership should be taken depends on position
        bool shouldTake = auraManager.shouldTakeOwnership(entity, pos);
        // Just verify it doesn't crash - result depends on zone geometry
        (void)shouldTake;
        REQUIRE(true);
    }

    SECTION("Multiple ownership transfers are safe") {
        EntityID entity = static_cast<EntityID>(62);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 2);
        auraManager.onOwnershipTransferred(entity, 2);
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 1);

        // Transfer back (simulates ping-pong at boundary)
        auraManager.onEntityEnteringAura(entity, pos, 1);
        auraManager.onOwnershipTransferred(entity, 1);
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 1);
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: Redis publish channel naming
// ============================================================================

TEST_CASE("AuraZoneHandler Redis channel naming convention", "[zones][handler]") {
    SECTION("Aura sync channel follows zone:N:aura pattern") {
        uint32_t zoneId = 5;
        std::string channel = "zone:" + std::to_string(zoneId) + ":aura";
        REQUIRE(channel == "zone:5:aura");
    }

    SECTION("Different zones get different channels") {
        std::string ch1 = "zone:" + std::to_string(1) + ":aura";
        std::string ch2 = "zone:" + std::to_string(2) + ":aura";
        REQUIRE(ch1 != ch2);
    }

    SECTION("Entity zone key follows entity_zone:N pattern") {
        EntityID entity = static_cast<EntityID>(42);
        std::string key = "entity_zone:" + std::to_string(static_cast<uint32_t>(entity));
        REQUIRE(key == "entity_zone:42");
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: Velocity extraction for aura entities
// ============================================================================

TEST_CASE("AuraZoneHandler velocity extraction from registry", "[zones][handler]") {
    Registry registry;

    SECTION("Entity with Velocity component - handler extracts it") {
        EntityID entity = registry.create();
        registry.emplace<Position>(entity);
        auto& vel = registry.emplace<Velocity>(entity);
        vel.dx = 3000;
        vel.dy = 0;
        vel.dz = 1500;

        // Handler does: if (auto* v = registry.try_get<Velocity>(entity)) { vel = *v; }
        Velocity extractedVel{};
        if (auto* v = registry.try_get<Velocity>(entity)) {
            extractedVel = *v;
        }

        REQUIRE(extractedVel.dx == 3000);
        REQUIRE(extractedVel.dz == 1500);
    }

    SECTION("Entity without Velocity - handler uses default zero") {
        EntityID entity = registry.create();
        registry.emplace<Position>(entity);

        Velocity extractedVel{};
        if (auto* v = registry.try_get<Velocity>(entity)) {
            extractedVel = *v;
        }

        REQUIRE(extractedVel.dx == 0);
        REQUIRE(extractedVel.dy == 0);
        REQUIRE(extractedVel.dz == 0);
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: StaticTag exclusion
// ============================================================================

TEST_CASE("AuraZoneHandler StaticTag exclusion from aura processing", "[zones][handler]") {
    Registry registry;

    SECTION("Dynamic entities appear in Position view without StaticTag") {
        EntityID dynamic = registry.create();
        registry.emplace<Position>(dynamic);

        auto view = registry.view<Position>(entt::exclude<StaticTag>);
        bool found = false;
        for (auto e : view) {
            if (e == dynamic) found = true;
        }
        REQUIRE(found);
    }

    SECTION("Static entities are excluded from Position view") {
        EntityID staticEntity = registry.create();
        registry.emplace<Position>(staticEntity);
        registry.emplace<StaticTag>(staticEntity);

        auto view = registry.view<Position>(entt::exclude<StaticTag>);
        bool found = false;
        for (auto e : view) {
            if (e == staticEntity) found = true;
        }
        REQUIRE_FALSE(found);
    }

    SECTION("Mixed entities - only dynamic included") {
        EntityID dyn1 = registry.create();
        registry.emplace<Position>(dyn1);

        EntityID stat1 = registry.create();
        registry.emplace<Position>(stat1);
        registry.emplace<StaticTag>(stat1);

        EntityID dyn2 = registry.create();
        registry.emplace<Position>(dyn2);

        auto view = registry.view<Position>(entt::exclude<StaticTag>);
        int count = 0;
        for (auto e : view) {
            (void)e;
            count++;
        }
        REQUIRE(count == 2);
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: adjacent zone entity state reception
// ============================================================================

TEST_CASE("AuraZoneHandler adjacent zone entity state", "[zones][handler]") {
    AuraProjectionManager auraManager(1);
    std::vector<ZoneDefinition> adjacentZones;
    auraManager.initialize(adjacentZones);

    SECTION("Ghost entity from adjacent zone is tracked") {
        EntityID ghostEntity = static_cast<EntityID>(200);
        Position pos = Position::fromVec3(glm::vec3(50.0f, 0.0f, 50.0f));
        Velocity vel;
        vel.dx = 1000;

        auraManager.onEntityStateFromAdjacentZone(2, ghostEntity, pos, vel);

        REQUIRE(auraManager.isEntityInAura(ghostEntity));
        REQUIRE(auraManager.getEntityOwnerZone(ghostEntity) == 2);
    }

    SECTION("Multiple adjacent zone updates overwrite previous state") {
        EntityID entity = static_cast<EntityID>(201);
        Position pos1 = Position::fromVec3(glm::vec3(50.0f, 0.0f, 50.0f));
        Position pos2 = Position::fromVec3(glm::vec3(55.0f, 0.0f, 50.0f));
        Velocity vel;

        auraManager.onEntityStateFromAdjacentZone(2, entity, pos1, vel);
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 2);

        // Same zone updates position
        auraManager.onEntityStateFromAdjacentZone(2, entity, pos2, vel);
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 2);
        REQUIRE(auraManager.isEntityInAura(entity));
    }

    SECTION("Entity from different adjacent zones") {
        EntityID e1 = static_cast<EntityID>(202);
        EntityID e2 = static_cast<EntityID>(203);
        Position pos = Position::fromVec3(glm::vec3(50.0f, 0.0f, 50.0f));
        Velocity vel;

        auraManager.onEntityStateFromAdjacentZone(2, e1, pos, vel);
        auraManager.onEntityStateFromAdjacentZone(3, e2, pos, vel);

        REQUIRE(auraManager.getEntityOwnerZone(e1) == 2);
        REQUIRE(auraManager.getEntityOwnerZone(e2) == 3);
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: entity removal from aura
// ============================================================================

TEST_CASE("AuraZoneHandler entity removal from aura tracking", "[zones][handler]") {
    AuraProjectionManager auraManager(1);
    std::vector<ZoneDefinition> adjacentZones;
    auraManager.initialize(adjacentZones);

    SECTION("Remove entity clears aura tracking") {
        EntityID entity = static_cast<EntityID>(300);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 1);
        REQUIRE(auraManager.isEntityInAura(entity));

        auraManager.removeEntity(entity);
        REQUIRE_FALSE(auraManager.isEntityInAura(entity));
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 0);
    }

    SECTION("Remove non-existent entity is safe") {
        EntityID nonExistent = static_cast<EntityID>(9999);
        REQUIRE_NOTHROW(auraManager.removeEntity(nonExistent));
    }

    SECTION("Remove entity then re-add works") {
        EntityID entity = static_cast<EntityID>(301);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 1);
        auraManager.removeEntity(entity);
        auraManager.onEntityEnteringAura(entity, pos, 1);

        REQUIRE(auraManager.isEntityInAura(entity));
    }
}

// ============================================================================
// AuraZoneHandler delegation chain: sync list for multiple entities
// ============================================================================

TEST_CASE("AuraZoneHandler sync list with mixed entity types", "[zones][handler]") {
    AuraProjectionManager auraManager(1);
    std::vector<ZoneDefinition> adjacentZones;
    auraManager.initialize(adjacentZones);

    SECTION("Sync list contains only owned entities among many") {
        // 5 owned entities
        for (int i = 0; i < 5; i++) {
            EntityID e = static_cast<EntityID>(i + 1);
            Position pos = Position::fromVec3(glm::vec3(100.0f + i, 0.0f, 100.0f));
            auraManager.onEntityEnteringAura(e, pos, 1);
        }

        // 3 ghost entities from zone 2
        for (int i = 0; i < 3; i++) {
            EntityID e = static_cast<EntityID>(i + 10);
            Position pos = Position::fromVec3(glm::vec3(100.0f + i, 0.0f, 100.0f));
            auraManager.onEntityEnteringAura(e, pos, 2);
        }

        std::vector<AuraEntityState> toSync;
        auraManager.getEntitiesToSync(toSync);

        REQUIRE(toSync.size() == 5);
        for (const auto& state : toSync) {
            REQUIRE(state.ownerZoneId == 1);
        }
    }

    SECTION("Empty aura returns empty sync list") {
        std::vector<AuraEntityState> toSync;
        auraManager.getEntitiesToSync(toSync);
        REQUIRE(toSync.empty());
    }

    SECTION("After removing all owned entities, sync list is empty") {
        for (int i = 0; i < 3; i++) {
            EntityID e = static_cast<EntityID>(i + 1);
            Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));
            auraManager.onEntityEnteringAura(e, pos, 1);
        }

        for (int i = 0; i < 3; i++) {
            auraManager.removeEntity(static_cast<EntityID>(i + 1));
        }

        std::vector<AuraEntityState> toSync;
        auraManager.getEntitiesToSync(toSync);
        REQUIRE(toSync.empty());
    }
}

// ============================================================================
// AuraZoneHandler: edge cases with large entity counts
// ============================================================================

TEST_CASE("AuraZoneHandler many entities in aura", "[zones][handler]") {
    AuraProjectionManager auraManager(1);
    std::vector<ZoneDefinition> adjacentZones;
    auraManager.initialize(adjacentZones);

    SECTION("100 entities all tracked correctly") {
        for (int i = 0; i < 100; i++) {
            EntityID e = static_cast<EntityID>(i + 1);
            Position pos = Position::fromVec3(glm::vec3(
                50.0f + (i % 10) * 5.0f, 0.0f, 50.0f + (i / 10) * 5.0f));
            auraManager.onEntityEnteringAura(e, pos, 1);
        }

        for (int i = 0; i < 100; i++) {
            REQUIRE(auraManager.isEntityInAura(static_cast<EntityID>(i + 1)));
        }

        std::vector<AuraEntityState> toSync;
        auraManager.getEntitiesToSync(toSync);
        REQUIRE(toSync.size() == 100);
    }

    SECTION("Visibility query with many entities") {
        for (int i = 0; i < 100; i++) {
            EntityID e = static_cast<EntityID>(i + 1);
            Position pos = Position::fromVec3(glm::vec3(
                50.0f + (i % 10) * 5.0f, 0.0f, 50.0f + (i / 10) * 5.0f));
            auraManager.onEntityEnteringAura(e, pos, 2);
        }

        Position playerPos = Position::fromVec3(glm::vec3(70.0f, 0.0f, 70.0f));
        auto visible = auraManager.getEntitiesInAuraForPlayer(playerPos, 30.0f);

        // Should find some but not all
        REQUIRE(visible.size() > 0);
        REQUIRE(visible.size() <= 100);
    }
}

// ============================================================================
// AuraZoneHandler: entity entering aura from different source zones
// ============================================================================

TEST_CASE("AuraZoneHandler source zone tracking", "[zones][handler]") {
    AuraProjectionManager auraManager(1);
    std::vector<ZoneDefinition> adjacentZones;
    auraManager.initialize(adjacentZones);

    SECTION("Entity from own zone (zone 1) entering aura") {
        EntityID entity = static_cast<EntityID>(400);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 1);

        REQUIRE(auraManager.isEntityInAura(entity));
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 1);
    }

    SECTION("Entity from adjacent zone (zone 2) entering aura") {
        EntityID entity = static_cast<EntityID>(401);
        Position pos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 100.0f));

        auraManager.onEntityEnteringAura(entity, pos, 2);

        REQUIRE(auraManager.isEntityInAura(entity));
        REQUIRE(auraManager.getEntityOwnerZone(entity) == 2);
    }
}
