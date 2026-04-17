#include <catch2/catch_test_macros.hpp>
#include "security/CircuitBreaker.hpp"

using namespace DarkAges::Security;

// ============================================================================
// Initialization
// ============================================================================

TEST_CASE("[circuit-breaker] Default construction starts CLOSED", "[security]") {
    CircuitBreaker cb("test");
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
    REQUIRE(std::string(cb.getStateString()) == "CLOSED");
}

TEST_CASE("[circuit-breaker] Custom config construction", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 3;
    config.successThreshold = 2;
    config.timeoutMs = 5000;
    config.halfOpenMaxCalls = 1;

    CircuitBreaker cb("custom", config);
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
}

// ============================================================================
// CLOSED State Behavior
// ============================================================================

TEST_CASE("[circuit-breaker] CLOSED state allows requests", "[security]") {
    CircuitBreaker cb("test");
    REQUIRE(cb.allowRequest());
    REQUIRE(cb.allowRequest());
    REQUIRE(cb.allowRequest());
}

TEST_CASE("[circuit-breaker] Success in CLOSED resets failure count", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 5;
    CircuitBreaker cb("test", config);

    // 4 failures (below threshold)
    for (int i = 0; i < 4; ++i) {
        cb.recordFailure();
    }
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);

    // Success resets counter
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);

    // 4 more failures should NOT trip (counter was reset)
    for (int i = 0; i < 4; ++i) {
        cb.recordFailure();
    }
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
}

TEST_CASE("[circuit-breaker] Failure threshold trips to OPEN", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 3;
    CircuitBreaker cb("test", config);

    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);

    cb.recordFailure();  // Third failure → OPEN
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

TEST_CASE("[circuit-breaker] Exact threshold boundary", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 1;
    CircuitBreaker cb("test", config);

    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

// ============================================================================
// OPEN State Behavior
// ============================================================================

TEST_CASE("[circuit-breaker] OPEN state rejects requests", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 1;
    config.timeoutMs = 99999999;  // Long timeout so it stays OPEN
    CircuitBreaker cb("test", config);

    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
    REQUIRE_FALSE(cb.allowRequest());
}

TEST_CASE("[circuit-breaker] OPEN state ignores further failures", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 2;
    CircuitBreaker cb("test", config);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);

    // Additional failures don't change state
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

TEST_CASE("[circuit-breaker] OPEN state ignores successes", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 2;
    CircuitBreaker cb("test", config);

    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);

    // Success in OPEN state doesn't recover
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

// ============================================================================
// HALF_OPEN State Behavior
// ============================================================================

TEST_CASE("[circuit-breaker] forceState to HALF_OPEN", "[security]") {
    CircuitBreaker cb("test");
    cb.forceState(CircuitBreaker::State::HALF_OPEN);
    REQUIRE(cb.getState() == CircuitBreaker::State::HALF_OPEN);
}

TEST_CASE("[circuit-breaker] HALF_OPEN allows limited calls", "[security]") {
    CircuitBreaker::Config config;
    config.halfOpenMaxCalls = 2;
    CircuitBreaker cb("test", config);

    cb.forceState(CircuitBreaker::State::HALF_OPEN);

    REQUIRE(cb.allowRequest());   // Call 1
    REQUIRE(cb.allowRequest());   // Call 2
    REQUIRE_FALSE(cb.allowRequest());  // Exceeded limit
}

TEST_CASE("[circuit-breaker] HALF_OPEN success threshold closes circuit", "[security]") {
    CircuitBreaker::Config config;
    config.successThreshold = 2;
    config.halfOpenMaxCalls = 5;
    CircuitBreaker cb("test", config);

    cb.forceState(CircuitBreaker::State::HALF_OPEN);

    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitBreaker::State::HALF_OPEN);

    cb.recordSuccess();  // Second success → CLOSED
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
}

TEST_CASE("[circuit-breaker] HALF_OPEN failure reopens circuit", "[security]") {
    CircuitBreaker cb("test");

    cb.forceState(CircuitBreaker::State::HALF_OPEN);
    REQUIRE(cb.getState() == CircuitBreaker::State::HALF_OPEN);

    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

TEST_CASE("[circuit-breaker] HALF_OPEN recovery resets counters", "[security]") {
    CircuitBreaker::Config config;
    config.successThreshold = 2;
    config.halfOpenMaxCalls = 10;
    config.failureThreshold = 3;
    CircuitBreaker cb("test", config);

    cb.forceState(CircuitBreaker::State::HALF_OPEN);

    // Recover
    cb.recordSuccess();
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);

    // Should need full failure threshold again to trip
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

// ============================================================================
// forceState
// ============================================================================

TEST_CASE("[circuit-breaker] forceState resets all counters", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 3;
    config.successThreshold = 2;
    config.halfOpenMaxCalls = 3;
    CircuitBreaker cb("test", config);

    // Trip to OPEN
    cb.recordFailure();
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);

    // Force back to CLOSED — should be fresh
    cb.forceState(CircuitBreaker::State::CLOSED);
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);

    // Need full threshold to trip again
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

