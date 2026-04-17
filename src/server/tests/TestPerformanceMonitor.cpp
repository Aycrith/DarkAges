#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "profiling/PerformanceMonitor.hpp"
#include "Constants.hpp"
#include <thread>
#include <chrono>
#include <cstdint>

using namespace DarkAges::Profiling;
using Catch::Approx;

// ============================================================================
// PerformanceMonitor - Initial State
// ============================================================================

TEST_CASE("[perf-monitor] Default state is acceptable with no data", "[profiling]") {
    PerformanceMonitor monitor;

    REQUIRE(monitor.isPerformanceAcceptable());
    REQUIRE(monitor.getCurrentFPS() == 0.0f);
    REQUIRE(monitor.getViolationCount() == 0);
    REQUIRE(monitor.getAverageTickTimeUs() == 0.0);
    REQUIRE(monitor.getMeasuredTickRate() == 0);
    REQUIRE(monitor.getP95TickTimeUs() == 0);
    REQUIRE(monitor.getP99TickTimeUs() == 0);
}

// ============================================================================
// PerformanceMonitor - Record Tick Times
// ============================================================================

TEST_CASE("[perf-monitor] Record single tick updates average", "[profiling]") {
    PerformanceMonitor monitor;

    monitor.recordTickTime(5000); // 5ms

    REQUIRE(monitor.getAverageTickTimeUs() == Approx(5000.0));
    REQUIRE_FALSE(monitor.isPerformanceAcceptable() == false); // 0% violations is fine
}

TEST_CASE("[perf-monitor] Record multiple ticks calculates correct average", "[profiling]") {
    PerformanceMonitor monitor;

    monitor.recordTickTime(1000);
    monitor.recordTickTime(2000);
    monitor.recordTickTime(3000);
    monitor.recordTickTime(4000);

    REQUIRE(monitor.getAverageTickTimeUs() == Approx(2500.0));
}

TEST_CASE("[perf-monitor] Large number of ticks maintains correct total", "[profiling]") {
    PerformanceMonitor monitor;

    // Record 100 ticks at 1000us each
    for (int i = 0; i < 100; ++i) {
        monitor.recordTickTime(1000);
    }

    REQUIRE(monitor.getAverageTickTimeUs() == Approx(1000.0));
}

// ============================================================================
// PerformanceMonitor - Violation Detection
// ============================================================================

TEST_CASE("[perf-monitor] Ticks within budget produce zero violations", "[profiling]") {
    PerformanceMonitor monitor;

    // TICK_BUDGET_US = 16666 (16.666ms)
    for (int i = 0; i < 100; ++i) {
        monitor.recordTickTime(10000); // 10ms - under budget
    }

    REQUIRE(monitor.getViolationCount() == 0);
    REQUIRE(monitor.isPerformanceAcceptable());
}

TEST_CASE("[perf-monitor] Ticks exceeding budget produce violations", "[profiling]") {
    PerformanceMonitor monitor;

    // Exactly at budget should NOT be a violation (> not >=)
    monitor.recordTickTime(DarkAges::Constants::TICK_BUDGET_US);
    REQUIRE(monitor.getViolationCount() == 0);

    // One over budget IS a violation
    monitor.recordTickTime(DarkAges::Constants::TICK_BUDGET_US + 1);
    REQUIRE(monitor.getViolationCount() == 1);
}

TEST_CASE("[perf-monitor] Performance becomes unacceptable with >1% violations", "[profiling]") {
    PerformanceMonitor monitor;

    // Record 99 good ticks (under budget)
    for (int i = 0; i < 99; ++i) {
        monitor.recordTickTime(1000);
    }
    // 1 bad tick = 1% violations (exactly 1%)
    monitor.recordTickTime(50000); // Way over budget

    // 1% should still be acceptable (< 1%)
    // Actually with 100 ticks and 1 violation, that's exactly 1%, which is NOT < 1%
    REQUIRE_FALSE(monitor.isPerformanceAcceptable());
}

TEST_CASE("[perf-monitor] Performance acceptable at just under 1% violations", "[profiling]") {
    PerformanceMonitor monitor;

    // 199 good + 1 bad = 0.5% violations
    for (int i = 0; i < 199; ++i) {
        monitor.recordTickTime(1000);
    }
    monitor.recordTickTime(50000);

    REQUIRE(monitor.isPerformanceAcceptable());
}

// ============================================================================
// PerformanceMonitor - FPS Calculation
// ============================================================================

TEST_CASE("[perf-monitor] FPS is zero with no ticks", "[profiling]") {
    PerformanceMonitor monitor;
    REQUIRE(monitor.getCurrentFPS() == 0.0f);
}

TEST_CASE("[perf-monitor] FPS calculated from average tick time", "[profiling]") {
    PerformanceMonitor monitor;

    // Record ticks at ~16666us (60Hz target)
    for (int i = 0; i < 10; ++i) {
        monitor.recordTickTime(16666);
    }

    float fps = monitor.getCurrentFPS();
    // 1000000 / 16666 ~= 60.0
    REQUIRE(fps == Approx(60.0f).margin(1.0f));
}

TEST_CASE("[perf-monitor] High tick time gives low FPS", "[profiling]") {
    PerformanceMonitor monitor;

    // 100ms per tick = 10 FPS
    monitor.recordTickTime(100000);

    float fps = monitor.getCurrentFPS();
    REQUIRE(fps == Approx(10.0f).margin(0.1f));
}

