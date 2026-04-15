// [ZONE_AGENT] Zone Handoff Controller Unit Tests
// Tests handoff initiation, phase transitions, state tracking, and edge cases

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zones/ZoneHandoff.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <cstdint>
#include <string>

using namespace DarkAges;
using Catch::Approx;

// ============================================================================
// Helper: Create a simple zone definition for zone 1 (left half of world)
// ============================================================================

static ZoneDefinition makeZone1Def() {
    ZoneDefinition def;
    def.zoneId = 1;
    def.zoneName = "Zone1";
    def.shape = ZoneShape::RECTANGLE;
    def.minX = -5000.0f;
    def.maxX = 0.0f;
    def.minY = -100.0f;
    def.maxY = 500.0f;
    def.minZ = -5000.0f;
    def.maxZ = 5000.0f;
    def.centerX = -2500.0f;
    def.centerZ = 0.0f;
    def.adjacentZones = {2};
    def.host = "127.0.0.1";
    def.port = 7778;
    return def;
}

static ZoneDefinition makeZone2Def() {
    ZoneDefinition def;
    def.zoneId = 2;
    def.zoneName = "Zone2";
    def.shape = ZoneShape::RECTANGLE;
    def.minX = 0.0f;
    def.maxX = 5000.0f;
    def.minY = -100.0f;
    def.maxY = 500.0f;
    def.minZ = -5000.0f;
    def.maxZ = 5000.0f;
    def.centerX = 2500.0f;
    def.centerZ = 0.0f;
    def.adjacentZones = {1};
    def.host = "127.0.0.1";
    def.port = 7779;
    return def;
}

// Helper: Create a position in fixed-point from float world coords
static Position makePosition(float x, float z) {
    Position pos;
    pos.x = static_cast<Constants::Fixed>(x * Constants::FLOAT_TO_FIXED);
    pos.y = 0;
    pos.z = static_cast<Constants::Fixed>(z * Constants::FLOAT_TO_FIXED);
    return pos;
}

// Helper: Create a velocity in fixed-point
static Velocity makeVelocity(float dx, float dz) {
    Velocity vel;
    vel.dx = static_cast<Constants::Fixed>(dx * Constants::FLOAT_TO_FIXED);
    vel.dy = 0;
    vel.dz = static_cast<Constants::Fixed>(dz * Constants::FLOAT_TO_FIXED);
    return vel;
}

// Helper: Set up a zoneByPosition callback that maps float coords to zones
// Zone 1: x in [-5000, 0], Zone 2: x in (0, 5000]
static void setupZoneCallbacks(ZoneHandoffController& controller) {
    controller.setZoneLookupCallbacks(
        // Zone lookup by ID
        [](uint32_t zoneId) -> ZoneDefinition* {
            static ZoneDefinition z1 = makeZone1Def();
            static ZoneDefinition z2 = makeZone2Def();
            if (zoneId == 1) return &z1;
            if (zoneId == 2) return &z2;
            return nullptr;
        },
        // Zone by position
        [](float x, float /*z*/) -> uint32_t {
            if (x <= 0.0f) return 2;  // If predicted past x=0 boundary, target is zone 2
            return 2;  // Default to zone 2 for any position
        }
    );
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_CASE("ZoneHandoffController initialization", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);

    SECTION("Default config initializes successfully") {
        bool result = controller.initialize();
        REQUIRE(result == true);
    }

    SECTION("Valid custom config initializes successfully") {
        HandoffConfig config;
        config.preparationDistance = 100.0f;
        config.auraEnterDistance = 50.0f;
        config.migrationDistance = 25.0f;
        config.handoffDistance = 10.0f;

        bool result = controller.initialize(config);
        REQUIRE(result == true);
    }

    SECTION("Invalid config fails when preparation <= auraEnter") {
        HandoffConfig config;
        config.preparationDistance = 50.0f;
        config.auraEnterDistance = 50.0f;
        config.migrationDistance = 25.0f;
        config.handoffDistance = 10.0f;

        bool result = controller.initialize(config);
        REQUIRE(result == false);
    }

    SECTION("Invalid config fails when auraEnter <= migration") {
        HandoffConfig config;
        config.preparationDistance = 75.0f;
        config.auraEnterDistance = 25.0f;
        config.migrationDistance = 25.0f;
        config.handoffDistance = 10.0f;

        bool result = controller.initialize(config);
        REQUIRE(result == false);
    }

    SECTION("Invalid config fails when migration <= handoff") {
        HandoffConfig config;
        config.preparationDistance = 75.0f;
        config.auraEnterDistance = 50.0f;
        config.migrationDistance = 10.0f;
        config.handoffDistance = 10.0f;

        bool result = controller.initialize(config);
        REQUIRE(result == false);
    }

    SECTION("Invalid config fails when all distances are equal") {
        HandoffConfig config;
        config.preparationDistance = 10.0f;
        config.auraEnterDistance = 10.0f;
        config.migrationDistance = 10.0f;
        config.handoffDistance = 10.0f;

        bool result = controller.initialize(config);
        REQUIRE(result == false);
    }
}

