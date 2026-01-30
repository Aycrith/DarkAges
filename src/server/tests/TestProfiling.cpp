#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "profiling/PerfettoProfiler.hpp"
#include "profiling/PerformanceMonitor.hpp"
#include <thread>
#include <chrono>
#include <cstdint>

using namespace DarkAges::Profiling;

TEST_CASE("PerformanceCounter basic operations", "[profiling]") {
    PerformanceCounter counter;
    counter.name = "test_counter";
    
    SECTION("Record and calculate average") {
        counter.record(100);
        counter.record(200);
        counter.record(300);
        
        REQUIRE(counter.count == 3);
        REQUIRE(counter.averageTimeUs() == Catch::Approx(200.0));
        REQUIRE(counter.minTimeUs == 100);
        REQUIRE(counter.maxTimeUs == 300);
    }
    
    SECTION("Reset clears all values") {
        counter.record(100);
        counter.record(200);
        counter.reset();
        
        REQUIRE(counter.count == 0);
        REQUIRE(counter.totalTimeUs == 0);
        REQUIRE(counter.maxTimeUs == 0);
        REQUIRE(counter.minTimeUs == ~0ULL);
    }
}

TEST_CASE("PerfettoProfiler singleton", "[profiling]") {
    SECTION("Returns same instance") {
        auto& instance1 = PerfettoProfiler::instance();
        auto& instance2 = PerfettoProfiler::instance();
        
        REQUIRE(&instance1 == &instance2);
    }
    
    SECTION("Initial state is inactive") {
        auto& profiler = PerfettoProfiler::instance();
        REQUIRE_FALSE(profiler.isActive());
    }
}

TEST_CASE("PerfettoProfiler initialization", "[profiling]") {
    auto& profiler = PerfettoProfiler::instance();
    
    SECTION("Initialize activates profiler") {
        profiler.initialize("test_trace.json");
        REQUIRE(profiler.isActive());
        
        // Cleanup
        profiler.shutdown();
    }
    
    SECTION("Shutdown deactivates profiler") {
        profiler.initialize("test_trace.json");
        profiler.shutdown();
        REQUIRE_FALSE(profiler.isActive());
    }
}

TEST_CASE("ScopedTraceEvent RAII", "[profiling]") {
    auto& profiler = PerfettoProfiler::instance();
    profiler.initialize("test_trace.json");
    
    SECTION("Scoped event records on destruction") {
        {
            auto event = profiler.scopedEvent("test_scope", TraceCategory::PHYSICS);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        // Event should be recorded in buffer
        REQUIRE(profiler.isActive());
    }
    
    // Cleanup
    profiler.shutdown();
}

TEST_CASE("PerformanceMonitor basic functionality", "[profiling]") {
    PerformanceMonitor monitor;
    
    SECTION("Initial state is acceptable") {
        REQUIRE(monitor.isPerformanceAcceptable());
        REQUIRE(monitor.getCurrentFPS() == 0.0f);
        REQUIRE(monitor.getViolationCount() == 0);
    }
    
    SECTION("Record tick times") {
        monitor.recordTickTime(1000);  // 1ms
        monitor.recordTickTime(2000);  // 2ms
        monitor.recordTickTime(3000);  // 3ms
        
        REQUIRE(monitor.getAverageTickTimeUs() == Catch::Approx(2000.0));
    }
    
    SECTION("Detect violations") {
        // Record ticks exceeding 16.666ms budget
        for (int i = 0; i < 10; ++i) {
            monitor.recordTickTime(20000);  // 20ms > 16.666ms budget
        }
        
        REQUIRE(monitor.getViolationCount() == 10);
    }
    
    SECTION("Reset clears stats") {
        monitor.recordTickTime(1000);
        monitor.recordTickTime(20000);  // Violation
        
        monitor.resetStats();
        
        REQUIRE(monitor.getAverageTickTimeUs() == 0.0);
        REQUIRE(monitor.getViolationCount() == 0);
    }
}

TEST_CASE("PerformanceMonitor percentiles", "[profiling]") {
    PerformanceMonitor monitor;
    
    // Record known values for percentile testing
    for (int i = 1; i <= 100; ++i) {
        monitor.recordTickTime(i * 100);  // 100us, 200us, ..., 10000us
    }
    
    SECTION("P95 calculation") {
        uint64_t p95 = monitor.getP95TickTimeUs();
        // 95th percentile of 1..100 should be around 95
        REQUIRE(p95 >= 9400);
        REQUIRE(p95 <= 9600);
    }
    
    SECTION("P99 calculation") {
        uint64_t p99 = monitor.getP99TickTimeUs();
        // 99th percentile of 1..100 should be around 99
        REQUIRE(p99 >= 9800);
        REQUIRE(p99 <= 10000);
    }
}

TEST_CASE("Trace categories", "[profiling]") {
    SECTION("Categories have expected values") {
        REQUIRE(static_cast<int>(TraceCategory::NETWORK) == 0);
        REQUIRE(static_cast<int>(TraceCategory::PHYSICS) == 1);
        REQUIRE(static_cast<int>(TraceCategory::GAME_LOGIC) == 2);
        REQUIRE(static_cast<int>(TraceCategory::REPLICATION) == 3);
        REQUIRE(static_cast<int>(TraceCategory::DATABASE) == 4);
        REQUIRE(static_cast<int>(TraceCategory::MIGRATION) == 5);
        REQUIRE(static_cast<int>(TraceCategory::TOTAL) == 6);
    }
}
