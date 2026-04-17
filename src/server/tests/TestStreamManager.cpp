// StreamManager unit tests
// Tests: XADD/XREAD stream operations, message queuing,
//        disconnected error handling, and stub fallback behavior

#include <catch2/catch_test_macros.hpp>
#include "db/StreamManager.hpp"
#include "db/RedisManager.hpp"
#include "db/RedisInternal.hpp"

#ifdef REDIS_AVAILABLE
#include <hiredis.h>
#endif

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace DarkAges;

// ============================================================================
// Unconditional tests (compile in both REDIS_AVAILABLE and stub modes)
// ============================================================================

TEST_CASE("StreamEntry struct construction", "[foundation][database]") {
    SECTION("Default construction") {
        StreamEntry entry;
        REQUIRE(entry.id.empty());
        REQUIRE(entry.fields.empty());
    }

    SECTION("Field manipulation") {
        StreamEntry entry;
        entry.id = "1234567890-0";
        entry.fields["event"] = "combat";
        entry.fields["damage"] = "100";
        entry.fields["source"] = "player_1";

        REQUIRE(entry.id == "1234567890-0");
        REQUIRE(entry.fields.size() == 3);
        REQUIRE(entry.fields.at("event") == "combat");
        REQUIRE(entry.fields.at("damage") == "100");
        REQUIRE(entry.fields.at("source") == "player_1");
    }

    SECTION("Field overwrite") {
        StreamEntry entry;
        entry.fields["key"] = "original";
        entry.fields["key"] = "updated";

        REQUIRE(entry.fields.size() == 1);
        REQUIRE(entry.fields.at("key") == "updated");
    }

    SECTION("Move semantics") {
        StreamEntry entry;
        entry.id = "test-id";
        entry.fields["a"] = "1";
        entry.fields["b"] = "2";

        StreamEntry moved = std::move(entry);
        REQUIRE(moved.id == "test-id");
        REQUIRE(moved.fields.size() == 2);
    }

    SECTION("Copy semantics") {
        StreamEntry entry;
        entry.id = "copy-id";
        entry.fields["x"] = "10";

        StreamEntry copied = entry;
        REQUIRE(copied.id == "copy-id");
        REQUIRE(copied.fields.at("x") == "10");
        // Original should be unchanged
        REQUIRE(entry.id == "copy-id");
    }
}

TEST_CASE("StreamManager callback types", "[foundation][database]") {
    SECTION("StreamAddCallback receives error result") {
        bool fired = false;
        StreamManager::StreamAddCallback cb = [&](const AsyncResult<std::string>& result) {
            fired = true;
            REQUIRE_FALSE(result.success);
            REQUIRE(result.error == "test error");
        };

        AsyncResult<std::string> result;
        result.success = false;
        result.error = "test error";
        cb(result);

        REQUIRE(fired);
    }

    SECTION("StreamAddCallback receives success result") {
        bool fired = false;
        StreamManager::StreamAddCallback cb = [&](const AsyncResult<std::string>& result) {
            fired = true;
            REQUIRE(result.success);
            REQUIRE(result.value == "1234567890-0");
            REQUIRE(result.error.empty());
        };

        AsyncResult<std::string> result;
        result.success = true;
        result.value = "1234567890-0";
        cb(result);

        REQUIRE(fired);
    }

    SECTION("StreamReadCallback receives entries") {
        bool fired = false;
        StreamManager::StreamReadCallback cb = [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            fired = true;
            REQUIRE(result.success);
            REQUIRE(result.value.size() == 2);
            REQUIRE(result.value[0].id == "id1");
            REQUIRE(result.value[1].id == "id2");
        };

        AsyncResult<std::vector<StreamEntry>> result;
        result.success = true;
        result.value.push_back({"id1", {{"f", "v1"}}});
        result.value.push_back({"id2", {{"f", "v2"}}});
        cb(result);

        REQUIRE(fired);
    }

    SECTION("StreamReadCallback receives empty result") {
        bool fired = false;
        StreamManager::StreamReadCallback cb = [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            fired = true;
            REQUIRE(result.success);
            REQUIRE(result.value.empty());
        };

        AsyncResult<std::vector<StreamEntry>> result;
        result.success = true;
        cb(result);

        REQUIRE(fired);
    }

    SECTION("Null callbacks are falsy") {
        StreamManager::StreamAddCallback nullAdd = nullptr;
        REQUIRE_FALSE(nullAdd);

        StreamManager::StreamReadCallback nullRead = nullptr;
        REQUIRE_FALSE(nullRead);
    }
}

TEST_CASE("AsyncResult template types", "[foundation][database]") {
    SECTION("AsyncResult<string> default") {
        AsyncResult<std::string> result;
        REQUIRE_FALSE(result.success);
        REQUIRE(result.value.empty());
        REQUIRE(result.error.empty());
    }

    SECTION("AsyncResult<vector<StreamEntry>> default") {
        AsyncResult<std::vector<StreamEntry>> result;
        REQUIRE_FALSE(result.success);
        REQUIRE(result.value.empty());
        REQUIRE(result.error.empty());
    }

    SECTION("AsyncResult error propagation") {
        AsyncResult<std::string> result;
        result.success = false;
        result.error = "Connection refused";

        REQUIRE_FALSE(result.success);
        REQUIRE(result.error == "Connection refused");
    }
}

