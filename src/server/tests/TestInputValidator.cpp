#include <catch2/catch_test_macros.hpp>
#include "security/InputValidator.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"

using namespace DarkAges::Security;
using namespace DarkAges;

// Helper to create Position from float coords
Position makePosition(float x, float y, float z) {
    Position p;
    p.x = static_cast<Constants::Fixed>(x * Constants::FIXED_PRECISION);
    p.y = static_cast<Constants::Fixed>(y * Constants::FIXED_PRECISION);
    p.z = static_cast<Constants::Fixed>(z * Constants::FIXED_PRECISION);
    return p;
}

// ============================================================================
// Position Validation
// ============================================================================

TEST_CASE("[input-validator] Valid positions within world bounds", "[security]") {
    // Center of world
    REQUIRE(InputValidator::isValidPosition(makePosition(0.0f, 0.0f, 0.0f)));

    // Near boundaries (but inside)
    REQUIRE(InputValidator::isValidPosition(makePosition(4999.0f, 499.0f, 4999.0f)));
    REQUIRE(InputValidator::isValidPosition(makePosition(-4999.0f, -99.0f, -4999.0f)));

    // Exactly at boundaries
    REQUIRE(InputValidator::isValidPosition(makePosition(4999.9f, 499.9f, 4999.9f)));
    REQUIRE(InputValidator::isValidPosition(makePosition(-4999.9f, -99.9f, -4999.9f)));
}

TEST_CASE("[input-validator] Invalid positions outside world bounds", "[security]") {
    // Just outside X
    REQUIRE_FALSE(InputValidator::isValidPosition(makePosition(5001.0f, 0.0f, 0.0f)));
    REQUIRE_FALSE(InputValidator::isValidPosition(makePosition(-5001.0f, 0.0f, 0.0f)));

    // Just outside Y
    REQUIRE_FALSE(InputValidator::isValidPosition(makePosition(0.0f, 501.0f, 0.0f)));
    REQUIRE_FALSE(InputValidator::isValidPosition(makePosition(0.0f, -101.0f, 0.0f)));

    // Just outside Z
    REQUIRE_FALSE(InputValidator::isValidPosition(makePosition(0.0f, 0.0f, 5001.0f)));
    REQUIRE_FALSE(InputValidator::isValidPosition(makePosition(0.0f, 0.0f, -5001.0f)));

    // Way outside
    REQUIRE_FALSE(InputValidator::isValidPosition(makePosition(99999.0f, 0.0f, 0.0f)));
}

// ============================================================================
// Rotation Validation
// ============================================================================

TEST_CASE("[input-validator] Valid rotations", "[security]") {
    // Zero rotation
    REQUIRE(InputValidator::isValidRotation({0.0f, 0.0f}));

    // Normal ranges
    REQUIRE(InputValidator::isValidRotation({3.14159f, 0.0f}));       // PI yaw
    REQUIRE(InputValidator::isValidRotation({-3.14159f, 0.0f}));      // -PI yaw
    REQUIRE(InputValidator::isValidRotation({0.0f, 1.57079f}));       // PI/2 pitch
    REQUIRE(InputValidator::isValidRotation({0.0f, -1.57079f}));      // -PI/2 pitch

    // At boundaries (within ±2π and ±π/2)
    REQUIRE(InputValidator::isValidRotation({6.28318f, 1.57079f}));
    REQUIRE(InputValidator::isValidRotation({-6.28318f, -1.57079f}));
}

TEST_CASE("[input-validator] Invalid rotations", "[security]") {
    // Yaw beyond ±2π
    REQUIRE_FALSE(InputValidator::isValidRotation({6.3f, 0.0f}));
    REQUIRE_FALSE(InputValidator::isValidRotation({-6.3f, 0.0f}));

    // Pitch beyond ±π/2
    REQUIRE_FALSE(InputValidator::isValidRotation({0.0f, 1.6f}));
    REQUIRE_FALSE(InputValidator::isValidRotation({0.0f, -1.6f}));

    // Extreme values
    REQUIRE_FALSE(InputValidator::isValidRotation({100.0f, 100.0f}));
}

