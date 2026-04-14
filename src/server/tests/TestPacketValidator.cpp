// Tests for PacketValidator — input validation and security

#include <catch2/catch_test_macros.hpp>
#include "security/PacketValidator.hpp"
#include "ecs/CoreTypes.hpp"

using namespace DarkAges;
using namespace DarkAges::Security;

TEST_CASE("PacketValidator Position Validation", "[security][packet]") {
    
    SECTION("Valid position in bounds") {
        Position pos{100.0f, 0.0f, 200.0f, 0};
        REQUIRE(PacketValidator::IsPositionInBounds(pos));
        REQUIRE(PacketValidator::ValidatePosition(pos));
    }
    
    SECTION("Position at world boundary is valid") {
        Position pos{PacketValidator::WORLD_MAX_X, 0.0f, PacketValidator::WORLD_MAX_Z, 0};
        REQUIRE(PacketValidator::IsPositionInBounds(pos));
    }
    
    SECTION("Position beyond world bounds is clamped") {
        Position pos{20000.0f, 500.0f, -30000.0f, 0};
        REQUIRE_FALSE(PacketValidator::IsPositionInBounds(pos));
        PacketValidator::ClampPosition(pos);
        REQUIRE(pos.x <= PacketValidator::WORLD_MAX_X);
        REQUIRE(pos.x >= PacketValidator::WORLD_MIN_X);
        REQUIRE(pos.z >= PacketValidator::WORLD_MIN_Z);
        REQUIRE(pos.z <= PacketValidator::WORLD_MAX_Z);
    }
    
    SECTION("Position with extreme Y is clamped") {
        Position pos{0.0f, 5000.0f, 0.0f, 0};
        PacketValidator::ClampPosition(pos);
        REQUIRE(pos.y <= PacketValidator::WORLD_MAX_Y);
        REQUIRE(pos.y >= PacketValidator::WORLD_MIN_Y);
    }
    
    SECTION("Negative coordinates clamped correctly") {
        Position pos{-50000.0f, -5000.0f, -50000.0f, 0};
        PacketValidator::ClampPosition(pos);
        REQUIRE(pos.x >= PacketValidator::WORLD_MIN_X);
        REQUIRE(pos.y >= PacketValidator::WORLD_MIN_Y);
        REQUIRE(pos.z >= PacketValidator::WORLD_MIN_Z);
    }
}

TEST_CASE("PacketValidator Speed Validation", "[security][packet]") {
    
    SECTION("Normal walking speed is valid") {
        REQUIRE(PacketValidator::ValidateSpeed(5.0f));
    }
    
    SECTION("Running speed is valid") {
        REQUIRE(PacketValidator::ValidateSpeed(15.0f));
    }
    
    SECTION("Speed at max tolerance is valid") {
        float maxAllowed = PacketValidator::MAX_SPEED * PacketValidator::MAX_SPEED_TOLERANCE;
        REQUIRE(PacketValidator::ValidateSpeed(maxAllowed));
    }
    
    SECTION("Speed exceeding max + tolerance is invalid") {
        float tooFast = PacketValidator::MAX_SPEED * PacketValidator::MAX_SPEED_TOLERANCE + 1.0f;
        REQUIRE_FALSE(PacketValidator::ValidateSpeed(tooFast));
    }
    
    SECTION("Negative speed is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateSpeed(-5.0f));
    }
    
    SECTION("Zero speed is valid") {
        REQUIRE(PacketValidator::ValidateSpeed(0.0f));
    }
}

TEST_CASE("PacketValidator Position Delta Validation", "[security][packet]") {
    
    SECTION("Normal movement is valid") {
        Position oldPos{0.0f, 0.0f, 0.0f, 0};
        Position newPos{5.0f, 0.0f, 5.0f, 0};
        REQUIRE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 1000, 10.0f));
    }
    
    SECTION("Standing still is valid") {
        Position oldPos{100.0f, 0.0f, 100.0f, 0};
        Position newPos{100.0f, 0.0f, 100.0f, 0};
        REQUIRE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 16, 10.0f));
    }
    
    SECTION("Teleportation is detected") {
        Position oldPos{0.0f, 0.0f, 0.0f, 0};
        Position newPos{5000.0f, 0.0f, 5000.0f, 0};
        REQUIRE_FALSE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 16, 10.0f));
    }
    
    SECTION("Speed hack is detected") {
        Position oldPos{0.0f, 0.0f, 0.0f, 0};
        Position newPos{50.0f, 0.0f, 0.0f, 0};
        // Moved 50m in 16ms = 3125 m/s — way over 10 m/s limit
        REQUIRE_FALSE(PacketValidator::ValidatePositionDelta(oldPos, newPos, 16, 10.0f));
    }
}

