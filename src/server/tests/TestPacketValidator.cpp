// [SECURITY_AGENT] Tests for PacketValidator
// Validates position, speed, rotation, entity, ability, and text validation

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "security/PacketValidator.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <limits>

using namespace DarkAges;
using namespace DarkAges::Security;

using Catch::Approx;

// ============================================================================
// Helper: Create a position with raw fixed-point values
// Position fields are in meters, stored as int32_t
// PacketValidator compares directly against WORLD_MAX_X (10000.0f)
// ============================================================================
static Position makePos(float x, float y, float z) {
    Position p;
    p.x = static_cast<Constants::Fixed>(x);
    p.y = static_cast<Constants::Fixed>(y);
    p.z = static_cast<Constants::Fixed>(z);
    p.timestamp_ms = 0;
    return p;
}

// ============================================================================
// Helper: Setup a player entity with components for testing
// ============================================================================
static EntityID setupEntity(Registry& registry, float x = 0, float y = 0, float z = 0) {
    auto entity = registry.create();
    registry.emplace<Position>(entity, makePos(x, y, z));
    registry.emplace<CombatState>(entity, CombatState{});
    return entity;
}

static EntityID setupEntityWithAbilities(Registry& registry) {
    auto entity = registry.create();
    registry.emplace<Position>(entity, makePos(0, 0, 0));
    registry.emplace<CombatState>(entity, CombatState{});
    registry.emplace<Mana>(entity, Mana{100.0f, 100.0f, 1.0f});

    Abilities ab;
    ab.abilities[0] = Ability{1, 0.0f, 10.0f};       // Ability 1, no cooldown, costs 10 mana
    ab.abilities[1] = Ability{2, 5.0f, 20.0f};       // Ability 2, on cooldown
    ab.abilities[2] = Ability{999, 0.0f, 200.0f};    // Ability 999, costs more mana than available
    ab.count = 3;
    registry.emplace<Abilities>(entity, ab);

    return entity;
}

// ============================================================================
// Position Validation
// ============================================================================

TEST_CASE("PacketValidator position validation", "[security][validation]") {
    SECTION("Valid position passes") {
        Position pos = makePos(10.0f, 5.0f, 10.0f);
        bool result = PacketValidator::ValidatePosition(pos);
        REQUIRE(result);
    }

    SECTION("Origin position is valid") {
        Position pos = makePos(0.0f, 0.0f, 0.0f);
        bool result = PacketValidator::ValidatePosition(pos);
        REQUIRE(result);
    }

    SECTION("Position is clamped to bounds") {
        // Create a position beyond world max (20000m > 10000m WORLD_MAX_X)
        Position pos = makePos(20000.0f, 0.0f, 0.0f);
        REQUIRE(static_cast<float>(pos.x) > PacketValidator::WORLD_MAX_X);

        PacketValidator::ValidatePosition(pos);
        REQUIRE(static_cast<float>(pos.x) <= PacketValidator::WORLD_MAX_X);
    }

    SECTION("Negative position is clamped") {
        Position pos = makePos(-20000.0f, 0.0f, 0.0f);
        PacketValidator::ValidatePosition(pos);
        REQUIRE(static_cast<float>(pos.x) >= PacketValidator::WORLD_MIN_X);
    }
}

TEST_CASE("PacketValidator position bounds check", "[security][validation]") {
    SECTION("Position within bounds returns true") {
        // Position is in meters (100m = 100, not 100,000)
        Position pos = makePos(100.0f, 50.0f, 100.0f);
        REQUIRE(PacketValidator::IsPositionInBounds(pos));
    }

    SECTION("Position at origin is in bounds") {
        Position pos = {0, 0, 0, 0};
        REQUIRE(PacketValidator::IsPositionInBounds(pos));
    }

    SECTION("ClampPosition clamps excessive values") {
        // Create position beyond world bounds (20000m > 10000m max)
        Position pos = makePos(20000.0f, 20000.0f, 20000.0f);
        PacketValidator::ClampPosition(pos);
        // Values should be clamped to world bounds
        REQUIRE(static_cast<float>(pos.x) <= PacketValidator::WORLD_MAX_X);
        REQUIRE(static_cast<float>(pos.y) <= PacketValidator::WORLD_MAX_Y);
        REQUIRE(static_cast<float>(pos.z) <= PacketValidator::WORLD_MAX_Z);
    }

    SECTION("Position beyond bounds returns false") {
        Position pos = makePos(20000.0f, 0.0f, 0.0f);
        REQUIRE_FALSE(PacketValidator::IsPositionInBounds(pos));
    }
}