// ============================================================================
// Redis-dependent tests (require both hiredis library AND running server)
// ============================================================================

#ifdef REDIS_AVAILABLE

// Helper to check if a live Redis server is available
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

TEST_CASE("StreamManager XADD with live Redis", "[foundation][database][!mayfail]") {
    if (!isRedisServerRunning()) {
        SKIP("Redis server not running - skipping integration tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));

    SECTION("XADD adds entry and returns stream ID") {
        bool callbackFired = false;
        std::string returnedId;

        std::unordered_map<std::string, std::string> fields = {
            {"event", "player_join"},
            {"player_id", "12345"}
        };

        redis.xadd("test:stream:xadd", "*", fields, [&](const AsyncResult<std::string>& result) {
            callbackFired = true;
            returnedId = result.value;
        });

        redis.update();
        REQUIRE(callbackFired);
        REQUIRE_FALSE(returnedId.empty());
        // Stream ID format: <milliseconds>-<sequence>
        REQUIRE(returnedId.find('-') != std::string::npos);
    }

    SECTION("XADD with empty fields returns error") {
        bool callbackFired = false;
        bool success = true;

        redis.xadd("test:stream:empty", "*", {}, [&](const AsyncResult<std::string>& result) {
            callbackFired = true;
            success = result.success;
        });

        redis.update();
        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
    }

    SECTION("XADD with explicit ID") {
        bool callbackFired = false;
        std::string returnedId;

        std::unordered_map<std::string, std::string> fields = {
            {"data", "test_value"}
        };

        redis.xadd("test:stream:explicit", "0-1", fields, [&](const AsyncResult<std::string>& result) {
            callbackFired = true;
            returnedId = result.value;
        });

        redis.update();
        REQUIRE(callbackFired);
        REQUIRE(returnedId == "0-1");
    }

    SECTION("XADD without callback does not crash") {
        std::unordered_map<std::string, std::string> fields = {{"k", "v"}};
        redis.xadd("test:stream:nocb", "*", fields, nullptr);
        redis.update();
    }

    redis.shutdown();
}

TEST_CASE("StreamManager XREAD with live Redis", "[foundation][database][!mayfail]") {
    if (!isRedisServerRunning()) {
        SKIP("Redis server not running - skipping integration tests");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));

    SECTION("XREAD reads entries after XADD") {
        bool addFired = false;
        std::unordered_map<std::string, std::string> fields = {
            {"type", "damage"},
            {"amount", "150"}
        };

        redis.xadd("test:stream:readback", "*", fields, [&](const AsyncResult<std::string>&) {
            addFired = true;
        });

        redis.update();
        REQUIRE(addFired);

        bool readFired = false;
        std::vector<StreamEntry> entries;

        redis.xread("test:stream:readback", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            readFired = true;
            if (result.success) {
                entries = result.value;
            }
        });

        redis.update();
        REQUIRE(readFired);
        REQUIRE_FALSE(entries.empty());
        REQUIRE(entries.back().fields.count("type"));
        REQUIRE(entries.back().fields.at("type") == "damage");
    }

    SECTION("XREAD with count limits results") {
        for (int i = 0; i < 3; ++i) {
            bool fired = false;
            std::unordered_map<std::string, std::string> fields = {
                {"index", std::to_string(i)}
            };
            redis.xadd("test:stream:count", "*", fields, [&](const AsyncResult<std::string>&) {
                fired = true;
            });
            redis.update();
            REQUIRE(fired);
        }

        bool readFired = false;
        std::vector<StreamEntry> entries;
        redis.xread("test:stream:count", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            readFired = true;
            if (result.success) {
                entries = result.value;
            }
        }, 2);

        redis.update();
        REQUIRE(readFired);
        REQUIRE(entries.size() <= 2);
    }

    SECTION("XREAD from future ID returns empty") {
        bool readFired = false;
        std::vector<StreamEntry> entries;

        redis.xread("test:stream:future", "9999999999999-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            readFired = true;
            if (result.success) {
                entries = result.value;
            }
        });

        redis.update();
        REQUIRE(readFired);
        REQUIRE(entries.empty());
    }

    SECTION("XREAD with null callback returns immediately") {
        redis.xread("test:stream", "0-0", nullptr);
        // Should not crash
    }

    SECTION("Multiple field-value pairs preserved through XADD/XREAD") {
        bool addFired = false;
        std::unordered_map<std::string, std::string> fields = {
            {"source_entity", "42"},
            {"target_entity", "99"},
            {"damage", "250"},
            {"ability", "fireball"}
        };

        redis.xadd("test:stream:fields", "*", fields, [&](const AsyncResult<std::string>&) {
            addFired = true;
        });

        redis.update();
        REQUIRE(addFired);

        bool readFired = false;
        std::vector<StreamEntry> entries;
        redis.xread("test:stream:fields", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            readFired = true;
            if (result.success) entries = result.value;
        });

        redis.update();
        REQUIRE(readFired);
        REQUIRE_FALSE(entries.empty());

        auto& entry = entries.back();
        REQUIRE(entry.fields.size() == 4);
        REQUIRE(entry.fields.at("source_entity") == "42");
        REQUIRE(entry.fields.at("target_entity") == "99");
        REQUIRE(entry.fields.at("damage") == "250");
        REQUIRE(entry.fields.at("ability") == "fireball");
    }

    redis.shutdown();
}