// ============================================================================
// Player Phase Query Tests
// ============================================================================

TEST_CASE("ZoneHandoffController player phase queries", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    SECTION("Unknown player has phase NONE") {
        REQUIRE(controller.getPlayerPhase(99999) == HandoffPhase::NONE);
    }

    SECTION("Unknown player has no handoff in progress") {
        REQUIRE(controller.isHandoffInProgress(99999) == false);
    }

    SECTION("Active handoffs map starts empty") {
        REQUIRE(controller.getActiveHandoffs().empty());
    }
}

// ============================================================================
// Handoff Initiation Tests
// ============================================================================

TEST_CASE("ZoneHandoffController handoff initiation via position check", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    SECTION("Player far from edge does not start handoff") {
        Position pos = makePosition(-2500.0f, 0.0f);
        Velocity vel = makeVelocity(1.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);

        REQUIRE(!controller.isHandoffInProgress(100));
        REQUIRE(controller.getPlayerPhase(100) == HandoffPhase::NONE);
    }

    SECTION("Player near zone edge starts preparation") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 1000);

        REQUIRE(controller.isHandoffInProgress(100));
        REQUIRE(controller.getPlayerPhase(100) == HandoffPhase::PREPARING);
    }

    SECTION("Player already in handoff updates tracking without re-triggering") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 1000);
        REQUIRE(controller.getPlayerPhase(100) == HandoffPhase::PREPARING);

        // Player moves closer to edge - should update tracking
        Position pos2 = makePosition(-40.0f, 0.0f);
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos2, vel, 2000);

        REQUIRE(controller.getPlayerPhase(100) == HandoffPhase::PREPARING);
        REQUIRE(controller.getActiveHandoffs().size() == 1);
    }

    SECTION("Player far from edge with no target zone does not start handoff") {
        // Zone lookup returns 0 for this position (no adjacent zone)
        controller.setZoneLookupCallbacks(nullptr, nullptr);  // Clear callbacks
        controller.setMyZoneDefinition(makeZone1Def());

        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        // Without callbacks, determineTargetZone falls back to myZoneDef_
        // futureX = -50, still inside zone 1 bounds, so returns 0
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 1000);

        // No handoff triggered because determineTargetZone returns 0
        REQUIRE(!controller.isHandoffInProgress(100));
    }
}

// ============================================================================
// Handoff Callbacks Tests
// ============================================================================

TEST_CASE("ZoneHandoffController callbacks fire correctly", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    bool startedCalled = false;
    bool startedSuccess = false;
    uint64_t startedPlayerId = 0;

    controller.setOnHandoffStarted(
        [&](uint64_t playerId, uint32_t /*src*/, uint32_t /*target*/, bool success) {
            startedCalled = true;
            startedPlayerId = playerId;
            startedSuccess = success;
        });

    SECTION("HandoffStarted callback fires on preparation entry") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(42, static_cast<EntityID>(1), 1, pos, vel, 1000);

        REQUIRE(startedCalled == true);
        REQUIRE(startedPlayerId == 42);
        REQUIRE(startedSuccess == true);
    }

    SECTION("HandoffStarted callback does not fire when no handoff started") {
        Position pos = makePosition(-3000.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(42, static_cast<EntityID>(1), 1, pos, vel, 1000);

        REQUIRE(startedCalled == false);
    }
}

