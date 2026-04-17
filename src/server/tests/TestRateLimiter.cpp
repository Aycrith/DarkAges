#include <catch2/catch_test_macros.hpp>
#include "security/RateLimiter.hpp"
#include <thread>
#include <chrono>
#include <string>

using namespace DarkAges::Security;

// ============================================================================
// TokenBucketRateLimiter
// ============================================================================

TEST_CASE("[rate-limiter] Token bucket allows initial burst", "[security]") {
    TokenBucketRateLimiter<std::string> limiter({10, 60}); // 10 max, 60/sec

    // Should allow up to maxTokens requests immediately
    for (uint32_t i = 0; i < 10; ++i) {
        REQUIRE(limiter.allow("192.168.1.1"));
    }

    // 11th request should be denied (bucket empty)
    REQUIRE_FALSE(limiter.allow("192.168.1.1"));
}

TEST_CASE("[rate-limiter] Token bucket refills over time", "[security]") {
    TokenBucketRateLimiter<std::string> limiter({5, 100}); // 5 max, 100/sec = 1 per 10ms

    // Exhaust tokens
    for (uint32_t i = 0; i < 5; ++i) {
        limiter.allow("10.0.0.1");
    }
    REQUIRE_FALSE(limiter.allow("10.0.0.1"));

    // Wait for refill (100/sec = 1 token per 10ms, wait 20ms for 2 tokens)
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    // Should have refilled at least 1-2 tokens
    REQUIRE(limiter.allow("10.0.0.1"));
}

TEST_CASE("[rate-limiter] Token bucket tracks keys independently", "[security]") {
    TokenBucketRateLimiter<std::string> limiter({3, 60});

    // Exhaust tokens for IP1
    for (uint32_t i = 0; i < 3; ++i) {
        REQUIRE(limiter.allow("192.168.1.1"));
    }
    REQUIRE_FALSE(limiter.allow("192.168.1.1"));

    // IP2 should still have full bucket
    REQUIRE(limiter.allow("192.168.1.2"));
    REQUIRE(limiter.allow("192.168.1.2"));
    REQUIRE(limiter.allow("192.168.1.2"));
    REQUIRE_FALSE(limiter.allow("192.168.1.2"));
}

TEST_CASE("[rate-limiter] Token bucket wouldAllow peeks without consuming", "[security]") {
    TokenBucketRateLimiter<std::string> limiter({2, 60});

    // New key: wouldAllow returns true (maxTokens > 0), doesn't create bucket
    REQUIRE(limiter.wouldAllow("test"));

    // Consume 2 tokens via allow (creates bucket)
    limiter.allow("test");
    limiter.allow("test");

    // Now bucket is actually empty - wouldAllow returns false
    REQUIRE_FALSE(limiter.wouldAllow("test"));
    REQUIRE_FALSE(limiter.allow("test"));
}

TEST_CASE("[rate-limiter] Token bucket getRemainingTokens", "[security]") {
    TokenBucketRateLimiter<std::string> limiter({10, 60});

    // Unknown key returns max
    REQUIRE(limiter.getRemainingTokens("new-key") == 10);

    // Consume 3
    limiter.allow("key");
    limiter.allow("key");
    limiter.allow("key");

    REQUIRE(limiter.getRemainingTokens("key") == 7);
}

TEST_CASE("[rate-limiter] Token bucket reset restores tokens", "[security]") {
    TokenBucketRateLimiter<std::string> limiter({5, 60});

    // Exhaust
    for (uint32_t i = 0; i < 5; ++i) limiter.allow("key");
    REQUIRE_FALSE(limiter.allow("key"));

    // Reset
    limiter.reset("key");

    // Should be full again
    REQUIRE(limiter.getRemainingTokens("key") == 5);
    REQUIRE(limiter.allow("key"));
}

// ============================================================================
// SlidingWindowRateLimiter
// ============================================================================

TEST_CASE("[rate-limiter] Sliding window allows up to max requests", "[security]") {
    SlidingWindowRateLimiter<std::string> limiter({5, 60000}); // 5 per minute

    for (uint32_t i = 0; i < 5; ++i) {
        REQUIRE(limiter.allow("client1"));
    }
    REQUIRE_FALSE(limiter.allow("client1"));
}

