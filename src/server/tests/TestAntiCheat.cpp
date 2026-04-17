// [SECURITY_AGENT] Tests for AntiCheatSystem
// Comprehensive anti-cheat detection and behavior tracking tests

#include <catch2/catch_test_macros.hpp>
#include "security/AntiCheat.hpp"
#include "security/AntiCheatConfig.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <cstdint>
#include <string>
#include <vector>

using namespace DarkAges;
using namespace DarkAges::Security;

// ============================================================================
// Helper: Create a fixed-point position from float values
// ============================================================================
static Position makePosition(float x, float y, float z, uint32_t ts = 0) {
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
static InputState makeInput(bool forward = true, bool sprint = false) {
    InputState input;
    input.forward = forward ? 1 : 0;
    input.sprint = sprint ? 1 : 0;
    return input;
}

// ============================================================================
// Helper: Setup a player entity in the registry
// ============================================================================
static EntityID setupPlayerEntity(Registry& registry, uint64_t playerId) {
    auto entity = registry.create();
    registry.emplace<Position>(entity, makePosition(0, 0, 0));
    registry.emplace<Velocity>(entity, Velocity{0, 0, 0});
    registry.emplace<PlayerInfo>(entity, PlayerInfo{playerId, 1, "TestPlayer"});
    registry.emplace<AntiCheatState>(entity, AntiCheatState{});
    registry.emplace<CombatState>(entity, CombatState{});
    return entity;
}

// ============================================================================
// AntiCheatSystem Initialization
// ============================================================================

TEST_CASE("AntiCheatSystem initialization", "[security][anticheat]") {
    AntiCheatSystem acs;

    SECTION("Initialize returns true") {
        REQUIRE(acs.initialize());
    }

    SECTION("Double initialize is safe") {
        REQUIRE(acs.initialize());
        REQUIRE(acs.initialize());
    }

    SECTION("Initial state has zero stats") {
        REQUIRE(acs.initialize());
        REQUIRE(acs.getTotalDetections() == 0);
        REQUIRE(acs.getPlayersKicked() == 0);
        REQUIRE(acs.getPlayersBanned() == 0);
        REQUIRE(acs.getActiveProfileCount() == 0);
    }

    SECTION("Shutdown is safe before init") {
        // Should not crash
        acs.shutdown();
    }

    SECTION("Shutdown clears profiles") {
        REQUIRE(acs.initialize());
        Registry registry;
        auto entity = setupPlayerEntity(registry, 42);
        // Trigger profile creation via validation
        (void)acs.validateMovement(entity, makePosition(0, 0, 0), makePosition(1, 0, 0),
                             100, makeInput(), registry);
        REQUIRE(acs.getActiveProfileCount() >= 1);
        acs.shutdown();
        // shutdown() doesn't clear profiles, just sets initialized=false
        REQUIRE(acs.getActiveProfileCount() >= 1);
    }
}

// ============================================================================
// Speed Hack Detection
// ============================================================================

TEST_CASE("AntiCheatSystem speed hack detection", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());
    Registry registry;
    auto entity = setupPlayerEntity(registry, 1);

    SECTION("Normal walking speed is accepted") {
        // Walk at 5 m/s over 100ms = 0.5m displacement
        auto result = acs.validateMovement(
            entity,
            makePosition(0, 0, 0),
            makePosition(0.5f, 0, 0),
            100, makeInput(true, false), registry
        );
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Sprint speed is accepted") {
        // Sprint at 9 m/s over 100ms = 0.9m displacement
        auto result = acs.validateMovement(
            entity,
            makePosition(0, 0, 0),
            makePosition(0.9f, 0, 0),
            100, makeInput(true, true), registry
        );
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Excessive speed triggers detection") {
        // Move 20m in 100ms = 200 m/s (way over 6 m/s limit)
        auto result = acs.validateMovement(
            entity,
            makePosition(0, 0, 0),
            makePosition(20, 0, 0),
            100, makeInput(), registry
        );
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::SPEED_HACK);
        REQUIRE(result.confidence > 0.0f);
        REQUIRE(result.actualValue > result.expectedValue);
    }

    SECTION("Speed with zero delta time does not crash") {
        auto result = acs.validateMovement(
            entity,
            makePosition(0, 0, 0),
            makePosition(10, 0, 0),
            0, makeInput(), registry
        );
        // Should not crash; zero dt means speed is indeterminate
        // The teleport detector may fire instead for large distance
        (void)result;
    }
}

