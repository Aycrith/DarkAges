// ZoneManager unit tests
// Tests: zone player set operations (SADD/SREM/SMEMBERS),
//        disconnected error handling, callback queuing, and stub fallback behavior

#include <catch2/catch_test_macros.hpp>
#include "db/ZoneManager.hpp"
#include "db/RedisManager.hpp"
#include "db/RedisInternal.hpp"

#ifdef REDIS_AVAILABLE
#include <hiredis.h>
#endif

#include <chrono>
#include <functional>
#include <string>
#include <vector>

using namespace DarkAges;

// ============================================================================
// Helper: drain callback queue manually (stub update() is a no-op)
// ============================================================================

static void drainCallbacks(RedisInternal& internal) {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(internal.callbackMutex);
        if (!internal.callbackQueue.empty()) {
            cb = internal.callbackQueue.front().func;
            internal.callbackQueue.pop();
        }
    }
    if (cb) cb();
}

static int drainAllCallbacks(RedisInternal& internal) {
    int count = 0;
    while (true) {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(internal.callbackMutex);
            if (internal.callbackQueue.empty()) break;
            cb = internal.callbackQueue.front().func;
            internal.callbackQueue.pop();
        }
        if (cb) {
            cb();
            count++;
        }
    }
    return count;
}

// ============================================================================
// Unconditional tests (compile in both REDIS_AVAILABLE and stub modes)
// ============================================================================

TEST_CASE("ZoneManager disconnected behavior", "[foundation][database]") {
    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    RedisInternal& internal = redis.getInternal();
    internal.connected = false;  // Simulate disconnected

    ZoneManager zones(redis, internal);

    SECTION("addPlayerToZone fires error callback when disconnected") {
        uint64_t sentBefore = internal.commandsSent_.load();
        uint64_t failedBefore = internal.commandsFailed_.load();

        bool callbackFired = false;
        bool success = true;

        zones.addPlayerToZone(1, 1001, [&](bool result) {
            callbackFired = true;
            success = result;
        });

        REQUIRE(internal.commandsSent_.load() == sentBefore + 1);
        REQUIRE(internal.commandsFailed_.load() == failedBefore + 1);

        // Drain callback
        drainCallbacks(internal);
        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
    }

    SECTION("removePlayerFromZone fires error callback when disconnected") {
        uint64_t sentBefore = internal.commandsSent_.load();
        uint64_t failedBefore = internal.commandsFailed_.load();

        bool callbackFired = false;
        bool success = true;

        zones.removePlayerFromZone(2, 2002, [&](bool result) {
            callbackFired = true;
            success = result;
        });

        REQUIRE(internal.commandsSent_.load() == sentBefore + 1);
        REQUIRE(internal.commandsFailed_.load() == failedBefore + 1);

        drainCallbacks(internal);
        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
    }

    SECTION("getZonePlayers fires error callback when disconnected") {
        uint64_t sentBefore = internal.commandsSent_.load();
        uint64_t failedBefore = internal.commandsFailed_.load();

        bool callbackFired = false;
        AsyncResult<std::vector<uint64_t>> result;
        result.success = true;

        zones.getZonePlayers(3, [&](const AsyncResult<std::vector<uint64_t>>& r) {
            callbackFired = true;
            result = r;
        });

        REQUIRE(internal.commandsSent_.load() == sentBefore + 1);
        REQUIRE(internal.commandsFailed_.load() == failedBefore + 1);

        drainCallbacks(internal);
        REQUIRE(callbackFired);
        REQUIRE_FALSE(result.success);
        REQUIRE(result.error == "Not connected");
    }
}

