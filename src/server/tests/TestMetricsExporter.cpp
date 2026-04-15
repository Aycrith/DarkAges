// MetricsExporter unit tests

#include <catch2/catch_test_macros.hpp>
#include "monitoring/MetricsExporter.hpp"

using namespace DarkAges::Monitoring;

// ============================================================================
// Counter Tests
// ============================================================================

TEST_CASE("Counter basic increment", "[metrics]") {
    Counter counter("test_counter", "A test counter");

    SECTION("Default increment by 1") {
        counter.Increment();
        REQUIRE(counter.GetValue() == 1.0);
    }

    SECTION("Increment by custom value") {
        counter.Increment(5.0);
        REQUIRE(counter.GetValue() == 5.0);
    }

    SECTION("Multiple increments accumulate") {
        counter.Increment(3.0);
        counter.Increment(7.0);
        REQUIRE(counter.GetValue() == 10.0);
    }

    SECTION("GetValue with no data returns 0") {
        REQUIRE(counter.GetValue() == 0.0);
    }
}

TEST_CASE("Counter with labels", "[metrics]") {
    Counter counter("labeled_counter", "Counter with labels");

    SECTION("Labels distinguish values") {
        counter.Increment({{"region", "us"}});
        counter.Increment({{"region", "eu"}});
        counter.Increment({{"region", "eu"}});

        REQUIRE(counter.GetValue({{"region", "us"}}) == 1.0);
        REQUIRE(counter.GetValue({{"region", "eu"}}) == 2.0);
    }

    SECTION("Unlabeled query on labeled counter returns 0") {
        counter.Increment({{"region", "us"}});
        REQUIRE(counter.GetValue() == 0.0);
    }

    SECTION("Multiple labels") {
        counter.Increment({{"method", "GET"}, {"status", "200"}});
        counter.Increment({{"method", "GET"}, {"status", "404"}});

        REQUIRE(counter.GetValue({{"method", "GET"}, {"status", "200"}}) == 1.0);
        REQUIRE(counter.GetValue({{"method", "GET"}, {"status", "404"}}) == 1.0);
    }
}

TEST_CASE("Counter serialization", "[metrics]") {
    Counter counter("http_requests_total", "Total HTTP requests");

    SECTION("Empty counter serializes header only") {
        std::string output = counter.Serialize();
        REQUIRE(output.find("# HELP http_requests_total Total HTTP requests") != std::string::npos);
        REQUIRE(output.find("# TYPE http_requests_total counter") != std::string::npos);
    }

    SECTION("Counter with value serializes correctly") {
        counter.Increment(42.0);
        std::string output = counter.Serialize();
        REQUIRE(output.find("http_requests_total 42") != std::string::npos);
    }

    SECTION("Labeled counter serializes with braces") {
        counter.Increment(10.0, {{"region", "us"}});
        std::string output = counter.Serialize();
        REQUIRE(output.find("http_requests_total{region=\"us\"}") != std::string::npos);
    }
}

// ============================================================================
// Gauge Tests
// ============================================================================

TEST_CASE("Gauge basic operations", "[metrics]") {
    Gauge gauge("test_gauge", "A test gauge");

    SECTION("Set and GetValue") {
        gauge.Set(42.5);
        REQUIRE(gauge.GetValue() == 42.5);
    }

    SECTION("Increment") {
        gauge.Set(10.0);
        gauge.Increment(5.0);
        REQUIRE(gauge.GetValue() == 15.0);
    }

    SECTION("Default increment by 1") {
        gauge.Increment();
        REQUIRE(gauge.GetValue() == 1.0);
    }

    SECTION("Decrement") {
        gauge.Set(20.0);
        gauge.Decrement(3.0);
        REQUIRE(gauge.GetValue() == 17.0);
    }

    SECTION("Default decrement by 1") {
        gauge.Set(10.0);
        gauge.Decrement();
        REQUIRE(gauge.GetValue() == 9.0);
    }

    SECTION("GetValue with no data returns 0") {
        REQUIRE(gauge.GetValue() == 0.0);
    }

    SECTION("Gauge can go negative") {
        gauge.Set(1.0);
        gauge.Decrement(5.0);
        REQUIRE(gauge.GetValue() == -4.0);
    }
}

TEST_CASE("Gauge with labels", "[metrics]") {
    Gauge gauge("cpu_usage", "CPU usage");

    SECTION("Labels distinguish values") {
        gauge.Set(50.0, {{"core", "0"}});
        gauge.Set(75.0, {{"core", "1"}});

        REQUIRE(gauge.GetValue({{"core", "0"}}) == 50.0);
        REQUIRE(gauge.GetValue({{"core", "1"}}) == 75.0);
    }

    SECTION("Overwrite existing labeled value") {
        gauge.Set(10.0, {{"host", "a"}});
        gauge.Set(20.0, {{"host", "a"}});
        REQUIRE(gauge.GetValue({{"host", "a"}}) == 20.0);
    }
}