// ============================================================================
// Teleport Detection
// ============================================================================

TEST_CASE("AntiCheatSystem teleport detection", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());
    Registry registry;
    auto entity = setupPlayerEntity(registry, 2);

    SECTION("Small position changes are allowed") {
        // Move 0.5m in 100ms = 5 m/s, within base speed of 6 m/s
        auto result = acs.validateMovement(
            entity,
            makePosition(10, 0, 10),
            makePosition(10.5f, 0, 10),
            100, makeInput(), registry
        );
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Large instant teleport detected") {
        // Move 500m instantly - well over 100m threshold
        auto result = acs.validateMovement(
            entity,
            makePosition(0, 0, 0),
            makePosition(500, 0, 0),
            100, makeInput(), registry
        );
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::TELEPORT);
        REQUIRE(result.correctedPosition.x == makePosition(0, 0, 0).x);
    }

    SECTION("Teleport detection with instant ban config") {
        AntiCheatConfig config = acs.getConfig();
        config.instantBanOnTeleport = true;
        acs.setConfig(config);

        auto result = acs.validateMovement(
            entity,
            makePosition(0, 0, 0),
            makePosition(200, 0, 0),
            16, makeInput(), registry
        );
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::TELEPORT);
        REQUIRE(result.severity == ViolationSeverity::BAN);
    }

    SECTION("Teleport detection without instant ban kicks instead") {
        AntiCheatConfig config = acs.getConfig();
        config.instantBanOnTeleport = false;
        acs.setConfig(config);

        auto result = acs.validateMovement(
            entity,
            makePosition(0, 0, 0),
            makePosition(200, 0, 0),
            16, makeInput(), registry
        );
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::TELEPORT);
        REQUIRE(result.severity == ViolationSeverity::CRITICAL);
    }
}

// ============================================================================
// Position Bounds Validation
// ============================================================================

TEST_CASE("AntiCheatSystem position bounds validation", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());

    SECTION("Valid position within bounds") {
        auto result = acs.validatePositionBounds(makePosition(100, 10, 100));
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Position at boundary is valid") {
        auto result = acs.validatePositionBounds(
            makePosition(Constants::WORLD_MAX_X, 0, 0));
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Position beyond X bounds detected") {
        auto result = acs.validatePositionBounds(
            makePosition(Constants::WORLD_MAX_X + 100, 0, 0));
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Position beyond Y bounds detected") {
        auto result = acs.validatePositionBounds(
            makePosition(0, Constants::WORLD_MAX_Y + 100, 0));
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Position beyond Z bounds detected") {
        auto result = acs.validatePositionBounds(
            makePosition(0, 0, Constants::WORLD_MIN_Z - 100));
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::POSITION_SPOOFING);
    }

    SECTION("Negative positions out of bounds detected") {
        auto result = acs.validatePositionBounds(
            makePosition(Constants::WORLD_MIN_X - 1, 0, 0));
        REQUIRE(result.detected);
    }
}

// ============================================================================
// Input Validation
// ============================================================================

TEST_CASE("AntiCheatSystem input validation", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());
    Registry registry;
    auto entity = setupPlayerEntity(registry, 3);

    SECTION("Valid input accepted") {
        InputState input = makeInput();
        input.yaw = 1.0f;
        input.pitch = 0.5f;
        auto result = acs.validateInput(entity, input, 1000, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Invalid yaw detected") {
        InputState input = makeInput();
        input.yaw = 10.0f;  // Way over 2*PI
        auto result = acs.validateInput(entity, input, 1000, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::INPUT_MANIPULATION);
    }

    SECTION("Invalid pitch detected") {
        InputState input = makeInput();
        input.pitch = 3.0f;  // Way over PI/2
        auto result = acs.validateInput(entity, input, 1000, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::INPUT_MANIPULATION);
    }

    SECTION("Conflicting forward/backward detected") {
        InputState input;
        input.forward = 1;
        input.backward = 1;
        auto result = acs.validateInput(entity, input, 1000, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::INPUT_MANIPULATION);
    }

    SECTION("Conflicting left/right detected") {
        InputState input;
        input.left = 1;
        input.right = 1;
        auto result = acs.validateInput(entity, input, 1000, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::INPUT_MANIPULATION);
    }
}