TEST_CASE("ZoneManager stub connected behavior", "[foundation][database]") {
    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    RedisInternal& internal = redis.getInternal();
    internal.connected = true;  // Stub mode (no real Redis)

    ZoneManager zones(redis, internal);

    SECTION("addPlayerToZone fires success callback in stub mode") {
        uint64_t sentBefore = internal.commandsSent_.load();
        uint64_t completedBefore = internal.commandsCompleted_.load();

        bool callbackFired = false;
        bool success = false;

        zones.addPlayerToZone(10, 9999, [&](bool result) {
            callbackFired = true;
            success = result;
        });

        REQUIRE(internal.commandsSent_.load() == sentBefore + 1);

        // In stub mode, the callback is queued but update() is a no-op
        // Manual drain required
        int drained = drainAllCallbacks(internal);
        REQUIRE(drained == 1);
        REQUIRE(callbackFired);
        REQUIRE(success);

        // Stub adds to commandsCompleted_ (not failed)
        REQUIRE(internal.commandsCompleted_.load() == completedBefore + 1);
    }

    SECTION("removePlayerFromZone fires success callback in stub mode") {
        bool callbackFired = false;
        bool success = false;

        zones.removePlayerFromZone(10, 9999, [&](bool result) {
            callbackFired = true;
            success = result;
        });

        drainAllCallbacks(internal);
        REQUIRE(callbackFired);
        REQUIRE(success);
    }

    SECTION("getZonePlayers returns empty vector in stub mode") {
        bool callbackFired = false;
        AsyncResult<std::vector<uint64_t>> result;

        zones.getZonePlayers(10, [&](const AsyncResult<std::vector<uint64_t>>& r) {
            callbackFired = true;
            result = r;
        });

        drainAllCallbacks(internal);
        REQUIRE(callbackFired);
        REQUIRE(result.success);
        REQUIRE(result.value.empty());  // Stub returns empty vector
    }
}

TEST_CASE("ZoneManager without callback", "[foundation][database]") {
    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    RedisInternal& internal = redis.getInternal();
    internal.connected = false;

    ZoneManager zones(redis, internal);

    SECTION("addPlayerToZone without callback does not crash") {
        uint64_t sentBefore = internal.commandsSent_.load();
        zones.addPlayerToZone(1, 1001, nullptr);
        REQUIRE(internal.commandsSent_.load() == sentBefore + 1);
        REQUIRE(internal.commandsFailed_.load() == 1);  // Disconnected = failed
    }

    SECTION("removePlayerFromZone without callback does not crash") {
        uint64_t sentBefore = internal.commandsSent_.load();
        zones.removePlayerFromZone(1, 1001, nullptr);
        REQUIRE(internal.commandsSent_.load() == sentBefore + 1);
    }

    SECTION("getZonePlayers without callback does not crash") {
        uint64_t sentBefore = internal.commandsSent_.load();
        zones.getZonePlayers(1, nullptr);
        REQUIRE(internal.commandsSent_.load() == sentBefore + 1);
        REQUIRE(internal.commandsFailed_.load() == 1);  // null callback = failed
    }
}

TEST_CASE("ZoneManager metrics accumulation", "[foundation][database]") {
    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    RedisInternal& internal = redis.getInternal();

    ZoneManager zones(redis, internal);

    SECTION("multiple operations accumulate metrics correctly") {
        // Connected mode
        internal.connected = true;

        zones.addPlayerToZone(1, 100, nullptr);
        zones.addPlayerToZone(1, 200, nullptr);
        zones.removePlayerFromZone(1, 100, nullptr);
        zones.getZonePlayers(1, [](const AsyncResult<std::vector<uint64_t>>&) {});

        drainAllCallbacks(internal);

        // 4 commands sent
        REQUIRE(internal.commandsSent_.load() >= 4);
        // In stub mode, all succeed when connected
        REQUIRE(internal.commandsFailed_.load() == 0);
    }

    SECTION("switching between connected and disconnected updates metrics") {
        // Start connected
        internal.connected = true;
        zones.addPlayerToZone(1, 100, nullptr);
        drainAllCallbacks(internal);
        REQUIRE(internal.commandsCompleted_.load() == 1);

        // Disconnect
        internal.connected = false;
        zones.addPlayerToZone(1, 200, nullptr);
        drainAllCallbacks(internal);
        REQUIRE(internal.commandsFailed_.load() == 1);

        // Reconnect
        internal.connected = true;
        zones.addPlayerToZone(1, 300, nullptr);
        drainAllCallbacks(internal);
        REQUIRE(internal.commandsCompleted_.load() == 2);
    }
}

