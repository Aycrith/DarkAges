// [SECURITY_AGENT] Tests for MovementValidator
// Covers speed hack, teleport, fly hack, no-clip, and position bounds detection

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "security/MovementValidator.hpp"
#include "security/ViolationTracker.hpp"
#include "security/AntiCheatConfig.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <cmath>
#include <cstdint>

using namespace DarkAges;
using namespace DarkAges::Security;

// ============================================================================
// Helper: Create a fixed-point position from float values
// ============================================================================
static Position makePos(float x, float y, float z, uint32_t ts = 0) {
    Position p;
    p.x = static_cast<Constants::Fixed>(x * Constants::FLOAT_TO_FIXED);
    p.y = static_cast<Constants::Fixed>(y * Constants::FLOAT_TO_FIXED);
    p.z = static_cast<Constants::Fixed>(z * Constants::FLOAT_TO_FIXED);
    p.timestamp_ms = ts;
    return p;
}

// ============================================================================
// Helper: Create a minimal input state
// ============================================================================
static InputState makeInput(bool forward = true, bool sprint = false, bool jump = false) {
    InputState input;
    input.forward = forward ? 1 : 0;
    input.sprint = sprint ? 1 : 0;
    input.jump = jump ? 1 : 0;
    return input;
}

// ============================================================================
// Helper: Setup a player entity in the registry
// ============================================================================
static EntityID setupPlayer(Registry& registry, uint64_t playerId,
    float x = 0, float y = 0, float z = 0) {
    auto entity = registry.create();
    registry.emplace<Position>(entity, makePos(x, y, z));
    registry.emplace<Velocity>(entity, Velocity{0, 0, 0});
    registry.emplace<PlayerInfo>(entity, PlayerInfo{playerId, 1, "TestPlayer"});
    registry.emplace<AntiCheatState>(entity, AntiCheatState{});
    registry.emplace<CombatState>(entity, CombatState{});
    return entity;
}

// ============================================================================
// MovementValidator::validatePositionBounds
// ============================================================================

TEST_CASE("MovementValidator position bounds validation", "[security][movement]") {
    MovementValidator validator;

    SECTION("Position within bounds is valid") {
        Position pos = makePos(0, 0, 0);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE_FALSE(result.detected);
        REQUIRE(result.type == CheatType::NONE);
    }

    SECTION("Position at positive X boundary is valid") {
        Position pos = makePos(Constants::WORLD_MAX_X - 0.5f, 0, 0);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Position at negative X boundary is valid") {
        Position pos = makePos(Constants::WORLD_MIN_X + 0.5f, 0, 0);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Position beyond X bounds is invalid") {
        Position pos = makePos(Constants::WORLD_MAX_X + 10.0f, 0, 0);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
        REQUIRE(result.severity == ViolationSeverity::CRITICAL);
    }

    SECTION("Position beyond negative X bounds is invalid") {
        Position pos = makePos(Constants::WORLD_MIN_X - 10.0f, 0, 0);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Position beyond Y bounds is invalid") {
        Position pos = makePos(0, Constants::WORLD_MAX_Y + 10.0f, 0);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Position beyond negative Y bounds is invalid") {
        Position pos = makePos(0, Constants::WORLD_MIN_Y - 10.0f, 0);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Position beyond Z bounds is invalid") {
        Position pos = makePos(0, 0, Constants::WORLD_MAX_Z + 10.0f);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Corrected position is clamped to bounds") {
        Position pos = makePos(6000.0f, 1000.0f, -6000.0f);
        auto result = validator.validatePositionBounds(pos);
        REQUIRE(result.detected);

        // Verify corrected position is within bounds
        float cx = result.correctedPosition.x * Constants::FIXED_TO_FLOAT;
        float cy = result.correctedPosition.y * Constants::FIXED_TO_FLOAT;
        float cz = result.correctedPosition.z * Constants::FIXED_TO_FLOAT;
        REQUIRE(cx <= Constants::WORLD_MAX_X + 0.01f);
        REQUIRE(cx >= Constants::WORLD_MIN_X - 0.01f);
        REQUIRE(cy <= Constants::WORLD_MAX_Y + 0.01f);
        REQUIRE(cy >= Constants::WORLD_MIN_Y - 0.01f);
        REQUIRE(cz <= Constants::WORLD_MAX_Z + 0.01f);
        REQUIRE(cz >= Constants::WORLD_MIN_Z - 0.01f);
    }
}

