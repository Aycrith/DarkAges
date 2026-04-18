// [PERFORMANCE_AGENT] Unit tests for PerfettoProfiler
// Tests PerfettoProfiler singleton, event tracing, counters, and ScopedTraceEvent RAII

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "profiling/PerfettoProfiler.hpp"
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <utility>

using namespace DarkAges::Profiling;

TEST_CASE("PerfettoProfiler singleton", "[perfetto]") {
    PerfettoProfiler& instance1 = PerfettoProfiler::instance();
    PerfettoProfiler& instance2 = PerfettoProfiler::instance();
    REQUIRE(&instance1 == &instance2);
}

TEST_CASE("PerfettoProfiler lifecycle", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    
    SECTION("Initial state inactive") {
        REQUIRE_FALSE(profiler.isActive());
    }
    
    SECTION("Initialize activates profiler") {
        bool result = profiler.initialize("/tmp/test_trace.perfetto");
        REQUIRE(result);
        REQUIRE(profiler.isActive());
        
        profiler.shutdown();
        REQUIRE_FALSE(profiler.isActive());
    }
    
    SECTION("Re-initialize after shutdown") {
        profiler.initialize("/tmp/test_trace2.perfetto");
        REQUIRE(profiler.isActive());
        
        profiler.shutdown();
        REQUIRE_FALSE(profiler.isActive());
        
        bool result = profiler.initialize("/tmp/test_trace3.perfetto");
        REQUIRE(result);
        REQUIRE(profiler.isActive());
        
        profiler.shutdown();
    }
}

TEST_CASE("PerfettoProfiler event recording", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.initialize("/tmp/test_events.perfetto");
    
    SECTION("beginEvent records without crash") {
        profiler.beginEvent("test_event", TraceCategory::NETWORK);
    }
    
    SECTION("endEvent records without crash") {
        profiler.endEvent("test_event", TraceCategory::PHYSICS);
    }
    
    SECTION("instantEvent records without crash") {
        profiler.instantEvent("marker", TraceCategory::GAME_LOGIC);
    }
    
    SECTION("counterEvent records without crash") {
        profiler.counterEvent("entity_count", 42);
        profiler.counterEvent("cpu_usage", -100);
        profiler.counterEvent("fps", 60);
    }
    
    SECTION("All trace categories handled") {
        profiler.beginEvent("net", TraceCategory::NETWORK);
        profiler.endEvent("net", TraceCategory::NETWORK);
        
        profiler.beginEvent("phys", TraceCategory::PHYSICS);
        profiler.endEvent("phys", TraceCategory::PHYSICS);
        
        profiler.beginEvent("game", TraceCategory::GAME_LOGIC);
        profiler.endEvent("game", TraceCategory::GAME_LOGIC);
        
        profiler.beginEvent("repl", TraceCategory::REPLICATION);
        profiler.endEvent("repl", TraceCategory::REPLICATION);
        
        profiler.beginEvent("db", TraceCategory::DATABASE);
        profiler.endEvent("db", TraceCategory::DATABASE);
        
        profiler.beginEvent("mig", TraceCategory::MIGRATION);
        profiler.endEvent("mig", TraceCategory::MIGRATION);
        
        profiler.beginEvent("total", TraceCategory::TOTAL);
        profiler.endEvent("total", TraceCategory::TOTAL);
    }
    
    profiler.shutdown();
}

TEST_CASE("PerfettoProfiler inactive mode", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.initialize("/tmp/test_inactive.perfetto");
    
    profiler.setActive(false);
    REQUIRE_FALSE(profiler.isActive());
    
    // These should be no-ops when inactive
    profiler.beginEvent("should_not_record", TraceCategory::NETWORK);
    profiler.endEvent("should_not_record", TraceCategory::NETWORK);
    profiler.instantEvent("marker", TraceCategory::GAME_LOGIC);
    profiler.counterEvent("counter", 100);
    
    profiler.setActive(true);
    REQUIRE(profiler.isActive());
    
    // Now events should record
    profiler.beginEvent("should_record", TraceCategory::PHYSICS);
    profiler.endEvent("should_record", TraceCategory::PHYSICS);
    
    profiler.shutdown();
}