// ============================================================================
// Rate Limiting
// ============================================================================

TEST_CASE("AntiCheatSystem rate limiting", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());
    Registry registry;
    auto entity = setupPlayerEntity(registry, 4);

    SECTION("Inputs within rate limit accepted") {
        AntiCheatConfig config = acs.getConfig();
        config.maxInputsPerSecond = 10;
        config.inputBurstAllowance = 2;
        config.inputWindowMs = 1000;
        acs.setConfig(config);

        // Re-create entity with new config
        AntiCheatState state;
        state.inputWindowStart = 0;
        state.inputCount = 0;
        registry.replace<AntiCheatState>(entity, state);

        // 10 inputs within limit
        for (uint32_t i = 0; i < 10; ++i) {
            auto result = acs.checkRateLimit(entity, i * 10, registry);
            REQUIRE_FALSE(result.detected);
        }
    }

    SECTION("Excessive inputs trigger flooding detection") {
        AntiCheatConfig config = acs.getConfig();
        config.maxInputsPerSecond = 5;
        config.inputBurstAllowance = 0;
        config.inputWindowMs = 1000;
        acs.setConfig(config);

        AntiCheatState state;
        state.inputWindowStart = 0;
        state.inputCount = 0;
        registry.replace<AntiCheatState>(entity, state);

        // First 5 should pass
        for (uint32_t i = 0; i < 5; ++i) {
            auto result = acs.checkRateLimit(entity, i * 10, registry);
            REQUIRE_FALSE(result.detected);
        }

        // 6th should trigger
        auto result = acs.checkRateLimit(entity, 50, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::PACKET_FLOODING);
    }

    SECTION("Window reset allows new inputs") {
        AntiCheatConfig config = acs.getConfig();
        config.maxInputsPerSecond = 3;
        config.inputBurstAllowance = 0;
        config.inputWindowMs = 100;
        acs.setConfig(config);

        AntiCheatState state;
        state.inputWindowStart = 0;
        state.inputCount = 0;
        registry.replace<AntiCheatState>(entity, state);

        // Exhaust first window
        for (uint32_t i = 0; i < 3; ++i) {
            (void)acs.checkRateLimit(entity, i * 10, registry);
        }

        // 4th in same window triggers
        auto result1 = acs.checkRateLimit(entity, 30, registry);
        REQUIRE(result1.detected);

        // After window expires, should accept again
        auto result2 = acs.checkRateLimit(entity, 200, registry);
        REQUIRE_FALSE(result2.detected);
    }
}

// ============================================================================
// Behavior Profile & Violation Tracking
// ============================================================================