TEST_CASE("PacketValidator Rotation Validation", "[security][packet]") {
    
    SECTION("Valid rotation") {
        float yaw = 45.0f;
        float pitch = 30.0f;
        REQUIRE(PacketValidator::ValidateRotation(yaw, pitch));
    }
    
    SECTION("Pitch clamped when out of range") {
        float yaw = 0.0f;
        float pitch = 100.0f;
        REQUIRE(PacketValidator::ValidateRotation(yaw, pitch));
        REQUIRE(pitch <= 90.0f);
    }
    
    SECTION("Negative pitch clamped") {
        float yaw = 180.0f;
        float pitch = -100.0f;
        REQUIRE(PacketValidator::ValidateRotation(yaw, pitch));
        REQUIRE(pitch >= -90.0f);
    }
    
    SECTION("Rotation delta with aimbot speed is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateRotationDelta(
            0.0f, 0.0f, 180.0f, 0.0f, 16));
    }
    
    SECTION("Normal rotation delta is valid") {
        REQUIRE(PacketValidator::ValidateRotationDelta(
            0.0f, 0.0f, 5.0f, 2.0f, 1000));
    }
}

TEST_CASE("PacketValidator Ability Validation", "[security][packet]") {
    
    SECTION("Valid ability ID") {
        REQUIRE(PacketValidator::ValidateAbilityId(1));
        REQUIRE(PacketValidator::ValidateAbilityId(500));
    }
    
    SECTION("Ability ID 0 is valid (basic attack)") {
        REQUIRE(PacketValidator::ValidateAbilityId(0));
    }
    
    SECTION("Ability ID beyond max is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidateAbilityId(PacketValidator::MAX_ABILITY_ID + 1));
    }
}

TEST_CASE("PacketValidator Text Validation", "[security][packet]") {
    
    SECTION("Valid player name") {
        std::string name = "Player123";
        REQUIRE(PacketValidator::ValidatePlayerName(name));
    }
    
    SECTION("Empty name is invalid") {
        std::string name = "";
        REQUIRE_FALSE(PacketValidator::ValidatePlayerName(name));
    }
    
    SECTION("Name exceeding max length is rejected") {
        std::string name(PacketValidator::MAX_PLAYER_NAME_LENGTH + 10, 'A');
        REQUIRE_FALSE(PacketValidator::ValidatePlayerName(name));
    }
    
    SECTION("Valid chat message") {
        std::string msg = "Hello, world!";
        REQUIRE(PacketValidator::ValidateChatMessage(msg));
    }
    
    SECTION("Empty chat message is valid") {
        std::string msg = "";
        REQUIRE(PacketValidator::ValidateChatMessage(msg));
    }
    
    SECTION("Chat exceeding max length is rejected") {
        std::string msg(PacketValidator::MAX_CHAT_MESSAGE_LENGTH + 10, 'A');
        REQUIRE_FALSE(PacketValidator::ValidateChatMessage(msg));
    }
}

TEST_CASE("PacketValidator Suspicious Pattern Detection", "[security][packet]") {
    
    SECTION("Normal text has no suspicious pattern") {
        REQUIRE_FALSE(PacketValidator::HasSuspiciousPattern("Hello world"));
    }
    
    SECTION("Repeated characters detected") {
        REQUIRE(PacketValidator::HasSuspiciousPattern("aaaaaaaaaaaaaaa"));
    }
    
    SECTION("Normal repeated chars are fine") {
        REQUIRE_FALSE(PacketValidator::HasSuspiciousPattern("aa"));
    }
}

TEST_CASE("PacketValidator Packet Data Validation", "[security][packet]") {
    
    SECTION("Valid packet data") {
        char data[64] = {};
        REQUIRE(PacketValidator::ValidatePacketData(data, 64));
    }
    
    SECTION("Zero-size packet is invalid") {
        char data[1] = {};
        REQUIRE_FALSE(PacketValidator::ValidatePacketData(data, 0));
    }
    
    SECTION("Packet exceeding max size is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidatePacketData(nullptr, PacketValidator::MAX_PACKET_SIZE + 1));
    }
    
    SECTION("Null data pointer is invalid") {
        REQUIRE_FALSE(PacketValidator::ValidatePacketData(nullptr, 64));
    }
}