TEST_CASE("[perf-monitor] Very fast ticks give high FPS", "[profiling]") {
    PerformanceMonitor monitor;

    // 100us per tick = 10000 FPS
    monitor.recordTickTime(100);

    float fps = monitor.getCurrentFPS();
    REQUIRE(fps == Approx(10000.0f).margin(10.0f));
}

// ============================================================================
// PerformanceMonitor - Percentiles
// ============================================================================

TEST_CASE("[perf-monitor] Percentiles with no data return zero", "[profiling]") {
    PerformanceMonitor monitor;
    REQUIRE(monitor.getP95TickTimeUs() == 0);
    REQUIRE(monitor.getP99TickTimeUs() == 0);
}

TEST_CASE("[perf-monitor] Percentiles with uniform data", "[profiling]") {
    PerformanceMonitor monitor;

    for (int i = 0; i < 50; ++i) {
        monitor.recordTickTime(5000);
    }

    REQUIRE(monitor.getP95TickTimeUs() == 5000);
    REQUIRE(monitor.getP99TickTimeUs() == 5000);
}

TEST_CASE("[perf-monitor] P95 is below P99 with varied data", "[profiling]") {
    PerformanceMonitor monitor;

    // 90 ticks at 1ms, 5 at 5ms, 4 at 10ms, 1 at 20ms
    for (int i = 0; i < 90; ++i) monitor.recordTickTime(1000);
    for (int i = 0; i < 5; ++i)  monitor.recordTickTime(5000);
    for (int i = 0; i < 4; ++i)  monitor.recordTickTime(10000);
    monitor.recordTickTime(20000);

    uint64_t p95 = monitor.getP95TickTimeUs();
    uint64_t p99 = monitor.getP99TickTimeUs();

    REQUIRE(p95 >= 1000);
    REQUIRE(p99 >= p95);
}

// ============================================================================
// PerformanceMonitor - Reset
// ============================================================================

TEST_CASE("[perf-monitor] Reset clears all statistics", "[profiling]") {
    PerformanceMonitor monitor;

    // Record various ticks including violations
    for (int i = 0; i < 50; ++i) {
        monitor.recordTickTime(1000);
    }
    for (int i = 0; i < 50; ++i) {
        monitor.recordTickTime(50000); // Violations
    }

    REQUIRE(monitor.getViolationCount() > 0);
    REQUIRE(monitor.getAverageTickTimeUs() > 0);

    monitor.resetStats();

    REQUIRE(monitor.getViolationCount() == 0);
    REQUIRE(monitor.getAverageTickTimeUs() == 0.0);
    REQUIRE(monitor.isPerformanceAcceptable());
    REQUIRE(monitor.getCurrentFPS() == 0.0f);
}

TEST_CASE("[perf-monitor] Can record new data after reset", "[profiling]") {
    PerformanceMonitor monitor;

    monitor.recordTickTime(10000);
    monitor.resetStats();
    monitor.recordTickTime(5000);

    REQUIRE(monitor.getAverageTickTimeUs() == Approx(5000.0));
}

// ============================================================================
// PerformanceMonitor - Start/Stop Lifecycle
// ============================================================================

TEST_CASE("[perf-monitor] Start and stop work correctly", "[profiling]") {
    PerformanceMonitor monitor;

    // Start monitoring
    monitor.start(5000); // 5 second report interval

    // Record some ticks while running
    for (int i = 0; i < 10; ++i) {
        monitor.recordTickTime(5000);
    }

    // Stop monitoring (should join thread cleanly)
    monitor.stop();

    // Data should still be accessible after stop
    REQUIRE(monitor.getAverageTickTimeUs() == Approx(5000.0));
}

TEST_CASE("[perf-monitor] Double start is safe (no-op)", "[profiling]") {
    PerformanceMonitor monitor;

    monitor.start(10000);
    monitor.start(10000); // Second start should be a no-op

    monitor.stop();
    // If we get here without hanging, double-start was handled correctly
}

TEST_CASE("[perf-monitor] Double stop is safe (no-op)", "[profiling]") {
    PerformanceMonitor monitor;

    monitor.start(10000);
    monitor.stop();
    monitor.stop(); // Second stop should be a no-op

    // If we get here without hanging, double-stop was handled correctly
}

TEST_CASE("[perf-monitor] Destructor stops monitor thread", "[profiling]") {
    // Monitor created in inner scope, destructor should clean up
    {
        PerformanceMonitor monitor;
        monitor.start(10000);
        monitor.recordTickTime(1000);
    }
    // If destructor didn't join, we'd get a crash or hang
}

// ============================================================================
// PerformanceMonitor - Circular Buffer
// ============================================================================

TEST_CASE("[perf-monitor] More than 1000 ticks still works (circular buffer)", "[profiling]") {
    PerformanceMonitor monitor;

    // Record more than TICK_HISTORY_SIZE (1000) ticks
    for (int i = 0; i < 1500; ++i) {
        monitor.recordTickTime(1000);
    }

    // Average should still be correct (uses totalTicks_/totalTickTimeUs_, not buffer)
    REQUIRE(monitor.getAverageTickTimeUs() == Approx(1000.0));

    // Percentiles operate on the circular buffer, so they see only last 1000
    REQUIRE(monitor.getP95TickTimeUs() == 1000);
    REQUIRE(monitor.getP99TickTimeUs() == 1000);
}