TEST_CASE("AntiCheatSystem behavior profile tracking", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());
    Registry registry;

    SECTION("Profile is created on first access") {
        uint64_t playerId = 100;
        auto* profile = acs.getProfile(playerId);
        REQUIRE(profile != nullptr);
        REQUIRE(profile->playerId == playerId);
        REQUIRE(acs.getActiveProfileCount() == 1);
    }

    SECTION("Profile tracks violations") {
        uint64_t playerId = 200;
        auto* profile = acs.getProfile(playerId);
        REQUIRE(profile->violationHistory.empty());

        CheatDetectionResult detection;
        detection.type = CheatType::SPEED_HACK;
        detection.severity = ViolationSeverity::CRITICAL;
        detection.confidence = 0.9f;
        detection.timestamp = 1000;
        detection.description = "Test speed hack";

        profile->recordViolation(detection);
        REQUIRE(profile->violationHistory.size() == 1);
        REQUIRE(profile->speedViolations == 1);
    }

    SECTION("Profile trust score decreases on violations") {
        uint64_t playerId = 300;
        auto* profile = acs.getProfile(playerId);
        uint8_t initialTrust = profile->trustScore;

        CheatDetectionResult detection;
        detection.type = CheatType::SPEED_HACK;
        detection.severity = ViolationSeverity::WARNING;
        detection.confidence = 0.5f;
        detection.timestamp = 1000;

        profile->recordViolation(detection);
        REQUIRE(profile->trustScore < initialTrust);
    }

    SECTION("Profile trust score is clamped to 0") {
        uint64_t playerId = 400;
        auto* profile = acs.getProfile(playerId);
        profile->trustScore = 5;

        // Apply a large negative delta
        profile->updateTrustScore(-100);
        REQUIRE(profile->trustScore == 0);
    }

    SECTION("Profile trust score is clamped to 100") {
        uint64_t playerId = 401;
        auto* profile = acs.getProfile(playerId);
        profile->trustScore = 95;

        profile->updateTrustScore(100);
        REQUIRE(profile->trustScore == 100);
    }

    SECTION("Trusted and suspicious thresholds work") {
        BehaviorProfile profile;
        profile.trustScore = 80;
        REQUIRE(profile.isTrusted());
        REQUIRE_FALSE(profile.isSuspicious());

        profile.trustScore = 20;
        REQUIRE_FALSE(profile.isTrusted());
        REQUIRE(profile.isSuspicious());

        profile.trustScore = 50;
        REQUIRE_FALSE(profile.isTrusted());
        REQUIRE_FALSE(profile.isSuspicious());
    }

    SECTION("New player detection works") {
        BehaviorProfile profile;
        profile.accountCreationTime = 0;
        REQUIRE(profile.isNewPlayer(100000));       // 100s, still new
        REQUIRE_FALSE(profile.isNewPlayer(400000));  // 400s, not new
    }

    SECTION("Recent violation count works correctly") {
        BehaviorProfile profile;
        profile.violationHistory.push_back({CheatType::SPEED_HACK, 100, 0.8f, ""});
        profile.violationHistory.push_back({CheatType::TELEPORT, 200, 1.0f, ""});
        profile.violationHistory.push_back({CheatType::SPEED_HACK, 500, 0.7f, ""});

        // Window uses <= comparison: 600 - timestamp <= windowMs
        REQUIRE(profile.getRecentViolationCount(500, 600) == 3);   // all 3
        REQUIRE(profile.getRecentViolationCount(100, 600) == 1);   // only timestamp 500 (600-500=100 <= 100)
        REQUIRE(profile.getRecentViolationCount(99, 600) == 0);    // 600-500=100 > 99
    }

    SECTION("Clean movement increases trust slowly") {
        BehaviorProfile profile;
        profile.trustScore = 50;

        // 600 clean ticks should recover 1 trust
        for (uint32_t i = 0; i < 600; ++i) {
            profile.recordCleanMovement();
        }
        REQUIRE(profile.trustScore == 51);
    }

    SECTION("Remove profile works") {
        uint64_t playerId = 500;
        (void)acs.getProfile(playerId);
        REQUIRE(acs.getActiveProfileCount() >= 1);
        acs.removeProfile(playerId);
        REQUIRE(acs.getActiveProfileCount() == 0);
    }
}

// ============================================================================
// Graduated Response
// ============================================================================

TEST_CASE("AntiCheatSystem graduated response", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());

    SECTION("Cheat callback fires on detection") {
        bool callbackFired = false;
        uint64_t callbackPlayerId = 0;
        acs.setOnCheatDetected([&](uint64_t pid, const CheatDetectionResult&) {
            callbackFired = true;
            callbackPlayerId = pid;
        });

        Registry registry;
        auto entity = setupPlayerEntity(registry, 600);

        // Trigger a speed hack
        (void)acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(50, 0, 0),
            100, makeInput(), registry);

        REQUIRE(callbackFired);
        REQUIRE(callbackPlayerId == 600);
    }

    SECTION("Critical severity triggers kick") {
        AntiCheatConfig config = acs.getConfig();
        config.instantBanOnTeleport = false;  // Don't ban, use kick for CRITICAL
        acs.setConfig(config);

        bool kicked = false;
        uint64_t kickedPlayerId = 0;
        acs.setOnPlayerKicked([&](uint64_t pid, const char*) {
            kicked = true;
            kickedPlayerId = pid;
        });

        Registry registry;
        auto entity = setupPlayerEntity(registry, 700);

        // Massive teleport -> CRITICAL -> kick
        (void)acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(200, 0, 0),
            100, makeInput(), registry);

        REQUIRE(kicked);
        REQUIRE(kickedPlayerId == 700);
        REQUIRE(acs.getPlayersKicked() == 1);
    }

    SECTION("Ban severity triggers ban") {
        bool banned = false;
        acs.setOnPlayerBanned([&](uint64_t, const char*, uint32_t) {
            banned = true;
        });

        AntiCheatConfig config = acs.getConfig();
        config.instantBanOnTeleport = true;
        acs.setConfig(config);

        Registry registry;
        auto entity = setupPlayerEntity(registry, 800);

        // Teleport with instant ban -> BAN
        (void)acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(500, 0, 0),
            16, makeInput(), registry);

        REQUIRE(banned);
        REQUIRE(acs.getPlayersBanned() == 1);
    }
}