TEST_CASE("StreamManager disconnected behavior (Redis available, server down)", "[foundation][database]") {
    RedisManager redis;
    // initialize fails because no Redis server is running
    bool connected = redis.initialize("localhost", 6379);

    if (connected) {
        // Server IS running - skip this test (covered by integration tests)
        redis.shutdown();
        SKIP("Redis server is running - disconnected tests not applicable");
    }

    SECTION("isConnected returns false when server is down") {
        REQUIRE_FALSE(redis.isConnected());
    }

    SECTION("XADD when disconnected fires error callback") {
        bool callbackFired = false;
        bool success = true;
        std::string error;

        std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
        redis.xadd("test:stream", "*", fields, [&](const AsyncResult<std::string>& result) {
            callbackFired = true;
            success = result.success;
            error = result.error;
        });

        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
        REQUIRE(error.find("Not connected") != std::string::npos);
    }

    SECTION("XADD with no callback does not crash when disconnected") {
        std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
        redis.xadd("test:stream", "*", fields, nullptr);
    }

    SECTION("XADD with empty fields when disconnected fails on connection check") {
        bool callbackFired = false;
        bool success = true;

        redis.xadd("test:stream", "*", {}, [&](const AsyncResult<std::string>& result) {
            callbackFired = true;
            success = result.success;
        });

        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
    }

    SECTION("XREAD when disconnected fires error callback") {
        bool callbackFired = false;
        bool success = true;
        std::string error;

        redis.xread("test:stream", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            callbackFired = true;
            success = result.success;
            error = result.error;
        });

        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
        REQUIRE(error.find("Not connected") != std::string::npos);
    }

    SECTION("XREAD with null callback returns immediately when disconnected") {
        redis.xread("test:stream", "0-0", nullptr);
    }

    SECTION("Multiple disconnected operations each fire error callbacks") {
        int callbackCount = 0;

        std::unordered_map<std::string, std::string> fields = {{"k", "v"}};

        redis.xadd("stream1", "*", fields, [&](const AsyncResult<std::string>&) {
            callbackCount++;
        });
        redis.xadd("stream2", "*", fields, [&](const AsyncResult<std::string>&) {
            callbackCount++;
        });
        redis.xread("stream3", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>&) {
            callbackCount++;
        });

        REQUIRE(callbackCount == 3);
    }
}

#else  // !REDIS_AVAILABLE - stub-only tests

TEST_CASE("StreamManager stub behavior", "[foundation][database]") {
    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    REQUIRE_FALSE(redis.isConnected());

    SECTION("Stub isConnected returns false") {
        REQUIRE_FALSE(redis.isConnected());
    }

    SECTION("Stub XADD disconnected fires error callback") {
        bool callbackFired = false;
        bool success = true;
        std::string error;

        std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
        redis.xadd("test:stream", "*", fields, [&](const AsyncResult<std::string>& result) {
            callbackFired = true;
            success = result.success;
            error = result.error;
        });

        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
        REQUIRE(error.find("Not connected") != std::string::npos);
    }

    SECTION("Stub XADD with no callback does not crash") {
        std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
        redis.xadd("test:stream", "*", fields, nullptr);
    }

    SECTION("Stub XREAD disconnected fires error callback") {
        bool callbackFired = false;
        bool success = true;
        std::string error;

        redis.xread("test:stream", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
            callbackFired = true;
            success = result.success;
            error = result.error;
        });

        REQUIRE(callbackFired);
        REQUIRE_FALSE(success);
        REQUIRE(error.find("Not connected") != std::string::npos);
    }

    SECTION("Stub XREAD with null callback returns immediately") {
        redis.xread("test:stream", "0-0", nullptr);
    }

    SECTION("Stub metrics are zero") {
        REQUIRE(redis.getCommandsSent() == 0);
        REQUIRE(redis.getCommandsCompleted() == 0);
        REQUIRE(redis.getCommandsFailed() == 0);
        REQUIRE(redis.getAverageLatencyMs() == 0.0f);
    }

    SECTION("Stub shutdown is safe") {
        redis.shutdown();
    }

    SECTION("Stub update is safe") {
        redis.update();
    }

    SECTION("Stub multiple disconnected operations fire error callbacks") {
        int callbackCount = 0;

        std::unordered_map<std::string, std::string> fields = {{"k", "v"}};

        redis.xadd("s1", "*", fields, [&](const AsyncResult<std::string>&) { callbackCount++; });
        redis.xadd("s2", "*", fields, [&](const AsyncResult<std::string>&) { callbackCount++; });
        redis.xread("s3", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>&) { callbackCount++; });

        REQUIRE(callbackCount == 3);
    }
}

#endif // REDIS_AVAILABLE
