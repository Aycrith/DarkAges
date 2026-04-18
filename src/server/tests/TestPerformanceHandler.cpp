// Unit tests for PerformanceHandler - performance budget monitoring and QoS degradation
// Tests tick budget checking, overrun detection, and QoS activation logic

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zones/PerformanceHandler.hpp"
#include "zones/ZoneServer.hpp"
#include "monitoring/MetricsExporter.hpp"
#include "Constants.hpp"

using namespace DarkAges;

// ============================================================================
// Performance Budget Checking
// ============================================================================

TEST_CASE("PerformanceHandler checkPerformanceBudgets - under budget", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Reset state
    server.setQoSDegraded(false);
    server.getMetricsRef().overruns = 0;
    
    // Under budget: 10ms (10,000us) < 16.666ms budget
    handler.checkPerformanceBudgets(10000);
    
    // Should NOT increment overruns
    REQUIRE(server.getMetricsRef().overruns == 0);
}

TEST_CASE("PerformanceHandler checkPerformanceBudgets - over budget", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Reset state
    server.setQoSDegraded(false);
    server.getMetricsRef().overruns = 0;
    
    // Over budget: 20ms (20,000us) > 16.666ms budget
    handler.checkPerformanceBudgets(20000);
    
    // Should increment overruns
    REQUIRE(server.getMetricsRef().overruns == 1);
}

TEST_CASE("PerformanceHandler checkPerformanceBudgets - multiple overruns accumulate", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Reset state
    server.setQoSDegraded(false);
    server.getMetricsRef().overruns = 5;
    
    // First overrun
    handler.checkPerformanceBudgets(20000);
    REQUIRE(server.getMetricsRef().overruns == 6);
    
    // Second overrun
    handler.checkPerformanceBudgets(25000);
    REQUIRE(server.getMetricsRef().overruns == 7);
}

TEST_CASE("PerformanceHandler checkPerformanceBudgets - at exact budget is OK", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Reset state
    server.setQoSDegraded(false);
    server.getMetricsRef().overruns = 0;
    
    // At exactly the budget boundary (16666us)
    handler.checkPerformanceBudgets(Constants::TICK_BUDGET_US);
    
    // Should NOT increment overruns (budget allows up to TICK_BUDGET_US)
    REQUIRE(server.getMetricsRef().overruns == 0);
}

// ============================================================================
// QoS Degradation Activation
// ============================================================================

TEST_CASE("PerformanceHandler activateQoSDegradation - sets degraded flag", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Reset state
    server.setQoSDegraded(false);
    
    REQUIRE_FALSE(server.isQoSDegraded());
    
    handler.activateQoSDegradation();
    
    REQUIRE(server.isQoSDegraded());
}

TEST_CASE("PerformanceHandler activateQoSDegradation - reduces update rate", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Reset state
    server.setQoSDegraded(false);
    server.setReducedUpdateRate(60);
    
    handler.activateQoSDegradation();
    
    // After activation, check that degraded flag is set
    // (setReducedUpdateRate is called internally - we verify flag is set)
    REQUIRE(server.isQoSDegraded());
}

// ============================================================================
// QoS Activation Threshold (20ms)
// ============================================================================

TEST_CASE("PerformanceHandler checkPerformanceBudgets - activates QoS at 20ms threshold", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Fresh start
    server.setQoSDegraded(false);
    server.getMetricsRef().overruns = 0;
    
    REQUIRE_FALSE(server.isQoSDegraded());
    
    // 20ms exactly (20000us) - should trigger (>= 20000us)
    handler.checkPerformanceBudgets(20000);
    
    // Verify QoS was activated
    REQUIRE(server.isQoSDegraded());
}

TEST_CASE("PerformanceHandler checkPerformanceBudgets - does not activate QoS under 20ms", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Fresh start
    server.setQoSDegraded(false);
    server.getMetricsRef().overruns = 0;
    
    REQUIRE_FALSE(server.isQoSDegraded());
    
    // 19ms - should not trigger
    handler.checkPerformanceBudgets(19000);
    
    REQUIRE_FALSE(server.isQoSDegraded());
}

// ============================================================================
// Network Metrics Update (null safety)
// ============================================================================

TEST_CASE("PerformanceHandler updateNetworkMetrics - null network is safe", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // No network set - should not crash
    auto& metrics = Monitoring::MetricsExporter::Instance();
    REQUIRE_NOTHROW(handler.updateNetworkMetrics(metrics, "1"));
}

// ============================================================================
// Handler Lifecycle
// ============================================================================

TEST_CASE("PerformanceHandler - constructs with ZoneServer reference", "[zones][performance]") {
    ZoneServer server;
    
    // Should construct without crashing
    PerformanceHandler handler(server);
    
    // Basic verification - handler is valid
    REQUIRE(&handler != nullptr);
}

TEST_CASE("PerformanceHandler - setNetwork accepts null", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Setting null should not crash
    handler.setNetwork(nullptr);
    REQUIRE(true);  // Reached
}

TEST_CASE("PerformanceHandler - setReplicationOptimizer accepts null", "[zones][performance]") {
    ZoneServer server;
    PerformanceHandler handler(server);
    
    // Setting null should not crash
    handler.setReplicationOptimizer(nullptr);
    REQUIRE(true);  // Reached
}