TEST_CASE("Gauge serialization", "[metrics]") {
    Gauge gauge("active_connections", "Active connections");

    SECTION("Empty gauge serializes header only") {
        std::string output = gauge.Serialize();
        REQUIRE(output.find("# HELP active_connections Active connections") != std::string::npos);
        REQUIRE(output.find("# TYPE active_connections gauge") != std::string::npos);
    }

    SECTION("Gauge with value serializes correctly") {
        gauge.Set(3.14);
        std::string output = gauge.Serialize();
        REQUIRE(output.find("active_connections 3.14") != std::string::npos);
    }
}

// ============================================================================
// Histogram Tests
// ============================================================================

TEST_CASE("Histogram basic observation", "[metrics]") {
    Histogram histogram("request_duration", "Request duration", {0.1, 0.5, 1.0, 5.0});

    SECTION("Observe a value") {
        histogram.Observe(0.3);
        std::string output = histogram.Serialize();
        REQUIRE(output.find("# TYPE request_duration histogram") != std::string::npos);
        REQUIRE(output.find("request_duration_count 1") != std::string::npos);
        REQUIRE(output.find("request_duration_sum 0.30") != std::string::npos);
    }

    SECTION("Multiple observations") {
        histogram.Observe(0.05);
        histogram.Observe(0.3);
        histogram.Observe(2.0);
        std::string output = histogram.Serialize();
        REQUIRE(output.find("request_duration_count 3") != std::string::npos);
        REQUIRE(output.find("request_duration_sum 2.35") != std::string::npos);
    }

    SECTION("Empty histogram serializes header") {
        std::string output = histogram.Serialize();
        REQUIRE(output.find("# HELP request_duration Request duration") != std::string::npos);
        REQUIRE(output.find("# TYPE request_duration histogram") != std::string::npos);
    }
}

TEST_CASE("Histogram bucket distribution", "[metrics]") {
    Histogram histogram("latency", "Latency", {1.0, 5.0, 10.0});

    SECTION("Value lands in correct bucket") {
        histogram.Observe(3.0);
        std::string output = histogram.Serialize();
        // Bucket format uses setprecision(4) so 1.0 -> "1.0000"
        REQUIRE(output.find("le=\"1.0000\"} 0") != std::string::npos);
        REQUIRE(output.find("le=\"5.0000\"} 1") != std::string::npos);
    }

    SECTION("Infinity bucket catches all") {
        histogram.Observe(1000.0);
        std::string output = histogram.Serialize();
        REQUIRE(output.find("le=\"+Inf\"} 1") != std::string::npos);
    }

    SECTION("Zero value lands in first bucket") {
        histogram.Observe(0.0);
        std::string output = histogram.Serialize();
        REQUIRE(output.find("le=\"1.0000\"} 1") != std::string::npos);
    }
}

TEST_CASE("Histogram with labels", "[metrics]") {
    Histogram histogram("req_dur", "Request duration", {1.0, 5.0});

    SECTION("Labeled observations") {
        histogram.Observe(2.0, {{"endpoint", "/api"}});
        histogram.Observe(0.5, {{"endpoint", "/health"}});

        std::string output = histogram.Serialize();
        REQUIRE(output.find("endpoint=\"/api\"") != std::string::npos);
        REQUIRE(output.find("endpoint=\"/health\"") != std::string::npos);
    }
}

// ============================================================================
// MetricsExporter Tests
// ============================================================================

TEST_CASE("MetricsExporter instance is singleton", "[metrics]") {
    auto& a = MetricsExporter::Instance();
    auto& b = MetricsExporter::Instance();
    REQUIRE(&a == &b);
}

TEST_CASE("MetricsExporter Create and Get metrics", "[metrics]") {
    auto& exporter = MetricsExporter::Instance();

    SECTION("Create and retrieve custom counter") {
        auto* c = exporter.CreateCounter("my_custom_counter", "Custom");
        REQUIRE(c != nullptr);
        REQUIRE(exporter.GetCounter("my_custom_counter") == c);
    }

    SECTION("Create and retrieve custom gauge") {
        auto* g = exporter.CreateGauge("my_custom_gauge", "Custom");
        REQUIRE(g != nullptr);
        REQUIRE(exporter.GetGauge("my_custom_gauge") == g);
    }

    SECTION("Create and retrieve custom histogram") {
        auto* h = exporter.CreateHistogram("my_custom_hist", "Custom", {1.0, 5.0});
        REQUIRE(h != nullptr);
        REQUIRE(exporter.GetHistogram("my_custom_hist") == h);
    }

    SECTION("GetCounter returns nullptr for unknown") {
        REQUIRE(exporter.GetCounter("nonexistent_counter_xyz") == nullptr);
    }

    SECTION("GetGauge returns nullptr for unknown") {
        REQUIRE(exporter.GetGauge("nonexistent_gauge_xyz") == nullptr);
    }

    SECTION("GetHistogram returns nullptr for unknown") {
        REQUIRE(exporter.GetHistogram("nonexistent_histogram_xyz") == nullptr);
    }
}