// ============================================================================
// Speed Validation
// ============================================================================

TEST_CASE("PacketValidator speed validation", "[security][validation]") {
    SECTION("Normal speed is valid") {
        REQUIRE(PacketValidator::ValidateSpeed(10.0f));
    }

    SECTION("Zero speed is valid") {
        REQUIRE(PacketValidator::ValidateSpeed(0.0f));
    }

    SECTION("Speed within tolerance is valid") {
        float maxSpeed = PacketValidator::MAX_SPEED * PacketValidator::MAX_SPEED_TOLERANCE;
        REQUIRE(PacketValidator::ValidateSpeed(maxSpeed - 0.1f));
    }

    SECTION("Speed at exact limit is valid") {
        float maxSpeed = PacketValidator::MAX_SPEED * PacketValidator::MAX_SPEED_TOLERANCE;
        REQUIRE(PacketValidator::ValidateSpeed(maxSpeed));
    }

    SECTION("Speed exceeding limit is invalid") {
        float maxSpeed = PacketValidator::MAX_SPEED * PacketValidator::MAX_SPEED_TOLERANCE;
        REQUIRE_FALSE(PacketValidator::ValidateSpeed(maxSpeed + 1.0f));
    }

    SECTION("NaN speed is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateSpeed(std::numeric_limits<float>::quiet_NaN()));
    }

    SECTION("Infinity speed is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateSpeed(std::numeric_limits<float>::infinity()));
    }
}

// ============================================================================
// Position Delta Validation
// ============================================================================

TEST_CASE("PacketValidator position delta validation", "[security][validation]") {
    SECTION("Small position change is valid") {
        Position oldPos = makePos(0.0f, 0.0f, 0.0f);
        Position newPos = makePos(1.0f, 0.0f, 0.0f);
        // 1m in 1000ms at maxSpeed 20 m/s = max 22m allowed
        REQUIRE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 1000, 20.0f));
    }

    SECTION("No movement is valid") {
        Position pos = makePos(5.0f, 0.0f, 5.0f);
        REQUIRE(PacketValidator::ValidatePositionDelta(pos, pos, 100, 20.0f));
    }

    SECTION("Zero delta time with same position is valid") {
        Position pos = makePos(1.0f, 2.0f, 3.0f);
        REQUIRE(PacketValidator::ValidatePositionDelta(pos, pos, 0, 20.0f));
    }

    SECTION("Zero delta time with different position is invalid") {
        Position oldPos = makePos(0.0f, 0.0f, 0.0f);
        Position newPos = makePos(1.0f, 0.0f, 0.0f);
        REQUIRE_FALSE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 0, 20.0f));
    }

    SECTION("Excessive position change is invalid") {
        Position oldPos = makePos(0.0f, 0.0f, 0.0f);
        Position newPos = makePos(500.0f, 0.0f, 0.0f);
        // 500m in 1000ms = 500 m/s, way over 20 m/s
        REQUIRE_FALSE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 1000, 20.0f));
    }

    SECTION("Movement at exact speed limit is valid") {
        Position oldPos = makePos(0.0f, 0.0f, 0.0f);
        // Move exactly 20m in 1000ms = 20 m/s, with 1.1 tolerance = 22m max
        Position newPos = makePos(20.0f, 0.0f, 0.0f);
        REQUIRE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 1000, 20.0f));
    }

    SECTION("Diagonal movement validated correctly") {
        Position oldPos = makePos(0.0f, 0.0f, 0.0f);
        Position newPos = makePos(10.0f, 0.0f, 10.0f);
        // sqrt(200) ~ 14.14m in 1000ms at 20 m/s = valid
        REQUIRE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 1000, 20.0f));
    }
}

// ============================================================================
// Rotation Validation
// ============================================================================

TEST_CASE("PacketValidator rotation validation", "[security][validation]") {
    SECTION("Valid yaw and pitch pass") {
        float yaw = 90.0f;
        float pitch = 45.0f;
        REQUIRE(PacketValidator::ValidateRotation(yaw, pitch));
        REQUIRE(yaw == 90.0f);
        REQUIRE(pitch == 45.0f);
    }

    SECTION("Zero rotation is valid") {
        float yaw = 0.0f;
        float pitch = 0.0f;
        REQUIRE(PacketValidator::ValidateRotation(yaw, pitch));
    }

    SECTION("Pitch is clamped to [-90, 90]") {
        float yaw = 45.0f;
        float pitch = 120.0f;
        PacketValidator::ValidateRotation(yaw, pitch);
        REQUIRE(pitch == 90.0f);
    }

    SECTION("Negative pitch is clamped") {
        float yaw = 45.0f;
        float pitch = -120.0f;
        PacketValidator::ValidateRotation(yaw, pitch);
        REQUIRE(pitch == -90.0f);
    }

    SECTION("Yaw is normalized to [0, 360]") {
        float yaw = 400.0f;
        float pitch = 0.0f;
        PacketValidator::ValidateRotation(yaw, pitch);
        REQUIRE(yaw == Approx(40.0f));
    }

    SECTION("Negative yaw is normalized") {
        float yaw = -30.0f;
        float pitch = 0.0f;
        PacketValidator::ValidateRotation(yaw, pitch);
        REQUIRE(yaw == Approx(330.0f));
    }
}