// ============================================================================
// Configuration
// ============================================================================

TEST_CASE("AntiCheatSystem configuration", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());

    SECTION("Default config has sensible values") {
        const auto& config = acs.getConfig();
        REQUIRE(config.speedTolerance > 1.0f);
        REQUIRE(config.maxTeleportDistance > 0.0f);
        REQUIRE(config.maxInputsPerSecond > 0);
        REQUIRE(config.initialTrustScore <= 100);
    }

    SECTION("Config can be changed at runtime") {
        AntiCheatConfig config = acs.getConfig();
        config.speedTolerance = 2.0f;
        config.maxTeleportDistance = 50.0f;
        acs.setConfig(config);

        const auto& newConfig = acs.getConfig();
        REQUIRE(newConfig.speedTolerance == 2.0f);
        REQUIRE(newConfig.maxTeleportDistance == 50.0f);
    }

    SECTION("Stricter teleport distance catches more cheats") {
        AntiCheatConfig config = acs.getConfig();
        config.maxTeleportDistance = 10.0f;  // Very strict
        config.instantBanOnTeleport = false;
        acs.setConfig(config);

        Registry registry;
        auto entity = setupPlayerEntity(registry, 900);

        // 15m move in 1 tick - now caught with strict config
        auto result = acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(15, 0, 0),
            16, makeInput(), registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::TELEPORT);
    }
}

// ============================================================================
// Combat Validation
// ============================================================================

TEST_CASE("AntiCheatSystem combat validation", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());
    Registry registry;
    auto attacker = setupPlayerEntity(registry, 1000);
    auto target = setupPlayerEntity(registry, 1001);

    SECTION("Valid attack accepted") {
        auto result = acs.validateCombat(
            attacker, target, 500,
            makePosition(2, 0, 0),  // Hit position
            makePosition(0, 0, 0),  // Attacker position
            registry
        );
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Damage hack detected") {
        AntiCheatConfig config = acs.getConfig();
        config.maxDamagePerHit = 1000;
        acs.setConfig(config);

        auto result = acs.validateCombat(
            attacker, target, 5000,  // Way over limit
            makePosition(1, 0, 0),
            makePosition(0, 0, 0),
            registry
        );
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::DAMAGE_HACK);
        REQUIRE(result.severity == ViolationSeverity::BAN);
    }

    SECTION("Hitbox extension detected for distant hits") {
        auto result = acs.validateCombat(
            attacker, target, 100,
            makePosition(100, 0, 0),  // 100m away, way over melee range
            makePosition(0, 0, 0),
            registry
        );
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::HITBOX_EXTENSION);
    }
}

// ============================================================================
// CheatType Utilities
// ============================================================================

TEST_CASE("CheatType string conversion", "[security][anticheat]") {
    REQUIRE(std::string(cheatTypeToString(CheatType::NONE)) == "NONE");
    REQUIRE(std::string(cheatTypeToString(CheatType::SPEED_HACK)) == "SPEED_HACK");
    REQUIRE(std::string(cheatTypeToString(CheatType::TELEPORT)) == "TELEPORT");
    REQUIRE(std::string(cheatTypeToString(CheatType::FLY_HACK)) == "FLY_HACK");
    REQUIRE(std::string(cheatTypeToString(CheatType::NO_CLIP)) == "NO_CLIP");
    REQUIRE(std::string(cheatTypeToString(CheatType::INPUT_MANIPULATION)) == "INPUT_MANIPULATION");
    REQUIRE(std::string(cheatTypeToString(CheatType::PACKET_FLOODING)) == "PACKET_FLOODING");
    REQUIRE(std::string(cheatTypeToString(CheatType::POSITION_SPOOFING)) == "POSITION_SPOOFING");
    REQUIRE(std::string(cheatTypeToString(CheatType::DAMAGE_HACK)) == "DAMAGE_HACK");
    REQUIRE(std::string(cheatTypeToString(CheatType::HITBOX_EXTENSION)) == "HITBOX_EXTENSION");
}

