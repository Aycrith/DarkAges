#include <catch2/catch_test_macros.hpp>
#include "security/TrafficAnalyzer.hpp"

using namespace DarkAges::Security;

// ============================================================================
// TrafficAnalyzer - Current Stats
// ============================================================================

TEST_CASE("[traffic-analyzer] Initial state has zero stats", "[security]") {
    TrafficAnalyzer analyzer;

    auto stats = analyzer.getCurrentStats(1000);
    REQUIRE(stats.connectionsPerSecond == 0);
    REQUIRE(stats.packetsPerSecond == 0);
    REQUIRE(stats.bytesPerSecond == 0);
    REQUIRE(stats.uniqueIPs == 0);
}

TEST_CASE("[traffic-analyzer] Record connections and check stats", "[security]") {
    TrafficAnalyzer analyzer;

    // Record some connections in the same second window
    analyzer.recordConnection("192.168.1.1", 100);
    analyzer.recordConnection("192.168.1.2", 200);
    analyzer.recordConnection("192.168.1.3", 300);

    auto stats = analyzer.getCurrentStats(500);
    REQUIRE(stats.connectionsPerSecond == 3);
    REQUIRE(stats.uniqueIPs == 3);
}

TEST_CASE("[traffic-analyzer] Record duplicate IPs counts once for unique", "[security]") {
    TrafficAnalyzer analyzer;

    analyzer.recordConnection("192.168.1.1", 100);
    analyzer.recordConnection("192.168.1.1", 200);
    analyzer.recordConnection("192.168.1.2", 300);

    auto stats = analyzer.getCurrentStats(500);
    REQUIRE(stats.connectionsPerSecond == 3);    // 3 total connections
    REQUIRE(stats.uniqueIPs == 2);               // 2 unique IPs
}

TEST_CASE("[traffic-analyzer] Record packets and check stats", "[security]") {
    TrafficAnalyzer analyzer;

    analyzer.recordPacket(100, 100);
    analyzer.recordPacket(200, 200);
    analyzer.recordPacket(500, 300);

    auto stats = analyzer.getCurrentStats(500);
    REQUIRE(stats.packetsPerSecond == 3);
    REQUIRE(stats.bytesPerSecond == 800); // 100 + 200 + 500
}

TEST_CASE("[traffic-analyzer] Mixed connections and packets", "[security]") {
    TrafficAnalyzer analyzer;

    analyzer.recordConnection("10.0.0.1", 100);
    analyzer.recordPacket(64, 150);
    analyzer.recordConnection("10.0.0.2", 200);
    analyzer.recordPacket(128, 250);

    auto stats = analyzer.getCurrentStats(500);
    REQUIRE(stats.connectionsPerSecond == 2);
    REQUIRE(stats.packetsPerSecond == 2);
    REQUIRE(stats.bytesPerSecond == 192); // 64 + 128
    REQUIRE(stats.uniqueIPs == 2);
}

// ============================================================================
// TrafficAnalyzer - Window Rotation
// ============================================================================

TEST_CASE("[traffic-analyzer] Window rotates after 1 second", "[security]") {
    TrafficAnalyzer analyzer;

    // Record in first window
    analyzer.recordConnection("10.0.0.1", 100);
    analyzer.recordPacket(100, 100);

    auto stats1 = analyzer.getCurrentStats(500);
    REQUIRE(stats1.connectionsPerSecond == 1);
    REQUIRE(stats1.packetsPerSecond == 1);

    // Move past 1 second boundary by recording in new window
    // (this triggers rotation of the old window)
    analyzer.recordConnection("10.0.0.2", 1500);

    // Current window should only have the new record
    auto stats2 = analyzer.getCurrentStats(1500);
    REQUIRE(stats2.connectionsPerSecond == 1); // The new connection
    REQUIRE(stats2.uniqueIPs == 1);
}