TEST_CASE("PerformanceCounter record and statistics", "[perfetto]") {
    PerformanceCounter counter;
    
    SECTION("Empty counter has zero average") {
        REQUIRE(counter.averageTimeUs() == Catch::Approx(0.0));
        REQUIRE(counter.count == 0);
        REQUIRE(counter.totalTimeUs == 0);
    }
    
    SECTION("Single record") {
        counter.record(1000);
        REQUIRE(counter.count == 1);
        REQUIRE(counter.totalTimeUs == 1000);
        REQUIRE(counter.maxTimeUs == 1000);
        REQUIRE(counter.minTimeUs == 1000);
        REQUIRE(counter.averageTimeUs() == Catch::Approx(1000.0));
    }
    
    SECTION("Multiple records average") {
        counter.record(1000);
        counter.record(2000);
        counter.record(3000);
        REQUIRE(counter.count == 3);
        REQUIRE(counter.totalTimeUs == 6000);
        REQUIRE(counter.averageTimeUs() == Catch::Approx(2000.0));
    }
    
    SECTION("Multiple records min and max") {
        counter.record(500);
        counter.record(2000);
        counter.record(1000);
        
        REQUIRE(counter.maxTimeUs == 2000);
        REQUIRE(counter.minTimeUs == 500);
    }
    
    SECTION("Reset clears all counters") {
        counter.record(1000);
        counter.record(2000);
        REQUIRE(counter.count == 2);
        
        counter.reset();
        REQUIRE(counter.count == 0);
        REQUIRE(counter.totalTimeUs == 0);
        REQUIRE(counter.maxTimeUs == 0);
        REQUIRE(counter.minTimeUs == ~0ULL);
        REQUIRE(counter.averageTimeUs() == Catch::Approx(0.0));
    }
    
    SECTION("Large values") {
        counter.record(1'000'000);
        counter.record(2'000'000);
        REQUIRE(counter.averageTimeUs() == Catch::Approx(1'500'000.0));
    }
}

TEST_CASE("PerfettoProfiler counter tracking", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.clearCounters();  // Clean slate
    profiler.initialize("/tmp/test_counters.perfetto");
    
    SECTION("getCounter returns nullptr for unknown counter") {
        REQUIRE(profiler.getCounter("nonexistent") == nullptr);
    }
    
    SECTION("counterEvent creates counter") {
        profiler.counterEvent("my_counter", 100);
        const PerformanceCounter* counter = profiler.getCounter("my_counter");
        REQUIRE(counter != nullptr);
        REQUIRE(counter->count == 1);
        REQUIRE(counter->totalTimeUs == 100);
    }
    
    SECTION("Multiple counter events aggregate") {
        profiler.counterEvent("agg_counter", 10);
        profiler.counterEvent("agg_counter", 20);
        profiler.counterEvent("agg_counter", 30);
        
        const PerformanceCounter* counter = profiler.getCounter("agg_counter");
        REQUIRE(counter != nullptr);
        REQUIRE(counter->count == 3);
        REQUIRE(counter->totalTimeUs == 60);
        REQUIRE(counter->averageTimeUs() == Catch::Approx(20.0));
    }
    
    SECTION("getCounterNames returns all counter names") {
        profiler.counterEvent("counter_a", 1);
        profiler.counterEvent("counter_b", 2);
        profiler.counterEvent("counter_c", 3);
        
        auto names = profiler.getCounterNames();
        REQUIRE(names.size() == 3);
    }
    
    SECTION("resetCounters clears all counters") {
        profiler.counterEvent("reset_counter", 999);
        REQUIRE(profiler.getCounter("reset_counter") != nullptr);
        REQUIRE(profiler.getCounter("reset_counter")->count == 1);
        
        profiler.resetCounters();
        REQUIRE(profiler.getCounter("reset_counter")->count == 0);
    }
    
    profiler.shutdown();
}

