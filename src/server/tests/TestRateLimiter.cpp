// [SECURITY_AGENT] Rate limiter unit tests
// Tests for per-IP connection limiting, per-player input rate limiting,
// DDoS flood detection, and cleanup functionality

#include <catch2/catch_test_macros.hpp>
#include "security/RateLimiter.hpp"

#include <thread>
#include <vector>
#include <string>

using namespace DarkAges::Security;

// ============================================================================
// Connection Limiting Tests
// ============================================================================

TEST_CASE("RateLimiter: Connection limit allows within limit", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxConnectionsPerIP = 3;
    RateLimiter limiter(config);

    std::string ip = "192.168.1.1";

    // Should allow first connections up to the limit
    REQUIRE(limiter.checkConnectionLimit(ip));
    limiter.recordConnection(ip);

    REQUIRE(limiter.checkConnectionLimit(ip));
    limiter.recordConnection(ip);

    REQUIRE(limiter.checkConnectionLimit(ip));
    limiter.recordConnection(ip);

    // Now at limit (3), should deny
    REQUIRE_FALSE(limiter.checkConnectionLimit(ip));

    REQUIRE(limiter.getConnectionCount(ip) == 3);
}

TEST_CASE("RateLimiter: Connection limit denies over limit", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxConnectionsPerIP = 2;
    RateLimiter limiter(config);

    std::string ip = "10.0.0.1";

    limiter.recordConnection(ip);
    limiter.recordConnection(ip);

    REQUIRE_FALSE(limiter.checkConnectionLimit(ip));
}

TEST_CASE("RateLimiter: Disconnection frees connection slot", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxConnectionsPerIP = 2;
    RateLimiter limiter(config);

    std::string ip = "10.0.0.2";

    limiter.recordConnection(ip);
    limiter.recordConnection(ip);
    REQUIRE_FALSE(limiter.checkConnectionLimit(ip));

    // Disconnect one
    limiter.recordDisconnection(ip);
    REQUIRE(limiter.checkConnectionLimit(ip));

    limiter.recordConnection(ip);
    REQUIRE_FALSE(limiter.checkConnectionLimit(ip));
}

TEST_CASE("RateLimiter: Multiple IPs tracked independently", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxConnectionsPerIP = 1;
    RateLimiter limiter(config);

    std::string ip1 = "192.168.1.10";
    std::string ip2 = "192.168.1.11";

    limiter.recordConnection(ip1);
    REQUIRE_FALSE(limiter.checkConnectionLimit(ip1));
    REQUIRE(limiter.checkConnectionLimit(ip2));

    limiter.recordConnection(ip2);
    REQUIRE_FALSE(limiter.checkConnectionLimit(ip2));
}

TEST_CASE("RateLimiter: Disconnection removes entry when count reaches zero", "[RateLimiter][Security]") {
    RateLimitConfig config;
    RateLimiter limiter(config);

    std::string ip = "10.0.0.5";

    limiter.recordConnection(ip);
    REQUIRE(limiter.getTrackedIPCount() == 1);

    limiter.recordDisconnection(ip);
    // Entry should be removed when connections drop to 0
    REQUIRE(limiter.getConnectionCount(ip) == 0);
}

// ============================================================================
// Input Rate Limiting Tests
// ============================================================================

TEST_CASE("RateLimiter: Input rate allows within limit", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxInputPerSecond = 5;
    config.inputWindowDurationMs = 1000;
    RateLimiter limiter(config);

    std::uint64_t playerId = 100;

    for (std::uint32_t i = 0; i < 5; ++i) {
        REQUIRE(limiter.checkInputRate(playerId));
        limiter.recordInput(playerId);
    }

    // At limit, should deny
    REQUIRE_FALSE(limiter.checkInputRate(playerId));
}

TEST_CASE("RateLimiter: Input rate denies over limit", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxInputPerSecond = 3;
    config.inputWindowDurationMs = 1000;
    RateLimiter limiter(config);

    std::uint64_t playerId = 200;

    limiter.recordInput(playerId);
    limiter.recordInput(playerId);
    limiter.recordInput(playerId);

    REQUIRE_FALSE(limiter.checkInputRate(playerId));
}

TEST_CASE("RateLimiter: Input rate allows after window expires", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxInputPerSecond = 2;
    config.inputWindowDurationMs = 50;  // 50ms window for fast test
    RateLimiter limiter(config);

    std::uint64_t playerId = 300;

    // Exhaust the window
    limiter.recordInput(playerId);
    limiter.recordInput(playerId);
    REQUIRE_FALSE(limiter.checkInputRate(playerId));

    // Wait for window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // Should allow again
    REQUIRE(limiter.checkInputRate(playerId));
}

TEST_CASE("RateLimiter: Input rate tracks multiple players independently", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxInputPerSecond = 2;
    RateLimiter limiter(config);

    std::uint64_t player1 = 1;
    std::uint64_t player2 = 2;

    limiter.recordInput(player1);
    limiter.recordInput(player1);
    REQUIRE_FALSE(limiter.checkInputRate(player1));

    // Player 2 should still be allowed
    REQUIRE(limiter.checkInputRate(player2));
    limiter.recordInput(player2);
    REQUIRE(limiter.checkInputRate(player2));
}