TEST_CASE("ZoneHandoffController completion callbacks", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    bool completedCalled = false;
    bool completedSuccess = false;
    uint64_t completedPlayerId = 0;

    controller.setOnHandoffCompleted(
        [&](uint64_t playerId, uint32_t /*src*/, uint32_t /*tgt*/, bool success) {
            completedCalled = true;
            completedPlayerId = playerId;
            completedSuccess = success;
        });

    // Start a handoff first
    Position pos = makePosition(-60.0f, 0.0f);
    Velocity vel = makeVelocity(5.0f, 0.0f);
    controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 1000);

    SECTION("completeHandoff with success fires completed callback with success") {
        controller.completeHandoff(100, true);

        REQUIRE(completedCalled == true);
        REQUIRE(completedPlayerId == 100);
        REQUIRE(completedSuccess == true);
    }

    SECTION("completeHandoff with failure fires completed callback with failure") {
        controller.completeHandoff(100, false);

        REQUIRE(completedCalled == true);
        REQUIRE(completedPlayerId == 100);
        REQUIRE(completedSuccess == false);
    }

    SECTION("completeHandoff on unknown player does nothing") {
        controller.completeHandoff(99999, true);

        REQUIRE(completedCalled == false);
    }
}

// ============================================================================
// Cancel Handoff Tests
// ============================================================================

TEST_CASE("ZoneHandoffController cancel handoff", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    Position pos = makePosition(-60.0f, 0.0f);
    Velocity vel = makeVelocity(5.0f, 0.0f);
    controller.checkPlayerPosition(200, static_cast<EntityID>(1), 1, pos, vel, 1000);
    REQUIRE(controller.isHandoffInProgress(200));

    SECTION("Cancel removes active handoff") {
        bool cancelled = controller.cancelHandoff(200);
        REQUIRE(cancelled == true);
        REQUIRE(controller.isHandoffInProgress(200) == false);
    }

    SECTION("Cancel unknown player returns false") {
        bool cancelled = controller.cancelHandoff(99999);
        REQUIRE(cancelled == false);
    }

    SECTION("Cancel then re-check can start new handoff") {
        controller.cancelHandoff(200);
        REQUIRE(!controller.isHandoffInProgress(200));

        controller.checkPlayerPosition(200, static_cast<EntityID>(1), 1, pos, vel, 2000);
        REQUIRE(controller.isHandoffInProgress(200));
    }
}

// ============================================================================
// Handoff Token Validation Tests
// ============================================================================

TEST_CASE("ZoneHandoffController handoff token validation", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    SECTION("Unknown player token validation fails") {
        REQUIRE(controller.validateHandoffToken(99999, "anytoken") == false);
    }

    SECTION("Token validation for player not in switching phase") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);
        controller.checkPlayerPosition(300, static_cast<EntityID>(1), 1, pos, vel, 0);

        // In PREPARING phase, no token has been generated yet
        const auto& handoffs = controller.getActiveHandoffs();
        auto it = handoffs.find(300);
        REQUIRE(it != handoffs.end());
        // Token should be empty since we're not in SWITCHING phase
        REQUIRE(it->second.handoffToken.empty());
    }
}

// ============================================================================
// Statistics Tracking Tests
// ============================================================================

TEST_CASE("ZoneHandoffController statistics tracking", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    SECTION("Stats start at zero") {
        auto stats = controller.getStats();
        REQUIRE(stats.totalHandoffs == 0);
        REQUIRE(stats.successfulHandoffs == 0);
        REQUIRE(stats.failedHandoffs == 0);
        REQUIRE(stats.cancelledHandoffs == 0);
        REQUIRE(stats.timeoutCount == 0);
    }

    SECTION("Starting a handoff increments total count") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);

        REQUIRE(controller.getStats().totalHandoffs == 1);
    }

    SECTION("Completing successfully increments successful count") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);

        controller.completeHandoff(100, true);
        REQUIRE(controller.getStats().successfulHandoffs == 1);
        REQUIRE(controller.getStats().failedHandoffs == 0);
    }

    SECTION("Completing with failure increments failed count") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);

        controller.completeHandoff(100, false);
        REQUIRE(controller.getStats().failedHandoffs == 1);
        REQUIRE(controller.getStats().successfulHandoffs == 0);
    }

    SECTION("Multiple handoffs accumulate stats") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        controller.completeHandoff(100, true);

        controller.checkPlayerPosition(200, static_cast<EntityID>(2), 2, pos, vel, 100);
        controller.completeHandoff(200, false);

        REQUIRE(controller.getStats().totalHandoffs == 2);
        REQUIRE(controller.getStats().successfulHandoffs == 1);
        REQUIRE(controller.getStats().failedHandoffs == 1);
    }
}