TEST_CASE("ScopedTraceEvent RAII behavior", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.initialize("/tmp/test_scoped.perfetto");
    
    SECTION("Scoped event records begin and end") {
        {
            auto scoped = profiler.scopedEvent("scoped_test", TraceCategory::GAME_LOGIC);
        }
    }
    
    SECTION("Scoped event move constructor") {
        {
            auto scoped1 = profiler.scopedEvent("moved_event", TraceCategory::PHYSICS);
            auto scoped2 = std::move(scoped1);
        }
    }
    
    SECTION("Scoped event move assignment") {
        {
            auto scoped1 = profiler.scopedEvent("assigned_event_1", TraceCategory::NETWORK);
            {
                auto scoped2 = profiler.scopedEvent("assigned_event_2", TraceCategory::DATABASE);
                scoped1 = std::move(scoped2);
            }
        }
    }
    
    SECTION("Scoped event with different categories") {
        auto ev1 = profiler.scopedEvent("net_event", TraceCategory::NETWORK);
        auto ev2 = profiler.scopedEvent("phys_event", TraceCategory::PHYSICS);
        auto ev3 = profiler.scopedEvent("game_event", TraceCategory::GAME_LOGIC);
    }
    
    profiler.shutdown();
}

TEST_CASE("PerfettoProfiler TRACE_COUNTER macro expansion", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.initialize("/tmp/test_macro.perfetto");
    
    // Macro expands to profiler.counterEvent(name, value)
    profiler.counterEvent("entity_count", 50);
    profiler.counterEvent("packet_rate", 1000);
    
    REQUIRE(profiler.getCounter("entity_count") != nullptr);
    REQUIRE(profiler.getCounter("packet_rate") != nullptr);
    
    profiler.shutdown();
}

TEST_CASE("PerfettoProfiler TRACE_EVENT macro expansion", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.initialize("/tmp/test_trace_macro.perfetto");
    
    auto scoped = profiler.scopedEvent("macro_event", TraceCategory::REPLICATION);
    
    profiler.shutdown();
}

TEST_CASE("PerfettoProfiler concurrent threads", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.initialize("/tmp/test_concurrent.perfetto");
    
    std::thread t1([&]() {
        for (int i = 0; i < 10; ++i) {
            profiler.beginEvent("thread1_event", TraceCategory::NETWORK);
            profiler.endEvent("thread1_event", TraceCategory::NETWORK);
            profiler.counterEvent("thread1_counter", i);
        }
    });
    
    std::thread t2([&]() {
        for (int i = 0; i < 10; ++i) {
            profiler.beginEvent("thread2_event", TraceCategory::PHYSICS);
            profiler.endEvent("thread2_event", TraceCategory::PHYSICS);
            profiler.counterEvent("thread2_counter", i);
        }
    });
    
    t1.join();
    t2.join();
    
    REQUIRE(profiler.getCounter("thread1_counter") != nullptr);
    REQUIRE(profiler.getCounter("thread2_counter") != nullptr);
    
    profiler.shutdown();
}

TEST_CASE("PerformanceCounter zero division protection", "[perfetto]") {
    PerformanceCounter counter;
    
    SECTION("Empty counter average is zero") {
        double avg = counter.averageTimeUs();
        REQUIRE(avg == 0.0);
    }
    
    SECTION("Zero value record") {
        counter.record(0);
        REQUIRE(counter.averageTimeUs() == Catch::Approx(0.0));
        REQUIRE(counter.minTimeUs == 0);
    }
}

TEST_CASE("PerfettoProfiler high-frequency event recording", "[perfetto]") {
    PerfettoProfiler& profiler = PerfettoProfiler::instance();
    profiler.resetCounters();  // Clean slate
    profiler.initialize("/tmp/test_buffer_growth.perfetto");
    
    // Record many events to test buffer
    for (int i = 0; i < 100; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "event_%d", i);
        profiler.beginEvent(name, TraceCategory::GAME_LOGIC);
        profiler.endEvent(name, TraceCategory::GAME_LOGIC);
    }
    
    // Counter tracking
    for (int i = 0; i < 100; ++i) {
        profiler.counterEvent("bulk_counter", i);
    }
    
    auto names = profiler.getCounterNames();
    REQUIRE(names.size() == 1);
    
    const PerformanceCounter* counter = profiler.getCounter("bulk_counter");
    REQUIRE(counter != nullptr);
    REQUIRE(counter->count == 100);
    
    profiler.shutdown();
}