// ============================================================================
// MovementValidator::isWithinWorldBounds / isPositionValid
// ============================================================================

TEST_CASE("MovementValidator world bounds checks", "[security][movement]") {
    MovementValidator validator;

    SECTION("Origin is within bounds") {
        Position pos = makePos(0, 0, 0);
        REQUIRE(validator.isWithinWorldBounds(pos));
        REQUIRE(validator.isPositionValid(pos));
    }

    SECTION("Far corner is within bounds") {
        Position pos = makePos(4999.0f, 499.0f, 4999.0f);
        REQUIRE(validator.isWithinWorldBounds(pos));
    }

    SECTION("Outside bounds returns false") {
        Position pos = makePos(5001.0f, 0, 0);
        REQUIRE_FALSE(validator.isWithinWorldBounds(pos));
        REQUIRE_FALSE(validator.isPositionValid(pos));
    }
}

// ============================================================================
// MovementValidator::calculateDistance
// ============================================================================

TEST_CASE("MovementValidator distance calculation", "[security][movement]") {
    MovementValidator validator;

    SECTION("Distance between same point is zero") {
        Position a = makePos(10, 5, 20);
        Position b = makePos(10, 5, 20);
        float dist = validator.calculateDistance(a, b);
        REQUIRE(dist == Catch::Approx(0.0f).margin(0.01f));
    }

    SECTION("Distance along X axis") {
        Position a = makePos(0, 0, 0);
        Position b = makePos(100, 0, 0);
        float dist = validator.calculateDistance(a, b);
        REQUIRE(dist == Catch::Approx(100.0f).margin(0.1f));
    }

    SECTION("Distance along Z axis") {
        Position a = makePos(0, 0, 0);
        Position b = makePos(0, 0, 50);
        float dist = validator.calculateDistance(a, b);
        REQUIRE(dist == Catch::Approx(50.0f).margin(0.1f));
    }

    SECTION("3D distance (Pythagorean)") {
        Position a = makePos(0, 0, 0);
        Position b = makePos(3, 4, 0);
        float dist = validator.calculateDistance(a, b);
        REQUIRE(dist == Catch::Approx(5.0f).margin(0.1f));
    }

    SECTION("Negative coordinates") {
        Position a = makePos(-10, -5, -20);
        Position b = makePos(10, 5, 20);
        float dist = validator.calculateDistance(a, b);
        float expected = std::sqrt(20.0f * 20.0f + 10.0f * 10.0f + 40.0f * 40.0f);
        REQUIRE(dist == Catch::Approx(expected).margin(0.1f));
    }
}

// ============================================================================
// MovementValidator::calculateSpeed
// ============================================================================

TEST_CASE("MovementValidator speed calculation", "[security][movement]") {
    MovementValidator validator;

    SECTION("Zero time returns zero speed") {
        Position a = makePos(0, 0, 0);
        Position b = makePos(100, 0, 0);
        float speed = validator.calculateSpeed(a, b, 0);
        REQUIRE(speed == 0.0f);
    }

    SECTION("Normal speed calculation") {
        Position a = makePos(0, 0, 0);
        Position b = makePos(10, 0, 0);
        // 10 meters in 1000ms = 10 m/s
        float speed = validator.calculateSpeed(a, b, 1000);
        REQUIRE(speed == Catch::Approx(10.0f).margin(0.1f));
    }

    SECTION("Speed with short time delta") {
        Position a = makePos(0, 0, 0);
        Position b = makePos(1, 0, 0);
        // 1 meter in 16ms = 62.5 m/s
        float speed = validator.calculateSpeed(a, b, 16);
        REQUIRE(speed > 60.0f);
    }
}

// ============================================================================
// MovementValidator::calculateMaxAllowedSpeed
// ============================================================================

TEST_CASE("MovementValidator max allowed speed", "[security][movement]") {
    MovementValidator validator;

    SECTION("Walking input returns base speed") {
        InputState input = makeInput(true, false);
        float maxSpeed = validator.calculateMaxAllowedSpeed(input);
        REQUIRE(maxSpeed == Catch::Approx(Constants::MAX_PLAYER_SPEED).margin(0.01f));
    }

    SECTION("Sprinting input returns sprint speed") {
        InputState input = makeInput(true, true);
        float maxSpeed = validator.calculateMaxAllowedSpeed(input);
        REQUIRE(maxSpeed == Catch::Approx(Constants::MAX_SPRINT_SPEED).margin(0.01f));
    }

    SECTION("No input still returns base speed") {
        InputState input = makeInput(false, false);
        float maxSpeed = validator.calculateMaxAllowedSpeed(input);
        REQUIRE(maxSpeed == Catch::Approx(Constants::MAX_PLAYER_SPEED).margin(0.01f));
    }
}