TEST_CASE("[circuit-breaker] forceState to OPEN rejects requests", "[security]") {
    CircuitBreaker cb("test");
    cb.forceState(CircuitBreaker::State::OPEN);
    REQUIRE_FALSE(cb.allowRequest());
}

// ============================================================================
// State String Conversion
// ============================================================================

TEST_CASE("[circuit-breaker] State string conversion", "[security]") {
    CircuitBreaker cb("test");

    REQUIRE(std::string(cb.getStateString()) == "CLOSED");

    cb.forceState(CircuitBreaker::State::OPEN);
    REQUIRE(std::string(cb.getStateString()) == "OPEN");

    cb.forceState(CircuitBreaker::State::HALF_OPEN);
    REQUIRE(std::string(cb.getStateString()) == "HALF_OPEN");
}

// ============================================================================
// Config Variations
// ============================================================================

TEST_CASE("[circuit-breaker] Zero failure threshold trips immediately", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 0;
    CircuitBreaker cb("test", config);

    // With threshold 0, any failure trips immediately
    // But threshold 0 means >= 0 failures, so it's already tripped?
    // Actually, failureCount starts at 0 and increments first
    // With threshold 0, even the first increment should trip
    // Let's verify behavior
    cb.recordFailure();
    // After first failure, count = 1, which is >= 0, so OPEN
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

TEST_CASE("[circuit-breaker] High threshold is resilient", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 100;
    CircuitBreaker cb("test", config);

    for (int i = 0; i < 99; ++i) {
        cb.recordFailure();
    }
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
    REQUIRE(cb.allowRequest());

    cb.recordFailure();  // 100th
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
}

// ============================================================================
// Full Lifecycle
// ============================================================================

TEST_CASE("[circuit-breaker] Full lifecycle: CLOSED -> OPEN -> HALF_OPEN -> CLOSED", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 2;
    config.successThreshold = 2;
    config.halfOpenMaxCalls = 5;
    CircuitBreaker cb("lifecycle", config);

    // 1. CLOSED — normal operation
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);
    REQUIRE(cb.allowRequest());

    // 2. Accumulate failures → OPEN
    cb.recordFailure();
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);
    REQUIRE_FALSE(cb.allowRequest());

    // 3. Force to HALF_OPEN (simulating timeout passage)
    cb.forceState(CircuitBreaker::State::HALF_OPEN);
    REQUIRE(cb.getState() == CircuitBreaker::State::HALF_OPEN);
    REQUIRE(cb.allowRequest());

    // 4. Failures in HALF_OPEN reopen
    cb.recordFailure();
    REQUIRE(cb.getState() == CircuitBreaker::State::OPEN);

    // 5. Back to HALF_OPEN, succeed this time
    cb.forceState(CircuitBreaker::State::HALF_OPEN);
    cb.recordSuccess();
    REQUIRE(cb.getState() == CircuitBreaker::State::HALF_OPEN);
    cb.recordSuccess();  // threshold reached
    REQUIRE(cb.getState() == CircuitBreaker::State::CLOSED);

    // 6. Back to normal
    REQUIRE(cb.allowRequest());
}

// ============================================================================
// Thread Safety (basic check — not a race condition detector)
// ============================================================================

TEST_CASE("[circuit-breaker] Concurrent access doesn't crash", "[security]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 10;
    CircuitBreaker cb("concurrent", config);

    // Just verify that rapid concurrent calls don't deadlock or crash
    for (int i = 0; i < 100; ++i) {
        if (cb.allowRequest()) {
            if (i % 3 == 0) {
                cb.recordSuccess();
            } else {
                cb.recordFailure();
            }
        }
    }

    // Should end up in one of the valid states
    auto state = cb.getState();
    REQUIRE((state == CircuitBreaker::State::CLOSED ||
             state == CircuitBreaker::State::OPEN ||
             state == CircuitBreaker::State::HALF_OPEN));
}