TEST_CASE("[rate-limiter] Sliding window tracks independently", "[security]") {
    SlidingWindowRateLimiter<uint32_t> limiter({3, 60000});

    REQUIRE(limiter.allow(1));
    REQUIRE(limiter.allow(1));
    REQUIRE(limiter.allow(1));
    REQUIRE_FALSE(limiter.allow(1));

    // Different key
    REQUIRE(limiter.allow(2));
    REQUIRE(limiter.allow(2));
    REQUIRE(limiter.allow(2));
    REQUIRE_FALSE(limiter.allow(2));
}

TEST_CASE("[rate-limiter] Sliding window getRemaining", "[security]") {
    SlidingWindowRateLimiter<std::string> limiter({10, 60000});

    REQUIRE(limiter.getRemaining("key") == 10);

    limiter.allow("key");
    limiter.allow("key");
    limiter.allow("key");

    REQUIRE(limiter.getRemaining("key") == 7);
}

TEST_CASE("[rate-limiter] Sliding window reset", "[security]") {
    SlidingWindowRateLimiter<std::string> limiter({3, 60000});

    for (uint32_t i = 0; i < 3; ++i) limiter.allow("key");
    REQUIRE_FALSE(limiter.allow("key"));

    limiter.reset("key");
    REQUIRE(limiter.getRemaining("key") == 3);
    REQUIRE(limiter.allow("key"));
}

TEST_CASE("[rate-limiter] Sliding window expires old entries", "[security]") {
    // 100ms window for fast test
    SlidingWindowRateLimiter<std::string> limiter({2, 100});

    REQUIRE(limiter.allow("key"));
    REQUIRE(limiter.allow("key"));
    REQUIRE_FALSE(limiter.allow("key"));

    // Wait for window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    // Old entries should have been cleaned up
    REQUIRE(limiter.allow("key"));
}

// ============================================================================
// AdaptiveRateLimiter
// ============================================================================

TEST_CASE("[rate-limiter] Adaptive limiter normal mode", "[security]") {
    AdaptiveRateLimiter::Config config;
    config.normalRate = 100;
    config.stressedRate = 50;
    config.criticalRate = 20;
    config.stressThreshold = 0.7f;
    config.criticalThreshold = 0.9f;

    AdaptiveRateLimiter limiter(config);

    // Low load - should use normal rate
    REQUIRE(limiter.getCurrentRateLimit() == 100);
    REQUIRE_FALSE(limiter.isStressed());
    REQUIRE_FALSE(limiter.isCritical());
}

TEST_CASE("[rate-limiter] Adaptive limiter stressed mode", "[security]") {
    AdaptiveRateLimiter::Config config;
    config.normalRate = 100;
    config.stressedRate = 50;
    config.criticalRate = 20;
    config.stressThreshold = 0.7f;
    config.criticalThreshold = 0.9f;

    AdaptiveRateLimiter limiter(config);
    limiter.updateServerLoad(0.8f); // Above stress threshold

    REQUIRE(limiter.getCurrentRateLimit() == 50);
    REQUIRE(limiter.isStressed());
    REQUIRE_FALSE(limiter.isCritical());
}

TEST_CASE("[rate-limiter] Adaptive limiter critical mode", "[security]") {
    AdaptiveRateLimiter::Config config;
    config.normalRate = 100;
    config.stressedRate = 50;
    config.criticalRate = 20;
    config.stressThreshold = 0.7f;
    config.criticalThreshold = 0.9f;

    AdaptiveRateLimiter limiter(config);
    limiter.updateServerLoad(0.95f); // Above critical threshold

    REQUIRE(limiter.getCurrentRateLimit() == 20);
    REQUIRE(limiter.isStressed());
    REQUIRE(limiter.isCritical());
}

TEST_CASE("[rate-limiter] Adaptive limiter transitions between modes", "[security]") {
    AdaptiveRateLimiter limiter(AdaptiveRateLimiter::Config{});

    // Start normal
    REQUIRE(limiter.getCurrentRateLimit() == 100);

    // Go stressed
    limiter.updateServerLoad(0.75f);
    REQUIRE(limiter.getCurrentRateLimit() == 50);

    // Go critical
    limiter.updateServerLoad(0.95f);
    REQUIRE(limiter.getCurrentRateLimit() == 20);

    // Recover to normal
    limiter.updateServerLoad(0.3f);
    REQUIRE(limiter.getCurrentRateLimit() == 100);
}