TEST_CASE("RateLimiter: Input rate 60Hz default", "[RateLimiter][Security]") {
    RateLimitConfig config;  // Default: 60 inputs/sec
    RateLimiter limiter(config);

    std::uint64_t playerId = 400;

    for (std::uint32_t i = 0; i < 60; ++i) {
        REQUIRE(limiter.checkInputRate(playerId));
        limiter.recordInput(playerId);
    }

    REQUIRE_FALSE(limiter.checkInputRate(playerId));
}

// ============================================================================
// DDoS Threshold Tests
// ============================================================================

TEST_CASE("RateLimiter: DDoS threshold allows normal connections", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxConnectionsPerSecondPerIP = 5;
    RateLimiter limiter(config);

    std::string ip = "192.168.1.100";

    for (std::uint32_t i = 0; i < 5; ++i) {
        REQUIRE_FALSE(limiter.checkDDoSThreshold(ip));
        limiter.recordConnectionAttempt(ip);
    }

    // Now at threshold, should detect flood
    REQUIRE(limiter.checkDDoSThreshold(ip));
}

TEST_CASE("RateLimiter: DDoS threshold detects flood", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxConnectionsPerSecondPerIP = 3;
    RateLimiter limiter(config);

    std::string ip = "10.0.0.50";

    limiter.recordConnectionAttempt(ip);
    limiter.recordConnectionAttempt(ip);
    limiter.recordConnectionAttempt(ip);

    REQUIRE(limiter.checkDDoSThreshold(ip));
}

TEST_CASE("RateLimiter: DDoS threshold resets after window", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.maxConnectionsPerSecondPerIP = 2;
    config.connectionWindowDurationMs = 50;  // 50ms window
    RateLimiter limiter(config);

    std::string ip = "172.16.0.1";

    limiter.recordConnectionAttempt(ip);
    limiter.recordConnectionAttempt(ip);
    REQUIRE(limiter.checkDDoSThreshold(ip));

    // Wait for window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    REQUIRE_FALSE(limiter.checkDDoSThreshold(ip));
}

TEST_CASE("RateLimiter: DDoS no attempts returns false", "[RateLimiter][Security]") {
    RateLimiter limiter;

    REQUIRE_FALSE(limiter.checkDDoSThreshold("1.2.3.4"));
}

// ============================================================================
// Maintenance Tests
// ============================================================================

TEST_CASE("RateLimiter: Reset clears all state", "[RateLimiter][Security]") {
    RateLimitConfig config;
    RateLimiter limiter(config);

    std::string ip = "10.0.0.99";
    std::uint64_t playerId = 999;

    limiter.recordConnection(ip);
    limiter.recordConnectionAttempt(ip);
    limiter.recordInput(playerId);

    REQUIRE(limiter.getConnectionCount(ip) > 0);
    REQUIRE(limiter.getInputCount(playerId) > 0);

    limiter.reset();

    REQUIRE(limiter.getConnectionCount(ip) == 0);
    REQUIRE(limiter.getInputCount(playerId) == 0);
    REQUIRE(limiter.getTrackedIPCount() == 0);
    REQUIRE(limiter.getTrackedPlayerCount() == 0);
}

TEST_CASE("RateLimiter: Cleanup stale removes old entries", "[RateLimiter][Security]") {
    RateLimitConfig config;
    config.inputWindowDurationMs = 1000;
    RateLimiter limiter(config);

    std::uint64_t playerId = 777;

    limiter.recordInput(playerId);
    REQUIRE(limiter.getTrackedPlayerCount() == 1);

    // Sleep to make entry stale
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Cleanup with very short maxAge removes the entry
    limiter.cleanupStale(10);
    REQUIRE(limiter.getTrackedPlayerCount() == 0);
}

TEST_CASE("RateLimiter: Cleanup stale preserves active entries", "[RateLimiter][Security]") {
    RateLimitConfig config;
    RateLimiter limiter(config);

    std::string ip = "10.0.0.77";

    limiter.recordConnection(ip);

    // Cleanup with long maxAge preserves the entry
    limiter.cleanupStale(600000);  // 10 minutes

    REQUIRE(limiter.getConnectionCount(ip) == 1);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_CASE("RateLimiter: Thread-safe concurrent connections", "[RateLimiter][Security][Concurrency]") {
    RateLimitConfig config;
    config.maxConnectionsPerIP = 100;
    RateLimiter limiter(config);

    std::string ip = "192.168.1.200";
    constexpr int numThreads = 8;
    constexpr int opsPerThread = 50;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < opsPerThread; ++i) {
                limiter.recordConnection(ip);
                limiter.checkConnectionLimit(ip);
                limiter.recordDisconnection(ip);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // After all threads complete, connection count should be 0
    // (each thread records + disconnections equally)
    REQUIRE(limiter.getConnectionCount(ip) == 0);
}

TEST_CASE("RateLimiter: Thread-safe concurrent input rate limiting", "[RateLimiter][Security][Concurrency]") {
    RateLimitConfig config;
    config.maxInputPerSecond = 1000;  // High limit for concurrency test
    RateLimiter limiter(config);

    std::uint64_t playerId = 555;
    constexpr int numThreads = 4;
    constexpr int opsPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < opsPerThread; ++i) {
                limiter.recordInput(playerId);
                limiter.checkInputRate(playerId);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All inputs should be recorded
    REQUIRE(limiter.getInputCount(playerId) == numThreads * opsPerThread);
}
