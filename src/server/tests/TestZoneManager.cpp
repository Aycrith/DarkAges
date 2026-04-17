// ZoneManager unit tests - covers zone player set operations
// Tests stub behavior when Redis is not available

#include <catch2/catch_test_macros.hpp>
#include "db/RedisManager.hpp"
#include "db/ZoneManager.hpp"
#include <atomic>
#include <vector>

using namespace DarkAges;

// Helper to check if Redis is actually running
static bool isRedisAvailable() {
    static bool checked = false;
    static bool available = false;
    if (!checked) {
        checked = true;
        RedisManager testRedis;
        available = testRedis.initialize("localhost", 6379);
        if (available) {
            testRedis.shutdown();
        }
    }
    return available;
}

TEST_CASE("ZoneManager addPlayerToZone fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    // Don't initialize - internal_.connected is false by default
    ZoneManager zm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackResult = true;

    zm.addPlayerToZone(1, 100, [&](bool success) {
        callbackCalled = true;
        callbackResult = success;
    });

    // Process callback queue
    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE_FALSE(callbackResult);
}

TEST_CASE("ZoneManager removePlayerFromZone fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    ZoneManager zm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackResult = true;

    zm.removePlayerFromZone(1, 100, [&](bool success) {
        callbackCalled = true;
        callbackResult = success;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE_FALSE(callbackResult);
}

TEST_CASE("ZoneManager getZonePlayers fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    ZoneManager zm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackSuccess = true;

    zm.getZonePlayers(1, [&](const AsyncResult<std::vector<uint64_t>>& result) {
        callbackCalled = true;
        callbackSuccess = result.success;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE_FALSE(callbackSuccess);
}

TEST_CASE("ZoneManager addPlayerToZone succeeds when connected", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");
    RedisManager redis;
    redis.initialize();  // Sets connected = true
    ZoneManager zm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackResult = false;

    zm.addPlayerToZone(1, 100, [&](bool success) {
        callbackCalled = true;
        callbackResult = success;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE(callbackResult);

    redis.shutdown();
}

TEST_CASE("ZoneManager removePlayerFromZone succeeds when connected", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");
    RedisManager redis;
    redis.initialize();
    ZoneManager zm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackResult = false;

    zm.removePlayerFromZone(1, 100, [&](bool success) {
        callbackCalled = true;
        callbackResult = success;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE(callbackResult);

    redis.shutdown();
}

TEST_CASE("ZoneManager getZonePlayers returns empty when connected", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");
    RedisManager redis;
    redis.initialize();
    ZoneManager zm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackSuccess = false;
    std::vector<uint64_t> players;

    zm.getZonePlayers(1, [&](const AsyncResult<std::vector<uint64_t>>& result) {
        callbackCalled = true;
        callbackSuccess = result.success;
        players = result.value;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE(callbackSuccess);
    REQUIRE(players.empty());

    redis.shutdown();
}

TEST_CASE("ZoneManager handles null callbacks without crashing", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");
    RedisManager redis;
    redis.initialize();
    ZoneManager zm(redis, redis.getInternal());

    // All operations should accept null callbacks without crashing
    REQUIRE_NOTHROW(zm.addPlayerToZone(1, 100));
    REQUIRE_NOTHROW(zm.removePlayerFromZone(1, 100));

    redis.shutdown();
}

TEST_CASE("ZoneManager disconnected null callbacks do not crash", "[foundation][database]") {
    RedisManager redis;
    // Don't initialize - connected = false
    ZoneManager zm(redis, redis.getInternal());

    REQUIRE_NOTHROW(zm.addPlayerToZone(1, 100));
    REQUIRE_NOTHROW(zm.removePlayerFromZone(1, 100));
}

TEST_CASE("ZoneManager zone join and leave workflow", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");
    RedisManager redis;
    redis.initialize();
    ZoneManager zm(redis, redis.getInternal());

    const uint32_t zoneId = 42;
    const uint64_t playerId1 = 1001;
    const uint64_t playerId2 = 1002;

    // Player 1 joins zone
    std::atomic<bool> join1Done{false};
    bool join1Result = false;
    zm.addPlayerToZone(zoneId, playerId1, [&](bool success) {
        join1Result = success;
        join1Done = true;
    });
    redis.update();
    REQUIRE(join1Done);
    REQUIRE(join1Result);

    // Player 2 joins zone
    std::atomic<bool> join2Done{false};
    bool join2Result = false;
    zm.addPlayerToZone(zoneId, playerId2, [&](bool success) {
        join2Result = success;
        join2Done = true;
    });
    redis.update();
    REQUIRE(join2Done);
    REQUIRE(join2Result);

    // Query zone players
    std::atomic<bool> queryDone{false};
    bool querySuccess = false;
    zm.getZonePlayers(zoneId, [&](const AsyncResult<std::vector<uint64_t>>& result) {
        querySuccess = result.success;
        queryDone = true;
    });
    redis.update();
    REQUIRE(queryDone);
    REQUIRE(querySuccess);

    // Player 1 leaves zone
    std::atomic<bool> leaveDone{false};
    bool leaveResult = false;
    zm.removePlayerFromZone(zoneId, playerId1, [&](bool success) {
        leaveResult = success;
        leaveDone = true;
    });
    redis.update();
    REQUIRE(leaveDone);
    REQUIRE(leaveResult);

    redis.shutdown();
}

TEST_CASE("ZoneManager multiple zones", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");
    RedisManager redis;
    redis.initialize();
    ZoneManager zm(redis, redis.getInternal());

    // Add player to multiple zones
    for (uint32_t zoneId = 1; zoneId <= 5; ++zoneId) {
        std::atomic<bool> done{false};
        bool result = false;
        zm.addPlayerToZone(zoneId, 100, [&](bool success) {
            result = success;
            done = true;
        });
        redis.update();
        REQUIRE(done);
        REQUIRE(result);
    }

    // Query each zone
    for (uint32_t zoneId = 1; zoneId <= 5; ++zoneId) {
        std::atomic<bool> done{false};
        bool success = false;
        zm.getZonePlayers(zoneId, [&](const AsyncResult<std::vector<uint64_t>>& result) {
            success = result.success;
            done = true;
        });
        redis.update();
        REQUIRE(done);
        REQUIRE(success);
    }

    redis.shutdown();
}