// ============================================================================
// NetworkRateLimiter
// ============================================================================

TEST_CASE("[rate-limiter] Network rate limiter connection limiting", "[security]") {
    NetworkRateLimiter limiter;

    // Should allow connections up to connection burst limit (10 tokens)
    for (int i = 0; i < 10; ++i) {
        REQUIRE(limiter.allowConnection("192.168.1.1"));
    }
    // 11th should fail
    REQUIRE_FALSE(limiter.allowConnection("192.168.1.1"));

    // Different IP should work
    REQUIRE(limiter.allowConnection("192.168.1.2"));
}

TEST_CASE("[rate-limiter] Network rate limiter packet limiting", "[security]") {
    NetworkRateLimiter limiter;

    // Packet burst is 120 tokens
    for (int i = 0; i < 120; ++i) {
        REQUIRE(limiter.allowPacket(1));
    }
    REQUIRE_FALSE(limiter.allowPacket(1));

    // Different connection
    REQUIRE(limiter.allowPacket(2));
}

TEST_CASE("[rate-limiter] Network rate limiter message limiting", "[security]") {
    NetworkRateLimiter limiter;

    // Message burst is 30 tokens
    for (int i = 0; i < 30; ++i) {
        REQUIRE(limiter.allowMessage(1));
    }
    REQUIRE_FALSE(limiter.allowMessage(1));
}

TEST_CASE("[rate-limiter] Network rate limiter connection closed resets", "[security]") {
    NetworkRateLimiter limiter;

    // Exhaust packet tokens for connection 1
    for (int i = 0; i < 120; ++i) limiter.allowPacket(1);
    REQUIRE_FALSE(limiter.allowPacket(1));

    // Close connection
    limiter.onConnectionClosed(1);

    // Should be reset
    REQUIRE(limiter.getRemainingPacketTokens(1) == 120);
    REQUIRE(limiter.allowPacket(1));
}

// ============================================================================
// ConnectionThrottler
// ============================================================================

TEST_CASE("[rate-limiter] Connection throttler allows first connection", "[security]") {
    ConnectionThrottler throttler;
    REQUIRE(throttler.allowConnection("10.0.0.1"));
}

TEST_CASE("[rate-limiter] Connection throttler blocks after max attempts", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 5;
    config.windowSeconds = 60;
    config.blockDurationSeconds = 300;

    ConnectionThrottler throttler(config);

    // Record 5 failed attempts
    for (int i = 0; i < 5; ++i) {
        throttler.recordAttempt("10.0.0.1", false);
    }

    // Should now block
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));

    // Different IP should still work
    REQUIRE(throttler.allowConnection("10.0.0.2"));
}

TEST_CASE("[rate-limiter] Connection throttler tracks success/failure", "[security]") {
    ConnectionThrottler throttler;

    // Successful connections don't count toward blocking the same way
    throttler.recordAttempt("10.0.0.1", true);
    throttler.recordAttempt("10.0.0.1", true);
    throttler.recordAttempt("10.0.0.1", false);

    // Still under limit (default 10)
    REQUIRE(throttler.allowConnection("10.0.0.1"));
}

TEST_CASE("[rate-limiter] Connection throttler cleanup removes old entries", "[security]") {
    ConnectionThrottler::Config config;
    config.maxAttempts = 5;
    config.windowSeconds = 1;   // 1 second window
    config.blockDurationSeconds = 1;

    ConnectionThrottler throttler(config);

    // Record attempts
    for (int i = 0; i < 5; ++i) {
        throttler.recordAttempt("10.0.0.1", false);
    }
    REQUIRE_FALSE(throttler.allowConnection("10.0.0.1"));

    // Wait for block to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // Cleanup with future timestamp
    throttler.cleanup(2500); // 2.5 seconds later

    // Should be allowed again (old entries cleaned up)
    REQUIRE(throttler.allowConnection("10.0.0.1"));
}