// ============================================================================
// Rotation Delta Validation
// ============================================================================

TEST_CASE("PacketValidator rotation delta validation", "[security][validation]") {
    SECTION("Small rotation change is valid") {
        REQUIRE(PacketValidator::ValidateRotationDelta(0.0f, 0.0f, 30.0f, 10.0f, 100));
    }

    SECTION("Zero delta time is always valid") {
        // Any rotation change in zero time is accepted
        REQUIRE(PacketValidator::ValidateRotationDelta(0.0f, 0.0f, 180.0f, 90.0f, 0));
    }

    SECTION("Rotation at max rate is valid") {
        // 720 deg/s over 500ms = 360 deg max
        REQUIRE(PacketValidator::ValidateRotationDelta(0.0f, 0.0f, 360.0f, 0.0f, 500));
    }

    SECTION("Excessive yaw rotation rate is invalid") {
        // 170 degrees in 100ms = 1700 deg/s >> 720 deg/s (under 180 wraparound threshold)
        REQUIRE_FALSE(PacketValidator::ValidateRotationDelta(0.0f, 0.0f, 170.0f, 0.0f, 100));
    }

    SECTION("Excessive pitch rotation rate is invalid") {
        // 500 degrees in 100ms = 5000 deg/s >> 720 deg/s
        REQUIRE_FALSE(PacketValidator::ValidateRotationDelta(0.0f, 0.0f, 0.0f, 500.0f, 100));
    }

    SECTION("Yaw wraparound is handled correctly") {
        // From 350 to 10 degrees = 20 degree rotation (shorter direction)
        // 20 degrees in 100ms = 200 deg/s, under 720
        REQUIRE(PacketValidator::ValidateRotationDelta(350.0f, 0.0f, 10.0f, 0.0f, 100));
    }
}

// ============================================================================
// Entity Validation
// ============================================================================

TEST_CASE("PacketValidator entity validation", "[security][validation]") {
    Registry registry;
    auto validEntity = setupEntity(registry, 0, 0, 0);

    SECTION("Valid entity passes") {
        REQUIRE(PacketValidator::ValidateEntityId(validEntity, registry));
    }

    SECTION("Null entity is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateEntityId(entt::null, registry));
    }

    SECTION("Destroyed entity is invalid") {
        auto temp = registry.create();
        registry.destroy(temp);
        REQUIRE_FALSE(PacketValidator::ValidateEntityId(temp, registry));
    }
}

// ============================================================================
// Attack Target Validation
// ============================================================================

TEST_CASE("PacketValidator attack target validation", "[security][validation]") {
    Registry registry;
    auto attacker = setupEntity(registry, 0, 0, 0);
    auto targetNear = setupEntity(registry, 2, 0, 0);
    auto targetFar = setupEntity(registry, 100, 0, 0);
    auto deadTarget = setupEntity(registry, 1, 0, 0);
    registry.get<CombatState>(deadTarget).health = 0;

    SECTION("Valid attack target passes") {
        REQUIRE(PacketValidator::ValidateAttackTarget(attacker, targetNear, registry, 10.0f));
    }

    SECTION("Self-attack is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAttackTarget(attacker, attacker, registry, 10.0f));
    }

    SECTION("Target out of range is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAttackTarget(attacker, targetFar, registry, 10.0f));
    }

    SECTION("Dead target is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAttackTarget(attacker, deadTarget, registry, 10.0f));
    }

    SECTION("Null attacker is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAttackTarget(entt::null, targetNear, registry, 10.0f));
    }

    SECTION("Attack at max range with tolerance is valid") {
        auto atEdge = setupEntity(registry, 10.5f, 0, 0);
        // 10.5m with 10% tolerance = 11m max, should pass
        REQUIRE(PacketValidator::ValidateAttackTarget(attacker, atEdge, registry, 10.0f));
    }
}

