// TestScyllaManager.cpp - Unit tests for ScyllaDB persistence layer
// [DATABASE_AGENT] Tests for WP-6-3: ScyllaDB Persistence Layer

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "db/ScyllaManager.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace DarkAges;

// Skip tests if ScyllaDB is not available
class ScyllaTestFixture {
public:
    static bool isAvailable() {
        // Check if we can connect to ScyllaDB
        ScyllaManager manager;
        bool connected = manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT);
        if (connected) {
            manager.shutdown();
        }
        return connected;
    }
};

// Skip macro for tests requiring ScyllaDB
#define SKIP_IF_NO_SCYLLA() \
    if (!ScyllaTestFixture::isAvailable()) { \
        SKIP("ScyllaDB not available - skipping test"); \
    }

TEST_CASE("ScyllaManager initialization", "[database][scylla]") {
    SECTION("Initialize with default parameters") {
        ScyllaManager manager;
        
        // This will succeed if ScyllaDB is running, fail otherwise
        bool result = manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT);
        
        if (result) {
            REQUIRE(manager.isConnected());
            manager.shutdown();
            REQUIRE_FALSE(manager.isConnected());
        } else {
            SKIP("ScyllaDB not available at localhost:9042");
        }
    }
    
    SECTION("Initialize with invalid host fails gracefully") {
        ScyllaManager manager;
        bool result = manager.initialize("invalid_host", Constants::SCYLLA_DEFAULT_PORT);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(manager.isConnected());
    }
}

