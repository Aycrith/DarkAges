#include <catch2/catch_test_macros.hpp>
#include "security/RateLimiter.hpp"
#include <thread>
#include <chrono>
#include <string>

using namespace DarkAges::Security;

// ============================================================================
// ConnectionThrottler - Default Construction
// ============================================================================

TEST_CASE("[conn-throttle] New IP is always allowed", "[security]") {
    ConnectionThrottler throttler;
    REQUIRE(throttler.allowConnection("10.0.0.1"));
}

TEST_CASE("[conn-throttle] Default config allows 10 attempts", "[security]") {
    ConnectionThrottler throttler;

    // Should allow up to maxAttempts (default 10)
    for (int i = 0; i < 10; ++i) {
        throttler.recordAttempt("10.0.0.1", false);
        // Still allowed until we hit the limit
    }

    // 10 attempts recorded, should now block
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));
}

TEST_CASE("[conn-throttle] Custom config controls max attempts", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 3;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    throttler.recordAttempt("10.0.0.1", false);
    throttler.recordAttempt("10.0.0.1", false);
    REQUIRE(throttler.allowConnection("10.0.0.1")); // Still under limit

    throttler.recordAttempt("10.0.0.1", false);
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1")); // Hit limit
}

// ============================================================================
// ConnectionThrottler - Multiple IPs
// ============================================================================

TEST_CASE("[conn-throttle] Different IPs are tracked independently", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 5;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    // Exhaust attempts for IP1
    for (int i = 0; i < 5; ++i) {
        throttler.recordAttempt("10.0.0.1", false);
    }
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));

    // IP2 should still be allowed
    REQUIRE(throttler.allowConnection("10.0.0.2"));
}

TEST_CASE("[conn-throttle] Many IPs work independently", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 2;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    // Block 5 different IPs
    for (int ip = 1; ip <= 5; ++ip) {
        std::string addr = "192.168.1." + std::to_string(ip);
        for (int i = 0; i < 2; ++i) {
            throttler.recordAttempt(addr, false);
        }
        REQUIRE_FALSE(throttler.allowConnection(addr));
    }

    // 6th IP should still be allowed
    REQUIRE(throttler.allowConnection("192.168.1.6"));
}

// ============================================================================
// ConnectionThrottler - Success vs Failure Tracking
// ============================================================================

TEST_CASE("[conn-throttle] Success attempts count toward limit", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 5;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    // Record 5 successful attempts
    for (int i = 0; i < 5; ++i) {
        throttler.recordAttempt("10.0.0.1", true);
    }

    // All count toward the max attempts limit
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));
}

TEST_CASE("[conn-throttle] Mixed success and failure both count", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 4;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    throttler.recordAttempt("10.0.0.1", true);
    throttler.recordAttempt("10.0.0.1", false);
    throttler.recordAttempt("10.0.0.1", true);
    REQUIRE(throttler.allowConnection("10.0.0.1")); // 3 < 4

    throttler.recordAttempt("10.0.0.1", false);
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1")); // 4 >= 4
}

// ============================================================================
// ConnectionThrottler - Cleanup
// ============================================================================

TEST_CASE("[conn-throttle] Cleanup removes old entries within window", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 3;
    config.windowSeconds = 1;    // 1 second window
    config.blockDurationSeconds = 1;

    ConnectionThrottler throttler(config);

    // Record 3 attempts at t=0
    for (int i = 0; i < 3; ++i) {
        throttler.recordAttempt("10.0.0.1", false);
    }
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));

    // Wait for window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // Cleanup with a future timestamp (2.5 seconds)
    throttler.cleanup(2500);

    // Should be allowed again after cleanup
    REQUIRE(throttler.allowConnection("10.0.0.1"));
}

TEST_CASE("[conn-throttle] Cleanup with no entries does not crash", "[security]") {
    ConnectionThrottler throttler;
    throttler.cleanup(10000); // Should not crash
}

TEST_CASE("[conn-throttle] Cleanup preserves active blocked entries", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 2;
    config.windowSeconds = 60;       // Long window
    config.blockDurationSeconds = 60; // Long block

    ConnectionThrottler throttler(config);

    // Block the IP
    throttler.recordAttempt("10.0.0.1", false);
    throttler.recordAttempt("10.0.0.1", false);
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));

    // Cleanup with small timestamp (still blocked)
    throttler.cleanup(1000); // 1 second, block lasts 60 seconds

    // Should still be blocked
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));
}

// ============================================================================
// ConnectionThrottler - Edge Cases
// ============================================================================

TEST_CASE("[conn-throttle] Single max attempt blocks immediately", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 1;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    REQUIRE(throttler.allowConnection("10.0.0.1"));
    throttler.recordAttempt("10.0.0.1", false);
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));
}

TEST_CASE("[conn-throttle] Empty IP string is handled", "[security]") {
    ConnectionThrottler throttler;
    REQUIRE(throttler.allowConnection(""));
    throttler.recordAttempt("", false);
    REQUIRE(throttler.allowConnection("")); // Still under default limit
}

TEST_CASE("[conn-throttle] IPv6 address format works", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 3;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    throttler.recordAttempt("::1", false);
    throttler.recordAttempt("::1", false);
    REQUIRE(throttler.allowConnection("::1"));

    throttler.recordAttempt("::1", false);
    REQUIRE_FALSE(throttler.allowConnection("::1"));
}

// ============================================================================
// ConnectionThrottler - Block Expiry via Cleanup
// ============================================================================

TEST_CASE("[conn-throttle] Blocked IP unblocks after cleanup with expired block", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 2;
    config.windowSeconds = 1;
    config.blockDurationSeconds = 1;

    ConnectionThrottler throttler(config);

    // Block IP
    throttler.recordAttempt("10.0.0.1", false);
    throttler.recordAttempt("10.0.0.1", false);
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));

    // Wait for block and window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    // Cleanup well into the future - both timestamps are old and block has expired
    throttler.cleanup(3000);

    REQUIRE(throttler.allowConnection("10.0.0.1"));
}
