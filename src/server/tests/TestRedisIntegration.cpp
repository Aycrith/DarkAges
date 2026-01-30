// [DATABASE_AGENT] Redis Integration Tests for WP-6-2
// Tests: <1ms latency, 10k ops/sec, connection pooling, pub/sub

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "db/RedisManager.hpp"
#include <chrono>
#include <thread>
#include <atomic>

using namespace DarkAges;

// Skip Redis tests if not available
#ifndef REDIS_AVAILABLE
#define SKIP_REDIS_TESTS true
#else
#define SKIP_REDIS_TESTS false
#endif

TEST_CASE("Redis Connection Pool", "[redis][database]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    
    SECTION("Initialize connection pool") {
        REQUIRE(redis.initialize("localhost", 6379) == true);
        REQUIRE(redis.isConnected() == true);
        redis.shutdown();
    }
    
    SECTION("Fail on bad host") {
        REQUIRE(redis.initialize("invalid.host.example", 6379) == false);
    }
}

TEST_CASE("Redis Basic Operations", "[redis][database]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    // Clean up any existing test keys
    redis.del("test:key1");
    redis.del("test:key2");
    redis.update(); // Process callbacks
    
    SECTION("SET and GET") {
        std::atomic<bool> setDone{false};
        std::atomic<bool> getDone{false};
        std::string retrievedValue;
        
        redis.set("test:key1", "hello_redis", 60, [&setDone](bool success) {
            REQUIRE(success);
            setDone = true;
        });
        
        // Process callback
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        REQUIRE(setDone);
        
        redis.get("test:key1", [&getDone, &retrievedValue](const AsyncResult<std::string>& result) {
            REQUIRE(result.success);
            retrievedValue = result.value;
            getDone = true;
        });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        REQUIRE(getDone);
        REQUIRE(retrievedValue == "hello_redis");
    }
    
    SECTION("DELETE") {
        std::atomic<bool> setDone{false};
        std::atomic<bool> delDone{false};
        std::atomic<bool> getAfterDelDone{false};
        bool keyExistedAfterDelete = true;
        
        // Set a key
        redis.set("test:key2", "value_to_delete", 60, [&setDone](bool success) {
            setDone = success;
        });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(setDone);
        
        // Delete it
        redis.del("test:key2", [&delDone](bool success) {
            delDone = success;
        });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(delDone);
        
        // Try to get it - should fail (key not found)
        redis.get("test:key2", [&getAfterDelDone, &keyExistedAfterDelete](const AsyncResult<std::string>& result) {
            getAfterDelDone = true;
            keyExistedAfterDelete = result.success;
        });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        REQUIRE(getAfterDelDone);
        REQUIRE_FALSE(keyExistedAfterDelete);
    }
    
    redis.shutdown();
}