TEST_CASE("[traffic-analyzer] Records in new window don't affect old", "[security]") {
    TrafficAnalyzer analyzer;

    // Window 1: time 0-1000
    analyzer.recordConnection("10.0.0.1", 100);
    analyzer.recordConnection("10.0.0.2", 200);

    // Window 2: time 1100-2000 (triggers rotation)
    analyzer.recordConnection("10.0.0.3", 1100);

    // Current window should only have the new record
    auto stats = analyzer.getCurrentStats(1500);
    REQUIRE(stats.connectionsPerSecond == 1);
    REQUIRE(stats.uniqueIPs == 1);
}

// ============================================================================
// TrafficAnalyzer - Attack Detection
// ============================================================================

TEST_CASE("[traffic-analyzer] No attack with insufficient data", "[security]") {
    TrafficAnalyzer analyzer;

    // Need minBaselineSamples (default 300) / 60 = ~5 windows of history
    // With no history, should return false
    analyzer.recordConnection("10.0.0.1", 100);
    REQUIRE_FALSE(analyzer.detectAttack(500));
}

TEST_CASE("[traffic-analyzer] Detect connection spike", "[security]") {
    TrafficAnalyzer::Config config;
    config.spikeThresholdPercent = 200; // 200% = 2x baseline triggers
    config.minBaselineSamples = 60;     // 1 minute

    TrafficAnalyzer analyzer(config);

    // Build baseline: 5 connections per second for 5 seconds
    for (uint32_t t = 0; t < 5; ++t) {
        uint32_t baseTime = t * 1100 + 100; // Each in new window
        for (int i = 0; i < 5; ++i) {
            analyzer.recordConnection("10.0.0." + std::to_string(i), baseTime + i * 10);
        }
        // Force window rotation by calling update
        analyzer.update(baseTime + 1100);
    }

    // Now create a spike: 20 connections in current window (> 2x baseline of 5)
    for (int i = 0; i < 20; ++i) {
        analyzer.recordConnection("10.0.0." + std::to_string(i), 6000 + i);
    }

    REQUIRE(analyzer.detectAttack(6500));
}

TEST_CASE("[traffic-analyzer] No attack under normal traffic", "[security]") {
    TrafficAnalyzer::Config config;
    config.spikeThresholdPercent = 200;
    config.minBaselineSamples = 60;

    TrafficAnalyzer analyzer(config);

    // Build baseline: consistent 5 connections per second
    for (uint32_t t = 0; t < 5; ++t) {
        uint32_t baseTime = t * 1100 + 100;
        for (int i = 0; i < 5; ++i) {
            analyzer.recordConnection("10.0.0." + std::to_string(i), baseTime + i * 10);
        }
        analyzer.update(baseTime + 1100);
    }

    // Current window: same level of traffic
    for (int i = 0; i < 5; ++i) {
        analyzer.recordConnection("10.0.0." + std::to_string(i), 6000 + i * 10);
    }

    REQUIRE_FALSE(analyzer.detectAttack(6500));
}

// ============================================================================
// TrafficAnalyzer - Update
// ============================================================================

TEST_CASE("[traffic-analyzer] Update triggers window rotation", "[security]") {
    TrafficAnalyzer analyzer;

    analyzer.recordConnection("10.0.0.1", 100);
    analyzer.recordPacket(100, 100);

    // Update with time past window boundary
    analyzer.update(1500);

    // New window should be empty
    auto stats = analyzer.getCurrentStats(1500);
    REQUIRE(stats.connectionsPerSecond == 0);
    REQUIRE(stats.packetsPerSecond == 0);
}

TEST_CASE("[traffic-analyzer] Update within window does nothing", "[security]") {
    TrafficAnalyzer analyzer;

    analyzer.recordConnection("10.0.0.1", 100);
    analyzer.update(500); // Within same window

    auto stats = analyzer.getCurrentStats(500);
    REQUIRE(stats.connectionsPerSecond == 1);
}