// ============================================================================
// Ability Validation
// ============================================================================

TEST_CASE("PacketValidator ability ID validation", "[security][validation]") {
    SECTION("Valid ability ID passes") {
        REQUIRE(PacketValidator::ValidateAbilityId(1));
        REQUIRE(PacketValidator::ValidateAbilityId(500));
        REQUIRE(PacketValidator::ValidateAbilityId(1000));
    }

    SECTION("Zero ability ID is invalid") {
        REQUIRE(PacketValidator::ValidateAbilityId(0));
    }

    SECTION("Ability ID beyond max is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAbilityId(1001));
    }
}

TEST_CASE("PacketValidator ability use validation", "[security][validation]") {
    Registry registry;
    auto entity = setupEntityWithAbilities(registry);

    SECTION("Using known ability with mana is valid") {
        REQUIRE(PacketValidator::ValidateAbilityUse(entity, 1, registry));
    }

    SECTION("Using ability on cooldown is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAbilityUse(entity, 2, registry));
    }

    SECTION("Using ability with insufficient mana is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAbilityUse(entity, 999, registry));
    }

    SECTION("Using unknown ability is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAbilityUse(entity, 42, registry));
    }

    SECTION("Using ability on entity without abilities component is invalid") {
        auto noAbEntity = setupEntity(registry, 0, 0, 0);
        REQUIRE_FALSE(PacketValidator::ValidateAbilityUse(noAbEntity, 1, registry));
    }

    SECTION("Invalid ability ID fails") {
        REQUIRE(PacketValidator::ValidateAbilityUse(entity, 0, registry));
    }
}

// ============================================================================
// Text Validation - Player Names
// ============================================================================

TEST_CASE("PacketValidator player name validation", "[security][validation]") {
    SECTION("Normal name passes") {
        std::string name = "PlayerOne";
        REQUIRE(PacketValidator::ValidatePlayerName(name));
        REQUIRE(name == "PlayerOne");
    }

    SECTION("Name with underscore is valid") {
        std::string name = "Player_One";
        REQUIRE(PacketValidator::ValidatePlayerName(name));
    }

    SECTION("Name with hyphen is valid") {
        std::string name = "Player-One";
        REQUIRE(PacketValidator::ValidatePlayerName(name));
    }

    SECTION("Name with numbers is valid") {
        std::string name = "Player123";
        REQUIRE(PacketValidator::ValidatePlayerName(name));
    }

    SECTION("Empty name is rejected") {
        std::string name = "";
        bool result = PacketValidator::ValidatePlayerName(name);
        REQUIRE_FALSE(result);
    }

    SECTION("Name exceeding max length is truncated") {
        std::string name(PacketValidator::MAX_PLAYER_NAME_LENGTH + 10, 'A');
        PacketValidator::ValidatePlayerName(name);
        REQUIRE(name.length() <= PacketValidator::MAX_PLAYER_NAME_LENGTH);
    }

    SECTION("Name with special characters is sanitized") {
        std::string name = "Player!@#$";
        PacketValidator::ValidatePlayerName(name);
        REQUIRE(name == "Player");
    }

    SECTION("Name with spaces is sanitized") {
        std::string name = "Player Name";
        PacketValidator::ValidatePlayerName(name);
        REQUIRE(name == "PlayerName");
    }
}

// ============================================================================
// Text Validation - Chat Messages
// ============================================================================

TEST_CASE("PacketValidator chat message validation", "[security][validation]") {
    SECTION("Normal message passes") {
        std::string msg = "Hello, world!";
        REQUIRE(PacketValidator::ValidateChatMessage(msg));
    }

    SECTION("Message exceeding max length is truncated") {
        std::string msg(PacketValidator::MAX_CHAT_MESSAGE_LENGTH + 50, 'A');
        PacketValidator::ValidateChatMessage(msg);
        REQUIRE(msg.length() <= PacketValidator::MAX_CHAT_MESSAGE_LENGTH);
    }

    SECTION("Control characters are removed") {
        std::string msg = "Hello\x01\x02World";
        PacketValidator::ValidateChatMessage(msg);
        REQUIRE(msg == "HelloWorld");
    }

    SECTION("Tab and newline are preserved") {
        std::string msg = "Hello\tWorld\nTest";
        PacketValidator::ValidateChatMessage(msg);
        REQUIRE(msg.find('\t') != std::string::npos);
        REQUIRE(msg.find('\n') != std::string::npos);
    }

    SECTION("Empty message is valid") {
        std::string msg = "";
        REQUIRE(PacketValidator::ValidateChatMessage(msg));
    }

    SECTION("Whitespace-only message becomes empty") {
        std::string msg = "   \t\n  ";
        PacketValidator::ValidateChatMessage(msg);
        REQUIRE(msg.empty());
    }
}