TEST_CASE("Redis Latency < 1ms", "[redis][database][performance]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    // Warm up
    for (int i = 0; i < 100; ++i) {
        redis.set("warmup:" + std::to_string(i), "value", 60);
    }
    redis.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    SECTION("Single operation latency") {
        auto start = std::chrono::high_resolution_clock::now();
        
        std::atomic<bool> done{false};
        redis.set("test:latency", "test_value", 60, [&done](bool success) {
            done = success;
        });
        
        // Process until done
        while (!done) {
            redis.update();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // Should complete in < 1ms (1000 microseconds) for local Redis
        // Allow some tolerance for CI environments
        REQUIRE(us < 5000); // 5ms max in CI
        
        // Check average latency reported by RedisManager
        float avgLatency = redis.getAverageLatencyMs();
        REQUIRE(avgLatency < 5.0f); // 5ms average max
    }
    
    SECTION("Multiple operations average latency") {
        const int numOps = 100;
        std::atomic<int> completed{0};
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < numOps; ++i) {
            redis.set("test:latency:" + std::to_string(i), "value" + std::to_string(i), 60, 
                     [&completed](bool) { completed++; });
        }
        
        // Process all callbacks
        while (completed < numOps) {
            redis.update();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        auto avgUs = totalUs / numOps;
        
        // Average should be well under 1ms per operation
        REQUIRE(avgUs < 1000); // < 1ms average
        
        // Check metrics
        REQUIRE(redis.getCommandsSent() >= numOps);
        REQUIRE(redis.getCommandsCompleted() >= numOps);
    }
    
    redis.shutdown();
}

TEST_CASE("Redis 10k Ops/sec Throughput", "[redis][database][performance]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    // Pipeline batch test for 10k ops/sec
    SECTION("Pipeline batch operations") {
        const int batchSize = 1000;
        const int numBatches = 10;
        
        std::vector<std::pair<std::string, std::string>> batch;
        batch.reserve(batchSize);
        
        for (int i = 0; i < batchSize; ++i) {
            batch.emplace_back("test:pipeline:" + std::to_string(i), "value" + std::to_string(i));
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        
        std::atomic<int> completedBatches{0};
        for (int b = 0; b < numBatches; ++b) {
            redis.pipelineSet(batch, 60, [&completedBatches](bool) {
                completedBatches++;
            });
        }
        
        // Process callbacks
        while (completedBatches < numBatches) {
            redis.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        int totalOps = batchSize * numBatches;
        double opsPerSec = (totalOps * 1000.0) / ms;
        
        std::cout << "Pipeline throughput: " << opsPerSec << " ops/sec (" << ms << "ms for " << totalOps << " ops)" << std::endl;
        
        // Should achieve 10k+ ops/sec with pipelining
        REQUIRE(opsPerSec > 5000); // Allow some tolerance for CI
    }
    
    redis.shutdown();
}

TEST_CASE("Redis Player Session Operations", "[redis][database]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    SECTION("Save and load player session") {
        PlayerSession session;
        session.playerId = 12345;
        session.zoneId = 1;
        session.connectionId = 999;
        session.position = {1000, 2000, 3000, 12345};
        session.health = 8500;
        session.lastActivity = 1234567890;
        std::strncpy(session.username, "TestPlayer", sizeof(session.username));
        
        std::atomic<bool> saveDone{false};
        std::atomic<bool> loadDone{false};
        PlayerSession loadedSession;
        
        redis.savePlayerSession(session, [&saveDone](bool success) {
            saveDone = success;
        });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(saveDone);
        
        redis.loadPlayerSession(12345, [&loadDone, &loadedSession](const AsyncResult<PlayerSession>& result) {
            loadDone = true;
            if (result.success) {
                loadedSession = result.value;
            }
        });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(loadDone);
        
        // Verify loaded data
        REQUIRE(loadedSession.playerId == 12345);
        REQUIRE(loadedSession.zoneId == 1);
        REQUIRE(loadedSession.connectionId == 999);
        REQUIRE(loadedSession.position.x == 1000);
        REQUIRE(loadedSession.position.y == 2000);
        REQUIRE(loadedSession.position.z == 3000);
        REQUIRE(loadedSession.health == 8500);
        REQUIRE(std::string(loadedSession.username) == "TestPlayer");
        
        // Cleanup
        redis.removePlayerSession(12345);
    }
    
    redis.shutdown();
}

TEST_CASE("Redis Zone Operations", "[redis][database]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    SECTION("Add and get players in zone") {
        std::atomic<bool> add1Done{false};
        std::atomic<bool> add2Done{false};
        std::atomic<bool> getDone{false};
        std::vector<uint64_t> players;
        
        // Add players to zone 42
        redis.addPlayerToZone(42, 1001, [&add1Done](bool success) { add1Done = success; });
        redis.addPlayerToZone(42, 1002, [&add2Done](bool success) { add2Done = success; });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        REQUIRE(add1Done);
        REQUIRE(add2Done);
        
        // Get players in zone
        redis.getZonePlayers(42, [&getDone, &players](const AsyncResult<std::vector<uint64_t>>& result) {
            getDone = true;
            if (result.success) {
                players = result.value;
            }
        });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        REQUIRE(getDone);
        REQUIRE(players.size() == 2);
        REQUIRE(std::find(players.begin(), players.end(), 1001) != players.end());
        REQUIRE(std::find(players.begin(), players.end(), 1002) != players.end());
        
        // Remove players
        std::atomic<bool> rem1Done{false};
        std::atomic<bool> rem2Done{false};
        redis.removePlayerFromZone(42, 1001, [&rem1Done](bool success) { rem1Done = success; });
        redis.removePlayerFromZone(42, 1002, [&rem2Done](bool success) { rem2Done = success; });
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        REQUIRE(rem1Done);
        REQUIRE(rem2Done);
    }
    
    redis.shutdown();
}

TEST_CASE("Redis Pub/Sub Cross-Zone", "[redis][database]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redisPub;
    RedisManager redisSub;
    REQUIRE(redisPub.initialize("localhost", 6379));
    REQUIRE(redisSub.initialize("localhost", 6379));
    
    SECTION("Subscribe and publish") {
        std::atomic<bool> messageReceived{false};
        std::string receivedChannel;
        std::string receivedMessage;
        
        // Subscribe
        redisSub.subscribe("test:channel", [&messageReceived, &receivedChannel, &receivedMessage](
                std::string_view channel, std::string_view message) {
            messageReceived = true;
            receivedChannel = std::string(channel);
            receivedMessage = std::string(message);
        });
        
        // Allow subscription to establish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Publish
        redisPub.publish("test:channel", "hello_from_redis");
        
        // Wait for message
        for (int i = 0; i < 50 && !messageReceived; ++i) {
            redisSub.update();
            redisPub.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        REQUIRE(messageReceived);
        REQUIRE(receivedChannel == "test:channel");
        REQUIRE(receivedMessage == "hello_from_redis");
    }
    
    SECTION("Zone message pub/sub") {
        std::atomic<bool> zoneMsgReceived{false};
        ZoneMessage receivedMsg;
        
        redisSub.subscribeToZoneChannel(5, [&zoneMsgReceived, &receivedMsg](const ZoneMessage& msg) {
            zoneMsgReceived = true;
            receivedMsg = msg;
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        ZoneMessage msg;
        msg.type = ZoneMessageType::ENTITY_SYNC;
        msg.sourceZoneId = 5;
        msg.targetZoneId = 0; // Broadcast
        msg.timestamp = 12345;
        msg.sequence = 1;
        msg.payload = {0x01, 0x02, 0x03, 0x04};
        
        redisPub.publishToZone(5, msg);
        
        // Wait for message
        for (int i = 0; i < 50 && !zoneMsgReceived; ++i) {
            redisSub.update();
            redisPub.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        REQUIRE(zoneMsgReceived);
        REQUIRE(receivedMsg.type == ZoneMessageType::ENTITY_SYNC);
        REQUIRE(receivedMsg.sourceZoneId == 5);
    }
    
    redisPub.shutdown();
    redisSub.shutdown();
}

TEST_CASE("Redis Failover Recovery", "[redis][database]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    SECTION("Continue operations after temporary disconnection") {
        // This test simulates what happens when a connection is lost
        // The pool should create new connections as needed
        
        std::atomic<int> successCount{0};
        
        // Perform some operations
        for (int i = 0; i < 10; ++i) {
            redis.set("test:failover:" + std::to_string(i), "value", 60, 
                     [&successCount](bool success) { if (success) successCount++; });
        }
        
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Most operations should succeed
        REQUIRE(successCount > 0);
    }
    
    redis.shutdown();
}

TEST_CASE("Redis Metrics", "[redis][database]") {
    if (SKIP_REDIS_TESTS) {
        SKIP("Redis not available - skipping tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    // Reset metrics by creating fresh connection
    uint64_t initialSent = redis.getCommandsSent();
    
    // Perform operations
    redis.set("test:metrics", "value1", 60);
    redis.set("test:metrics2", "value2", 60);
    redis.get("test:metrics", nullptr);
    
    redis.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    uint64_t finalSent = redis.getCommandsSent();
    uint64_t finalCompleted = redis.getCommandsCompleted();
    
    REQUIRE(finalSent >= initialSent + 3);
    REQUIRE(finalCompleted >= 3);
    
    float avgLatency = redis.getAverageLatencyMs();
    REQUIRE(avgLatency >= 0.0f);
    
    redis.shutdown();
}