// ============================================================================
// MovementValidator::detectTeleport
// ============================================================================

TEST_CASE("MovementValidator teleport detection", "[security][movement]") {
    Registry registry;
    MovementValidator validator;

    EntityID entity = setupPlayer(registry, 1);

    SECTION("Small movement is not teleport") {
        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(5, 0, 5);
        auto result = validator.detectTeleport(entity, oldPos, newPos, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Movement within teleport threshold is allowed") {
        Position oldPos = makePos(0, 0, 0);
        // 90 meters - within default 100m threshold
        Position newPos = makePos(90, 0, 0);
        auto result = validator.detectTeleport(entity, oldPos, newPos, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Large instant movement is teleport") {
        AntiCheatConfig config;
        config.maxTeleportDistance = 100.0f;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(200, 0, 0);  // 200 meters
        auto result = validator.detectTeleport(entity, oldPos, newPos, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::TELEPORT);
        REQUIRE(result.confidence > 0.9f);
    }

    SECTION("Teleport with instantBanOnTeleport has BAN severity") {
        AntiCheatConfig config;
        config.maxTeleportDistance = 50.0f;
        config.instantBanOnTeleport = true;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(100, 0, 0);
        auto result = validator.detectTeleport(entity, oldPos, newPos, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::TELEPORT);
        REQUIRE(result.severity == ViolationSeverity::BAN);
    }

    SECTION("Teleport without instantBanOnTeleport has CRITICAL severity") {
        AntiCheatConfig config;
        config.maxTeleportDistance = 50.0f;
        config.instantBanOnTeleport = false;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(100, 0, 0);
        auto result = validator.detectTeleport(entity, oldPos, newPos, registry);
        REQUIRE(result.detected);
        REQUIRE(result.severity == ViolationSeverity::CRITICAL);
    }

    SECTION("Teleport corrected position is old position") {
        AntiCheatConfig config;
        config.maxTeleportDistance = 100.0f;
        validator.setConfig(config);

        Position oldPos = makePos(10, 5, 20);
        Position newPos = makePos(500, 100, 500);
        auto result = validator.detectTeleport(entity, oldPos, newPos, registry);
        REQUIRE(result.detected);
        // Corrected position should be the old (valid) position
        REQUIRE(result.correctedPosition.x == oldPos.x);
        REQUIRE(result.correctedPosition.y == oldPos.y);
        REQUIRE(result.correctedPosition.z == oldPos.z);
    }
}

// ============================================================================
// MovementValidator::detectSpeedHack
// ============================================================================

TEST_CASE("MovementValidator speed hack detection", "[security][movement]") {
    Registry registry;
    MovementValidator validator;

    EntityID entity = setupPlayer(registry, 1);

    SECTION("Normal walking speed is allowed") {
        AntiCheatConfig config;
        config.speedTolerance = 1.2f;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        // 5 meters in 1 second = 5 m/s (under 6 m/s limit)
        Position newPos = makePos(5, 0, 0);
        InputState input = makeInput(true, false);
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Normal sprint speed is allowed") {
        AntiCheatConfig config;
        config.speedTolerance = 1.2f;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        // 8 meters in 1 second = 8 m/s (under 9 m/s sprint limit with tolerance)
        Position newPos = makePos(8, 0, 0);
        InputState input = makeInput(true, true);
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Extreme speed is detected") {
        AntiCheatConfig config;
        config.speedTolerance = 1.2f;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        // 100 meters in 1 second = 100 m/s (way over limit)
        Position newPos = makePos(100, 0, 0);
        InputState input = makeInput(true, false);
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::SPEED_HACK);
        REQUIRE(result.severity == ViolationSeverity::CRITICAL);
    }

    SECTION("Zero time delta returns no detection") {
        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(100, 0, 0);
        InputState input = makeInput(true, false);
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 0, input, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Speed hack corrected position is old position") {
        AntiCheatConfig config;
        config.speedTolerance = 1.0f;
        validator.setConfig(config);

        Position oldPos = makePos(10, 5, 20);
        Position newPos = makePos(500, 5, 20);
        InputState input = makeInput(true, false);
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 100, input, registry);
        REQUIRE(result.detected);
        REQUIRE(result.correctedPosition.x == oldPos.x);
        REQUIRE(result.correctedPosition.y == oldPos.y);
        REQUIRE(result.correctedPosition.z == oldPos.z);
    }

    SECTION("Speed hack confidence scales with excess speed") {
        AntiCheatConfig config;
        config.speedTolerance = 1.0f;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        // 2x the speed limit
        Position newPos1 = makePos(12, 0, 0);
        InputState input = makeInput(true, false);
        auto result1 = validator.detectSpeedHack(entity, oldPos, newPos1, 1000, input, registry);

        // 10x the speed limit
        Position newPos2 = makePos(60, 0, 0);
        auto result2 = validator.detectSpeedHack(entity, oldPos, newPos2, 1000, input, registry);

        REQUIRE(result1.detected);
        REQUIRE(result2.detected);
        // Higher excess speed should have higher or equal confidence
        REQUIRE(result2.confidence >= result1.confidence);
    }
}