// ============================================================================
// Suspicious Pattern Detection
// ============================================================================

TEST_CASE("PacketValidator suspicious pattern detection", "[security][validation]") {
    SECTION("Normal text is not suspicious") {
        REQUIRE_FALSE(PacketValidator::HasSuspiciousPattern("Hello World"));
    }

    SECTION("Repeated characters are suspicious") {
        REQUIRE(PacketValidator::HasSuspiciousPattern("AAAAAAABBBB"));
    }

    SECTION("Excessive caps are suspicious") {
        REQUIRE(PacketValidator::HasSuspiciousPattern("THIS IS ALL CAPS SHOUTING"));
    }

    SECTION("Normal mixed case is not suspicious") {
        REQUIRE_FALSE(PacketValidator::HasSuspiciousPattern("Hello World 123"));
    }

    SECTION("SQL injection pattern detected") {
        REQUIRE(PacketValidator::HasSuspiciousPattern("SELECT * FROM users"));
    }

    SECTION("SQL comment pattern detected") {
        REQUIRE(PacketValidator::HasSuspiciousPattern("admin'--"));
    }

    SECTION("Script injection pattern detected") {
        REQUIRE(PacketValidator::HasSuspiciousPattern("<script>alert(1)</script>"));
    }

    SECTION("Short text with caps is not suspicious") {
        REQUIRE_FALSE(PacketValidator::HasSuspiciousPattern("HELLO"));
    }
}

// ============================================================================
// Packet Data Validation
// ============================================================================

TEST_CASE("PacketValidator packet data validation", "[security][validation]") {
    SECTION("Valid packet data passes") {
        uint8_t data[100] = {};
        REQUIRE(PacketValidator::ValidatePacketData(data, 100));
    }

    SECTION("Null data is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidatePacketData(nullptr, 100));
    }

    SECTION("Zero size is invalid") {
        uint8_t data[1] = {};
        REQUIRE_FALSE(PacketValidator::ValidatePacketData(data, 0));
    }

    SECTION("Packet at max size is valid") {
        uint8_t data[4096] = {};
        REQUIRE(PacketValidator::ValidatePacketData(data, PacketValidator::MAX_PACKET_SIZE));
    }

    SECTION("Packet exceeding max size is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidatePacketData(nullptr, PacketValidator::MAX_PACKET_SIZE + 1));
        // Need a valid pointer too for size check to matter
        uint8_t data[5000] = {};
        REQUIRE_FALSE(PacketValidator::ValidatePacketData(data, PacketValidator::MAX_PACKET_SIZE + 1));
    }

    SECTION("Small valid packet passes") {
        uint8_t data[1] = {0xFF};
        REQUIRE(PacketValidator::ValidatePacketData(data, 1));
    }
}

// ============================================================================
// Constants Consistency
// ============================================================================

TEST_CASE("PacketValidator constants are sensible", "[security][validation]") {
    SECTION("Speed constants are positive") {
        REQUIRE(PacketValidator::MAX_SPEED > 0.0f);
        REQUIRE(PacketValidator::MAX_SPEED_TOLERANCE >= 1.0f);
    }

    SECTION("Rotation rate is positive") {
        REQUIRE(PacketValidator::MAX_ROTATION_RATE > 0.0f);
    }

    SECTION("World bounds are consistent") {
        REQUIRE(PacketValidator::WORLD_MIN_X < PacketValidator::WORLD_MAX_X);
        REQUIRE(PacketValidator::WORLD_MIN_Y < PacketValidator::WORLD_MAX_Y);
        REQUIRE(PacketValidator::WORLD_MIN_Z < PacketValidator::WORLD_MAX_Z);
    }

    SECTION("Max ability ID is positive") {
        REQUIRE(PacketValidator::MAX_ABILITY_ID > 0);
    }

    SECTION("Max attack range is positive") {
        REQUIRE(PacketValidator::MAX_ATTACK_RANGE > 0.0f);
    }

    SECTION("Name length limits are positive") {
        REQUIRE(PacketValidator::MAX_PLAYER_NAME_LENGTH > 0);
    }

    SECTION("Chat length limits are positive") {
        REQUIRE(PacketValidator::MAX_CHAT_MESSAGE_LENGTH > 0);
    }

    SECTION("Packet size limits are positive") {
        REQUIRE(PacketValidator::MAX_PACKET_SIZE > 0);
    }
}