TEST_CASE("ZoneManager callback queue ordering", "[foundation][database]") {
    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    RedisInternal& internal = redis.getInternal();
    internal.connected = true;

    ZoneManager zones(redis, internal);

    SECTION("callbacks fire in FIFO order") {
        std::vector<int> order;

        zones.addPlayerToZone(1, 100, [&order](bool) { order.push_back(1); });
        zones.addPlayerToZone(1, 200, [&order](bool) { order.push_back(2); });
        zones.removePlayerFromZone(1, 100, [&order](bool) { order.push_back(3); });
        zones.getZonePlayers(1, [&order](const AsyncResult<std::vector<uint64_t>>&) { order.push_back(4); });

        drainAllCallbacks(internal);

        REQUIRE(order.size() == 4);
        REQUIRE(order[0] == 1);
        REQUIRE(order[1] == 2);
        REQUIRE(order[2] == 3);
        REQUIRE(order[3] == 4);
    }
}

TEST_CASE("ZoneManager multiple players same zone", "[foundation][database]") {
    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    RedisInternal& internal = redis.getInternal();
    internal.connected = true;

    ZoneManager zones(redis, internal);

    SECTION("adding multiple players increments metrics per operation") {
        uint64_t sentBefore = internal.commandsSent_.load();

        for (uint64_t i = 1; i <= 10; ++i) {
            zones.addPlayerToZone(5, i, nullptr);
        }

        REQUIRE(internal.commandsSent_.load() == sentBefore + 10);

        drainAllCallbacks(internal);
        REQUIRE(internal.commandsCompleted_.load() >= 10);
    }

    SECTION("remove all players from zone") {
        // Add 5 players
        for (uint64_t i = 1; i <= 5; ++i) {
            zones.addPlayerToZone(5, i, nullptr);
        }
        drainAllCallbacks(internal);

        uint64_t completedBefore = internal.commandsCompleted_.load();

        // Remove all 5
        for (uint64_t i = 1; i <= 5; ++i) {
            zones.removePlayerFromZone(5, i, nullptr);
        }
        drainAllCallbacks(internal);

        REQUIRE(internal.commandsCompleted_.load() == completedBefore + 5);
    }
}

// ============================================================================
// Redis-dependent tests (require both hiredis library AND running server)
// ============================================================================

#ifdef REDIS_AVAILABLE

static bool isRedisServerRunning() {
    static bool checked = false;
    static bool available = false;

    if (!checked) {
        RedisManager testRedis;
        available = testRedis.initialize("localhost", 6379);
        if (available) testRedis.shutdown();
        checked = true;
    }

    return available;
}

TEST_CASE("ZoneManager live Redis integration", "[foundation][database][!mayfail]") {
    if (!isRedisServerRunning()) {
        SKIP("Redis server not running - skipping integration tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    RedisInternal& internal = redis.getInternal();

    ZoneManager zones(redis, internal);

    SECTION("addPlayerToZone with live Redis") {
        bool callbackFired = false;
        bool success = false;

        zones.addPlayerToZone(100, 12345, [&](bool result) {
            callbackFired = true;
            success = result;
        });

        redis.update();
        REQUIRE(callbackFired);
        REQUIRE(success);
    }

    SECTION("removePlayerFromZone with live Redis") {
        // First add
        bool addFired = false;
        zones.addPlayerToZone(100, 12346, [&](bool) { addFired = true; });
        redis.update();
        REQUIRE(addFired);

        // Then remove
        bool removeFired = false;
        bool removeSuccess = false;
        zones.removePlayerFromZone(100, 12346, [&](bool result) {
            removeFired = true;
            removeSuccess = result;
        });
        redis.update();
        REQUIRE(removeFired);
        REQUIRE(removeSuccess);
    }

    SECTION("getZonePlayers with live Redis") {
        // Add a player first
        bool addFired = false;
        zones.addPlayerToZone(200, 99999, [&](bool) { addFired = true; });
        redis.update();
        REQUIRE(addFired);

        // Get zone players
        bool readFired = false;
        std::vector<uint64_t> players;
        zones.getZonePlayers(200, [&](const AsyncResult<std::vector<uint64_t>>& result) {
            readFired = true;
            if (result.success) {
                players = result.value;
            }
        });
        redis.update();
        REQUIRE(readFired);
        REQUIRE_FALSE(players.empty());
        // Player 99999 should be in the set
        bool found = false;
        for (auto id : players) {
            if (id == 99999) found = true;
        }
        REQUIRE(found);
    }

    redis.shutdown();
}

#endif // REDIS_AVAILABLE