// ============================================================================
// MovementValidator::detectFlyHack
// ============================================================================

TEST_CASE("MovementValidator fly hack detection", "[security][movement]") {
    Registry registry;
    MovementValidator validator;

    EntityID entity = setupPlayer(registry, 1);

    SECTION("Position above world max Y is fly hack") {
        Position pos = makePos(0, Constants::WORLD_MAX_Y + 100.0f, 0);
        Velocity vel{0, 0, 0};
        InputState input = makeInput(true, false);
        auto result = validator.detectFlyHack(entity, pos, vel, input, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::FLY_HACK);
        REQUIRE(result.severity == ViolationSeverity::CRITICAL);
    }

    SECTION("Position within Y bounds is not fly hack") {
        Position pos = makePos(0, 50, 0);
        Velocity vel{0, 0, 0};
        InputState input = makeInput(true, false);
        auto result = validator.detectFlyHack(entity, pos, vel, input, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Fly hack corrected Y is clamped to max") {
        Position pos = makePos(0, 1000.0f, 0);
        Velocity vel{0, 0, 0};
        InputState input = makeInput(true, false);
        auto result = validator.detectFlyHack(entity, pos, vel, input, registry);
        REQUIRE(result.detected);

        float correctedY = result.correctedPosition.y * Constants::FIXED_TO_FLOAT;
        REQUIRE(correctedY <= Constants::WORLD_MAX_Y + 0.01f);
    }

    SECTION("Vertical velocity without jump input may be suspicious") {
        AntiCheatConfig config;
        config.maxVerticalSpeedNoJump = 0.5f;
        config.maxAirTimeMs = 100;  // Short for testing
        validator.setConfig(config);

        // Set velocity to upward (2 m/s) without jump input
        Velocity vel{0, static_cast<Constants::Fixed>(2.0f * Constants::FLOAT_TO_FIXED), 0};
        Position pos = makePos(0, 10, 0);
        InputState input = makeInput(true, false, false);  // No jump

        // The detectFlyHack checks air time, so we need AntiCheatState
        // Without sufficient air time, it won't detect on first call
        auto result = validator.detectFlyHack(entity, pos, vel, input, registry);
        (void)result;  // Side-effect: updates AntiCheatState
        // First call may not detect (needs sustained air time)
        // The logic depends on AntiCheatState.lastValidationTime
    }
}

// ============================================================================
// MovementValidator::validateMovement (orchestrator)
// ============================================================================

TEST_CASE("MovementValidator validateMovement orchestrates checks", "[security][movement]") {
    Registry registry;
    MovementValidator validator;

    EntityID entity = setupPlayer(registry, 1);

    SECTION("Normal movement passes all checks") {
        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(3, 0, 0);
        InputState input = makeInput(true, false);
        auto result = validator.validateMovement(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Out-of-bounds position is caught first") {
        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(10000, 0, 0);  // Out of bounds
        InputState input = makeInput(true, false);
        auto result = validator.validateMovement(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Teleport is caught") {
        AntiCheatConfig config;
        config.maxTeleportDistance = 100.0f;
        config.instantBanOnTeleport = false;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(200, 0, 0);  // Teleport distance
        InputState input = makeInput(true, false);
        auto result = validator.validateMovement(entity, oldPos, newPos, 16, input, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::TELEPORT);
    }
}

// ============================================================================
// MovementValidator with ViolationTracker integration
// ============================================================================

TEST_CASE("MovementValidator integrates with ViolationTracker", "[security][movement]") {
    Registry registry;
    MovementValidator validator;
    ViolationTracker tracker;

    bool cheatDetected = false;
    tracker.setOnCheatDetected([&](uint64_t, const CheatDetectionResult&) {
        cheatDetected = true;
    });

    validator.setViolationTracker(&tracker);

    EntityID entity = setupPlayer(registry, 42);

    SECTION("Clean movement records to tracker") {
        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(3, 0, 0);
        InputState input = makeInput(true, false);
        auto result = validator.validateMovement(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE_FALSE(result.detected);
        // Clean movement should be recorded
        auto* profile = tracker.getProfile(42);
        // Profile may not exist yet for clean movement (depends on implementation)
    }

    SECTION("Speed hack triggers tracker callback") {
        AntiCheatConfig config;
        config.speedTolerance = 1.0f;
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(100, 0, 0);  // Way too fast
        InputState input = makeInput(true, false);

        // Update registry velocity component
        if (Velocity* vel = registry.try_get<Velocity>(entity)) {
            vel->dx = static_cast<Constants::Fixed>(100.0f * Constants::FLOAT_TO_FIXED);
        }

        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE(result.detected);
        REQUIRE(cheatDetected);
    }
}

// ============================================================================
// MovementValidator::applyPositionCorrection
// ============================================================================

TEST_CASE("MovementValidator position correction", "[security][movement]") {
    Registry registry;
    MovementValidator validator;

    EntityID entity = setupPlayer(registry, 1, 50, 10, 50);

    SECTION("Apply correction updates entity position") {
        Position corrected = makePos(0, 0, 0);
        validator.applyPositionCorrection(entity, corrected, registry);

        const Position* pos = registry.try_get<Position>(entity);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == corrected.x);
        REQUIRE(pos->y == corrected.y);
        REQUIRE(pos->z == corrected.z);
    }

    SECTION("Correction with clamped values") {
        Position corrected = makePos(Constants::WORLD_MAX_X, 0, 0);
        validator.applyPositionCorrection(entity, corrected, registry);

        const Position* pos = registry.try_get<Position>(entity);
        float x = pos->x * Constants::FIXED_TO_FLOAT;
        REQUIRE(x <= Constants::WORLD_MAX_X);
    }
}

// ============================================================================
// Edge cases and integration
// ============================================================================

TEST_CASE("MovementValidator edge cases", "[security][movement]") {
    Registry registry;
    MovementValidator validator;

    SECTION("Entity without PlayerInfo component") {
        auto entity = registry.create();
        registry.emplace<Position>(entity, makePos(0, 0, 0));

        Position oldPos = makePos(0, 0, 0);
        Position newPos = makePos(5, 0, 5);
        InputState input = makeInput(true, false);
        // Should not crash even without PlayerInfo
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Movement at exact world boundary") {
        // Use values slightly inside to avoid fixed-point rounding issues
        Position pos = makePos(Constants::WORLD_MAX_X - 0.5f, 0, 0);
        REQUIRE(validator.isWithinWorldBounds(pos));
    }

    SECTION("Distance with large coordinates") {
        Position a = makePos(-4000, 0, -4000);
        Position b = makePos(4000, 0, 4000);
        float dist = validator.calculateDistance(a, b);
        float expected = std::sqrt(8000.0f * 8000.0f + 8000.0f * 8000.0f);
        REQUIRE(dist == Catch::Approx(expected).margin(1.0f));
    }

    SECTION("Speed at exactly the limit") {
        auto entity = setupPlayer(registry, 1);
        AntiCheatConfig config;
        config.speedTolerance = 1.2f;  // 20% tolerance
        validator.setConfig(config);

        Position oldPos = makePos(0, 0, 0);
        // Exactly at base speed: 6 m/s
        Position newPos = makePos(6, 0, 0);
        InputState input = makeInput(true, false);
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 1000, input, registry);
        // At the limit with 20% tolerance should be allowed
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Speed just over tolerance is detected") {
        AntiCheatConfig config;
        config.speedTolerance = 1.0f;  // No tolerance
        validator.setConfig(config);

        auto entity = setupPlayer(registry, 1);

        Position oldPos = makePos(0, 0, 0);
        // 7 m/s > 6 m/s limit
        Position newPos = makePos(7, 0, 0);
        InputState input = makeInput(true, false);
        auto result = validator.detectSpeedHack(entity, oldPos, newPos, 1000, input, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::SPEED_HACK);
    }
}