// ============================================================================
// Update and Phase Transition Tests
// ============================================================================

TEST_CASE("ZoneHandoffController update phase transitions", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    Registry registry;

    SECTION("Update cancels handoff when player moves back from edge") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(controller.isHandoffInProgress(100));

        // Move player far from edge
        Position posFar = makePosition(-200.0f, 0.0f);
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, posFar, vel, 100);

        controller.update(registry, 200);

        REQUIRE(controller.isHandoffInProgress(100) == false);
        REQUIRE(controller.getStats().cancelledHandoffs == 1);
    }

    SECTION("Completed handoff is cleaned up by update") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(controller.isHandoffInProgress(100));

        controller.completeHandoff(100, true);
        REQUIRE(controller.getPlayerPhase(100) == HandoffPhase::COMPLETED);

        controller.update(registry, 2000);
        REQUIRE(controller.isHandoffInProgress(100) == false);
    }
}

// ============================================================================
// Timeout Tests
// ============================================================================

TEST_CASE("ZoneHandoffController timeout handling", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);

    HandoffConfig config;
    config.preparationDistance = 75.0f;
    config.auraEnterDistance = 50.0f;
    config.migrationDistance = 25.0f;
    config.handoffDistance = 10.0f;
    config.preparationTimeoutMs = 100;
    config.migrationTimeoutMs = 50;
    config.switchTimeoutMs = 50;

    controller.initialize(config);
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    Registry registry;

    SECTION("Preparation phase times out") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(0.0f, 0.0f);  // Not moving
        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(controller.getPlayerPhase(100) == HandoffPhase::PREPARING);

        controller.update(registry, 50);
        REQUIRE(controller.isHandoffInProgress(100));

        controller.update(registry, 151);
        REQUIRE(controller.isHandoffInProgress(100) == false);
        REQUIRE(controller.getStats().timeoutCount == 1);
    }
}

// ============================================================================
// Multiple Concurrent Handoffs Tests
// ============================================================================

TEST_CASE("ZoneHandoffController handles multiple concurrent handoffs", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    Position pos = makePosition(-60.0f, 0.0f);
    Velocity vel = makeVelocity(5.0f, 0.0f);

    SECTION("Multiple players can have concurrent handoffs") {
        controller.checkPlayerPosition(1, static_cast<EntityID>(1), 1, pos, vel, 0);
        controller.checkPlayerPosition(2, static_cast<EntityID>(2), 2, pos, vel, 0);
        controller.checkPlayerPosition(3, static_cast<EntityID>(3), 3, pos, vel, 0);

        REQUIRE(controller.getActiveHandoffs().size() == 3);
        REQUIRE(controller.isHandoffInProgress(1));
        REQUIRE(controller.isHandoffInProgress(2));
        REQUIRE(controller.isHandoffInProgress(3));
    }

    SECTION("Completing one does not affect others") {
        controller.checkPlayerPosition(1, static_cast<EntityID>(1), 1, pos, vel, 0);
        controller.checkPlayerPosition(2, static_cast<EntityID>(2), 2, pos, vel, 0);

        controller.completeHandoff(1, true);
        Registry registry;
        controller.update(registry, 100);

        REQUIRE(controller.isHandoffInProgress(1) == false);
        REQUIRE(controller.isHandoffInProgress(2));
        REQUIRE(controller.getActiveHandoffs().size() == 1);
    }

    SECTION("Cancelling one does not affect others") {
        controller.checkPlayerPosition(1, static_cast<EntityID>(1), 1, pos, vel, 0);
        controller.checkPlayerPosition(2, static_cast<EntityID>(2), 2, pos, vel, 0);

        controller.cancelHandoff(1);

        REQUIRE(controller.isHandoffInProgress(1) == false);
        REQUIRE(controller.isHandoffInProgress(2));
    }
}