TEST_CASE("MetricsExporter SerializeAll", "[metrics]") {
    auto& exporter = MetricsExporter::Instance();

    // Create some metrics to ensure SerializeAll has content
    exporter.CreateCounter("serialize_test_counter", "Test counter for serialize");
    exporter.CreateGauge("serialize_test_gauge", "Test gauge for serialize");

    SECTION("SerializeAll produces non-empty output") {
        std::string output = exporter.SerializeAll();
        REQUIRE_FALSE(output.empty());
    }

    SECTION("SerializeAll includes created counter") {
        std::string output = exporter.SerializeAll();
        REQUIRE(output.find("serialize_test_counter") != std::string::npos);
    }

    SECTION("SerializeAll includes created gauge") {
        std::string output = exporter.SerializeAll();
        REQUIRE(output.find("serialize_test_gauge") != std::string::npos);
    }

    SECTION("Counter with value appears in SerializeAll") {
        auto* c = exporter.GetCounter("serialize_test_counter");
        REQUIRE(c != nullptr);
        c->Increment(99.0);
        std::string output = exporter.SerializeAll();
        REQUIRE(output.find("serialize_test_counter 99") != std::string::npos);
    }

    SECTION("Gauge with value appears in SerializeAll") {
        auto* g = exporter.GetGauge("serialize_test_gauge");
        REQUIRE(g != nullptr);
        g->Set(7.5);
        std::string output = exporter.SerializeAll();
        REQUIRE(output.find("serialize_test_gauge 7.50") != std::string::npos);
    }
}

// ============================================================================
// ScopedTimer Tests
// ============================================================================

TEST_CASE("ScopedTimer records duration to histogram", "[metrics]") {
    Histogram histogram("scoped_timer_test", "ScopedTimer test", {1.0, 10.0, 100.0, 1000.0});

    SECTION("Timer records on scope exit") {
        {
            ScopedTimer timer(histogram);
            // Do minimal work
        }
        std::string output = histogram.Serialize();
        REQUIRE(output.find("scoped_timer_test_count 1") != std::string::npos);
    }

    SECTION("ElapsedMs returns non-negative value") {
        ScopedTimer timer(histogram);
        double elapsed = timer.ElapsedMs();
        REQUIRE(elapsed >= 0.0);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Counter edge cases", "[metrics]") {
    Counter counter("edge_counter", "Edge cases");

    SECTION("Zero increment") {
        counter.Increment(0.0);
        REQUIRE(counter.GetValue() == 0.0);
    }

    SECTION("Large increment") {
        counter.Increment(1e9);
        REQUIRE(counter.GetValue() == 1e9);
    }

    SECTION("Increment with no labels uses default") {
        counter.Increment();
        counter.Increment(5.0);
        REQUIRE(counter.GetValue() == 6.0);
    }
}

TEST_CASE("Gauge edge cases", "[metrics]") {
    Gauge gauge("edge_gauge", "Edge cases");

    SECTION("Zero value") {
        gauge.Set(0.0);
        REQUIRE(gauge.GetValue() == 0.0);
    }

    SECTION("Negative value") {
        gauge.Set(-100.0);
        REQUIRE(gauge.GetValue() == -100.0);
    }

    SECTION("Very large value") {
        gauge.Set(1e15);
        REQUIRE(gauge.GetValue() == 1e15);
    }
}

TEST_CASE("Histogram edge cases", "[metrics]") {
    SECTION("Single bucket") {
        Histogram histogram("single_bucket", "Single", {10.0});
        histogram.Observe(5.0);
        histogram.Observe(15.0);
        std::string output = histogram.Serialize();
        REQUIRE(output.find("single_bucket_count 2") != std::string::npos);
    }

    SECTION("Empty buckets vector still gets Inf bucket") {
        Histogram histogram("no_buckets", "No buckets", {});
        histogram.Observe(100.0);
        std::string output = histogram.Serialize();
        REQUIRE(output.find("le=\"+Inf\"} 1") != std::string::npos);
    }
}