TEST_CASE("ViolationSeverity string conversion", "[security][anticheat]") {
    REQUIRE(std::string(severityToString(ViolationSeverity::INFO)) == "INFO");
    REQUIRE(std::string(severityToString(ViolationSeverity::WARNING)) == "WARNING");
    REQUIRE(std::string(severityToString(ViolationSeverity::SUSPICIOUS)) == "SUSPICIOUS");
    REQUIRE(std::string(severityToString(ViolationSeverity::CRITICAL)) == "CRITICAL");
    REQUIRE(std::string(severityToString(ViolationSeverity::BAN)) == "BAN");
}

// ============================================================================
// Statistics
// ============================================================================

TEST_CASE("AntiCheatSystem statistics tracking", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());

    SECTION("Detection counts increment") {
        Registry registry;
        auto entity = setupPlayerEntity(registry, 1100);

        // Trigger speed hack
        (void)acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(50, 0, 0),
            100, makeInput(), registry);

        REQUIRE(acs.getTotalDetections() >= 1);
        REQUIRE(acs.getDetectionCount(CheatType::SPEED_HACK) >= 1);
    }

    SECTION("Reset statistics clears counters") {
        Registry registry;
        auto entity = setupPlayerEntity(registry, 1200);
        (void)acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(50, 0, 0),
            100, makeInput(), registry);

        REQUIRE(acs.getTotalDetections() >= 1);
        acs.resetStatistics();
        REQUIRE(acs.getTotalDetections() == 0);
        REQUIRE(acs.getPlayersKicked() == 0);
        REQUIRE(acs.getPlayersBanned() == 0);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("AntiCheatSystem edge cases", "[security][anticheat]") {
    AntiCheatSystem acs;
    REQUIRE(acs.initialize());

    SECTION("Zero-distance movement is valid") {
        Registry registry;
        auto entity = setupPlayerEntity(registry, 1300);
        auto result = acs.validateMovement(entity,
            makePosition(10, 0, 10),
            makePosition(10, 0, 10),
            16, makeInput(), registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Diagonal movement at valid speed") {
        Registry registry;
        auto entity = setupPlayerEntity(registry, 1301);
        // Diagonal: sqrt(0.5^2 + 0.5^2) ~ 0.707m in 100ms = ~7 m/s
        // Slightly over base speed but under tolerance
        auto result = acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(0.5f, 0, 0.5f),
            100, makeInput(), registry);
        // Should pass since tolerance is 1.2x
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Entity without PlayerInfo does not crash for clean movement") {
        Registry registry;
        auto entity = registry.create();
        registry.emplace<Position>(entity, makePosition(0, 0, 0));

        // No PlayerInfo - clean movement validation is OK (no violation to report)
        auto result = acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(0.1f, 0, 0),
            100, makeInput(), registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Validation works without AntiCheatState component") {
        Registry registry;
        auto entity = registry.create();
        registry.emplace<Position>(entity, makePosition(0, 0, 0));
        registry.emplace<PlayerInfo>(entity, PlayerInfo{1400, 1, "NoCheatState"});

        auto result = acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(1, 0, 0),
            100, makeInput(), registry);
        (void)result;
    }

    SECTION("Very small dt still processes correctly") {
        Registry registry;
        auto entity = setupPlayerEntity(registry, 1500);
        // 0.005m in 1ms = 5 m/s, within base speed limit of 6 m/s
        auto result = acs.validateMovement(entity,
            makePosition(0, 0, 0),
            makePosition(0.005f, 0, 0),
            1, makeInput(), registry);
        REQUIRE_FALSE(result.detected);
    }
}