// ============================================================================
// Zone Definition Configuration Tests
// ============================================================================

TEST_CASE("ZoneHandoffController zone definition affects calculations", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    SECTION("Player near X-max edge triggers handoff") {
        Position pos = makePosition(-30.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(controller.isHandoffInProgress(100));
    }

    SECTION("Player near X-min edge triggers handoff") {
        Position pos = makePosition(-4970.0f, 0.0f);
        Velocity vel = makeVelocity(-5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(controller.isHandoffInProgress(100));
    }

    SECTION("Changing zone definition updates distance calculations") {
        // Test with a NEW controller for zone 2
        ZoneHandoffController controller2(2, nullptr, nullptr, nullptr);
        controller2.initialize();
        controller2.setMyZoneDefinition(makeZone2Def());
        controller2.setZoneLookupCallbacks(
            [](uint32_t zoneId) -> ZoneDefinition* {
                static ZoneDefinition z1 = makeZone1Def();
                static ZoneDefinition z2 = makeZone2Def();
                if (zoneId == 1) return &z1;
                if (zoneId == 2) return &z2;
                return nullptr;
            },
            [](float x, float) -> uint32_t {
                if (x <= 0.0f) return 1;
                return 1;
            }
        );

        // In zone 2 (0 to 5000), at x=30, distance to minX edge (0) is 30m
        Position pos = makePosition(30.0f, 0.0f);
        Velocity vel = makeVelocity(-5.0f, 0.0f);  // Moving toward zone 1

        controller2.checkPlayerPosition(100, static_cast<EntityID>(1), 2, pos, vel, 0);
        REQUIRE(controller2.isHandoffInProgress(100));
    }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_CASE("ZoneHandoffController edge cases", "[zones][handoff]") {
    ZoneHandoffController controller(1, nullptr, nullptr, nullptr);
    controller.initialize();
    controller.setMyZoneDefinition(makeZone1Def());
    setupZoneCallbacks(controller);

    SECTION("Zero velocity triggers handoff if near edge") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(0.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        // With zero velocity, determineTargetZone's fallback path uses myZoneDef_
        // futureX = -60, still inside zone 1 -> returns 0 -> no handoff
        // This is expected behavior: no movement = no target zone detected
        // But with zoneByPosition_ callback set, it should work
        REQUIRE(controller.isHandoffInProgress(100));
    }

    SECTION("Cancel immediately after start") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(controller.isHandoffInProgress(100));

        bool cancelled = controller.cancelHandoff(100);
        REQUIRE(cancelled == true);
        REQUIRE(!controller.isHandoffInProgress(100));
    }

    SECTION("Complete immediately after start") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        controller.completeHandoff(100, true);

        REQUIRE(controller.getPlayerPhase(100) == HandoffPhase::COMPLETED);
        REQUIRE(controller.getStats().successfulHandoffs == 1);
    }

    SECTION("Update with no active handoffs is safe") {
        Registry registry;
        controller.update(registry, 1000);
        REQUIRE(controller.getActiveHandoffs().empty());
    }

    SECTION("Multiple updates are idempotent for completed handoffs") {
        Position pos = makePosition(-60.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        controller.completeHandoff(100, true);

        Registry registry;
        controller.update(registry, 100);
        controller.update(registry, 200);
        controller.update(registry, 300);

        REQUIRE(!controller.isHandoffInProgress(100));
        REQUIRE(controller.getStats().successfulHandoffs == 1);
    }

    SECTION("Player at exact preparation distance boundary does not start handoff") {
        // 75m is the boundary (preparationDistance). Condition is < 75, not <=
        Position pos = makePosition(-75.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(!controller.isHandoffInProgress(100));
    }

    SECTION("Player just inside preparation distance starts handoff") {
        Position pos = makePosition(-74.0f, 0.0f);
        Velocity vel = makeVelocity(5.0f, 0.0f);

        controller.checkPlayerPosition(100, static_cast<EntityID>(1), 1, pos, vel, 0);
        REQUIRE(controller.isHandoffInProgress(100));
    }
}