// ============================================================================
// Input Sequence Validation
// ============================================================================

TEST_CASE("[input-validator] Normal sequence progression", "[security]") {
    // Monotonically increasing is valid
    REQUIRE(InputValidator::isValidInputSequence(100, 99));
    REQUIRE(InputValidator::isValidInputSequence(200, 100));
    REQUIRE(InputValidator::isValidInputSequence(1, 0));
}

TEST_CASE("[input-validator] Out-of-order within tolerance", "[security]") {
    // Slightly behind (packet reordering) - not allowed (duplicate or old)
    REQUIRE_FALSE(InputValidator::isValidInputSequence(50, 100));

    // Exact duplicate
    REQUIRE_FALSE(InputValidator::isValidInputSequence(100, 100));
}

TEST_CASE("[input-validator] Sequence too far ahead", "[security]") {
    // Jump of 1000 or more is rejected
    REQUIRE_FALSE(InputValidator::isValidInputSequence(1100, 100));
    REQUIRE_FALSE(InputValidator::isValidInputSequence(2000, 0));
}

TEST_CASE("[input-validator] Sequence wraparound", "[security]") {
    // Near uint32_t max - wraparound should be allowed
    uint32_t nearMax = 0xFFFFFFFF - 50;
    uint32_t wrapped = 10;
    // lastSequence=nearMax, sequence=wrapped should pass wraparound check
    REQUIRE(InputValidator::isValidInputSequence(wrapped, nearMax));
}

// ============================================================================
// String Sanitization
// ============================================================================

TEST_CASE("[input-validator] Sanitize normal strings", "[security]") {
    REQUIRE(InputValidator::sanitizeString("Hello World", 256) == "Hello World");
    REQUIRE(InputValidator::sanitizeString("abc123!@#", 256) == "abc123!@#");
}

TEST_CASE("[input-validator] Sanitize strips control characters", "[security]") {
    // Null bytes, newlines, tabs (tab is 9 < 32, should be stripped)
    std::string withNulls = "Hello\x00World";
    REQUIRE(InputValidator::sanitizeString(withNulls, 256) == "Hello");

    std::string withControl = "A\tB\nC";
    REQUIRE(InputValidator::sanitizeString(withControl, 256) == "ABC");

    // DEL character (127) should be stripped
    std::string withDel = "Hello\x7FWorld";
    REQUIRE(InputValidator::sanitizeString(withDel, 256) == "HelloWorld");
}

TEST_CASE("[input-validator] Sanitize respects max length", "[security]") {
    REQUIRE(InputValidator::sanitizeString("Hello World", 5) == "Hello");
    REQUIRE(InputValidator::sanitizeString("Hello World", 0).empty());
    REQUIRE(InputValidator::sanitizeString("Hello World", 1) == "H");
}

TEST_CASE("[input-validator] Sanitize empty string", "[security]") {
    REQUIRE(InputValidator::sanitizeString("", 256).empty());
}

// ============================================================================
// Packet Size Validation
// ============================================================================

TEST_CASE("[input-validator] Valid packet sizes", "[security]") {
    REQUIRE(InputValidator::isValidPacketSize(1));
    REQUIRE(InputValidator::isValidPacketSize(100));
    REQUIRE(InputValidator::isValidPacketSize(1400));   // Max allowed
}

TEST_CASE("[input-validator] Invalid packet sizes", "[security]") {
    REQUIRE_FALSE(InputValidator::isValidPacketSize(0));      // Zero
    REQUIRE_FALSE(InputValidator::isValidPacketSize(1401));   // Over max
    REQUIRE_FALSE(InputValidator::isValidPacketSize(65535));  // Way over
}

// ============================================================================
// Protocol Version
// ============================================================================

TEST_CASE("[input-validator] Protocol version check", "[security]") {
    REQUIRE(InputValidator::isValidProtocolVersion(Constants::PROTOCOL_VERSION));
    REQUIRE_FALSE(InputValidator::isValidProtocolVersion(0));
    REQUIRE_FALSE(InputValidator::isValidProtocolVersion(2));
    REQUIRE_FALSE(InputValidator::isValidProtocolVersion(999));
}