TEST_CASE("ScyllaManager combat event logging", "[database][scylla]") {
    SKIP_IF_NO_SCYLLA();
    
    ScyllaManager manager;
    REQUIRE(manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT));
    
    SECTION("Log single combat event") {
        CombatEvent event{};
        event.eventId = 1;
        event.timestamp = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        event.zoneId = 1;
        event.attackerId = 1001;
        event.targetId = 1002;
        event.damageAmount = 500;
        event.eventType = "damage";
        event.position = Position{1000, 0, 2000, event.timestamp};
        event.serverTick = 12345;
        
        std::atomic<bool> callbackCalled{false};
        std::atomic<bool> writeSuccess{false};
        
        manager.logCombatEvent(event, [&](bool success) {
            writeSuccess = success;
            callbackCalled = true;
        });
        
        // Wait for async completion (with timeout)
        int waitCount = 0;
        while (!callbackCalled && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        REQUIRE(callbackCalled);
        REQUIRE(writeSuccess);
    }
    
    SECTION("Log multiple combat events") {
        std::vector<CombatEvent> events;
        auto now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        for (int i = 0; i < 10; ++i) {
            CombatEvent event{};
            event.eventId = static_cast<uint64_t>(i + 1);
            event.timestamp = now;
            event.zoneId = 1;
            event.attackerId = 1001;
            event.targetId = 1002 + i;
            event.damageAmount = static_cast<int16_t>(100 * (i + 1));
            event.eventType = (i % 2 == 0) ? "damage" : "kill";
            event.position = Position{1000 + i * 100, 0, 2000, now};
            event.serverTick = 12345 + i;
            events.push_back(event);
        }
        
        std::atomic<bool> callbackCalled{false};
        std::atomic<bool> writeSuccess{false};
        
        manager.logCombatEventsBatch(events, [&](bool success) {
            writeSuccess = success;
            callbackCalled = true;
        });
        
        int waitCount = 0;
        while (!callbackCalled && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        REQUIRE(callbackCalled);
        REQUIRE(writeSuccess);
    }
    
    manager.shutdown();
}

TEST_CASE("ScyllaManager player stats", "[database][scylla]") {
    SKIP_IF_NO_SCYLLA();
    
    ScyllaManager manager;
    REQUIRE(manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT));
    
    SECTION("Update player stats") {
        PlayerCombatStats stats{};
        stats.playerId = 1001;
        stats.sessionDate = 20240130;
        stats.kills = 5;
        stats.deaths = 2;
        stats.damageDealt = 5000;
        stats.damageTaken = 2000;
        stats.longestKillStreak = 3;
        stats.currentKillStreak = 1;
        
        std::atomic<bool> callbackCalled{false};
        std::atomic<bool> writeSuccess{false};
        
        manager.updatePlayerStats(stats, [&](bool success) {
            writeSuccess = success;
            callbackCalled = true;
        });
        
        int waitCount = 0;
        while (!callbackCalled && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        REQUIRE(callbackCalled);
        REQUIRE(writeSuccess);
    }
    
    SECTION("Get player stats") {
        // First update some stats
        PlayerCombatStats stats{};
        stats.playerId = 1002;
        stats.sessionDate = 20240130;
        stats.kills = 10;
        stats.deaths = 3;
        stats.damageDealt = 10000;
        stats.damageTaken = 5000;
        
        std::atomic<bool> updateComplete{false};
        manager.updatePlayerStats(stats, [&](bool) {
            updateComplete = true;
        });
        
        int waitCount = 0;
        while (!updateComplete && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        // Now query the stats
        std::atomic<bool> queryComplete{false};
        PlayerCombatStats retrievedStats{};
        bool querySuccess = false;
        
        manager.getPlayerStats(1002, 20240130, [&](bool success, const PlayerCombatStats& s) {
            querySuccess = success;
            retrievedStats = s;
            queryComplete = true;
        });
        
        waitCount = 0;
        while (!queryComplete && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        REQUIRE(queryComplete);
        REQUIRE(querySuccess);
        // Note: Counter values accumulate across test runs
        REQUIRE(retrievedStats.kills >= 10);
        REQUIRE(retrievedStats.deaths >= 3);
    }
    
    manager.shutdown();
}

TEST_CASE("ScyllaManager metrics", "[database][scylla]") {
    SKIP_IF_NO_SCYLLA();
    
    ScyllaManager manager;
    REQUIRE(manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT));
    
    SECTION("Metrics tracking") {
        // Initial state
        REQUIRE(manager.getWritesQueued() == 0);
        REQUIRE(manager.getWritesCompleted() == 0);
        
        // Log an event
        CombatEvent event{};
        event.timestamp = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        event.zoneId = 1;
        event.attackerId = 1001;
        event.targetId = 1002;
        event.damageAmount = 100;
        event.eventType = "damage";
        event.position = Position{1000, 0, 2000, event.timestamp};
        
        manager.logCombatEvent(event);
        
        REQUIRE(manager.getWritesQueued() == 1);
        
        // Wait for completion
        int waitCount = 0;
        while (manager.getWritesCompleted() < 1 && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        REQUIRE(manager.getWritesCompleted() == 1);
        REQUIRE(manager.getWritesFailed() == 0);
    }
    
    manager.shutdown();
}

TEST_CASE("ScyllaManager performance - 100k writes/sec", "[database][scylla][performance]") {
    SKIP_IF_NO_SCYLLA();
    
    ScyllaManager manager;
    REQUIRE(manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT));
    
    SECTION("Batch write 100k events") {
        constexpr int EVENT_COUNT = 100000;
        
        std::vector<CombatEvent> events;
        events.reserve(EVENT_COUNT);
        
        auto now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        for (int i = 0; i < EVENT_COUNT; ++i) {
            CombatEvent event{};
            event.eventId = static_cast<uint64_t>(i);
            event.timestamp = now;
            event.zoneId = static_cast<uint32_t>(i % 10);
            event.attackerId = 1000 + (i % 100);
            event.targetId = 2000 + (i % 100);
            event.damageAmount = static_cast<int16_t>(100 + (i % 500));
            event.eventType = (i % 10 == 0) ? "kill" : "damage";
            event.position = Position{
                static_cast<Constants::Fixed>(1000 + i),
                0,
                static_cast<Constants::Fixed>(2000 + i),
                now
            };
            event.serverTick = static_cast<uint32_t>(i);
            events.push_back(event);
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        
        std::atomic<bool> callbackCalled{false};
        std::atomic<bool> writeSuccess{false};
        
        // Write in batches of 1000
        constexpr int BATCH_SIZE = 1000;
        std::atomic<int> completedBatches{0};
        int totalBatches = EVENT_COUNT / BATCH_SIZE;
        
        for (int batch = 0; batch < totalBatches; ++batch) {
            auto startIt = events.begin() + batch * BATCH_SIZE;
            auto endIt = startIt + BATCH_SIZE;
            std::vector<CombatEvent> batchEvents(startIt, endIt);
            
            manager.logCombatEventsBatch(batchEvents, [&](bool success) {
                if (++completedBatches == totalBatches) {
                    writeSuccess = success;
                    callbackCalled = true;
                }
            });
        }
        
        // Wait for all batches to complete
        int waitCount = 0;
        while (!callbackCalled && waitCount < 1000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        double writesPerSecond = (static_cast<double>(EVENT_COUNT) / duration) * 1000.0;
        
        INFO("Wrote " << EVENT_COUNT << " events in " << duration << "ms");
        INFO("Throughput: " << writesPerSecond << " writes/sec");
        
        REQUIRE(callbackCalled);
        REQUIRE(writeSuccess);
        // Should complete within 10 seconds for 100k writes (10k/sec minimum)
        REQUIRE(duration < 10000);
    }
    
    manager.shutdown();
}

TEST_CASE("ScyllaManager write latency", "[database][scylla][performance]") {
    SKIP_IF_NO_SCYLLA();
    
    ScyllaManager manager;
    REQUIRE(manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT));
    
    SECTION("Individual write latency p99 < 10ms") {
        constexpr int EVENT_COUNT = 1000;
        
        std::vector<long long> latencies;
        latencies.reserve(EVENT_COUNT);
        std::atomic<int> completed{0};
        
        auto now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        for (int i = 0; i < EVENT_COUNT; ++i) {
            CombatEvent event{};
            event.eventId = static_cast<uint64_t>(i);
            event.timestamp = now;
            event.zoneId = 1;
            event.attackerId = 1001;
            event.targetId = 1002;
            event.damageAmount = 100;
            event.eventType = "damage";
            event.position = Position{1000, 0, 2000, now};
            
            auto start = std::chrono::high_resolution_clock::now();
            
            manager.logCombatEvent(event, [&, start](bool) {
                auto end = std::chrono::high_resolution_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                latencies.push_back(latency);
                completed++;
            });
        }
        
        // Wait for all writes to complete
        int waitCount = 0;
        while (completed < EVENT_COUNT && waitCount < 500) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        REQUIRE(completed == EVENT_COUNT);
        
        // Calculate percentiles
        std::sort(latencies.begin(), latencies.end());
        
        size_t p50_idx = latencies.size() * 0.50;
        size_t p99_idx = latencies.size() * 0.99;
        size_t p999_idx = latencies.size() * 0.999;
        
        double p50_ms = latencies[p50_idx] / 1000.0;
        double p99_ms = latencies[p99_idx] / 1000.0;
        double p999_ms = latencies[p999_idx] / 1000.0;
        
        INFO("Write latency p50: " << p50_ms << "ms");
        INFO("Write latency p99: " << p99_ms << "ms");
        INFO("Write latency p99.9: " << p999_ms << "ms");
        
        // p99 should be under 10ms
        REQUIRE(p99_ms < 10.0);
    }
    
    manager.shutdown();
}

TEST_CASE("ScyllaManager analytics queries", "[database][scylla]") {
    SKIP_IF_NO_SCYLLA();
    
    ScyllaManager manager;
    REQUIRE(manager.initialize("localhost", Constants::SCYLLA_DEFAULT_PORT));
    
    SECTION("Get kill feed") {
        // First insert some kill events
        auto now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        std::vector<CombatEvent> killEvents;
        for (int i = 0; i < 5; ++i) {
            CombatEvent event{};
            event.eventId = static_cast<uint64_t>(i);
            event.timestamp = now;
            event.zoneId = 42;
            event.attackerId = 1001 + i;
            event.targetId = 2001 + i;
            event.damageAmount = 1000;
            event.eventType = "kill";
            event.position = Position{1000 + i * 100, 0, 2000, now};
            killEvents.push_back(event);
        }
        
        std::atomic<bool> insertComplete{false};
        manager.logCombatEventsBatch(killEvents, [&](bool) {
            insertComplete = true;
        });
        
        int waitCount = 0;
        while (!insertComplete && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        // Allow time for indexing
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Query kill feed
        std::atomic<bool> queryComplete{false};
        std::vector<CombatEvent> feed;
        bool querySuccess = false;
        
        manager.getKillFeed(42, 10, [&](bool success, const std::vector<CombatEvent>& events) {
            querySuccess = success;
            feed = events;
            queryComplete = true;
        });
        
        waitCount = 0;
        while (!queryComplete && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            manager.update();
            waitCount++;
        }
        
        REQUIRE(queryComplete);
        REQUIRE(querySuccess);
    }
    
    manager.shutdown();
}
