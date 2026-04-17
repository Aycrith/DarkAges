// StreamManager unit tests - covers Redis Streams XADD/XREAD operations
// Tests stub behavior when Redis is not available

#include <catch2/catch_test_macros.hpp>
#include "db/RedisManager.hpp"
#include "db/StreamManager.hpp"
#include "db/RedisInternal.hpp"
#include <atomic>
#include <vector>
#include <string>

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

// ============================================================================
// Stub / Disconnected Tests
// ============================================================================

TEST_CASE("StreamManager xadd fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    // Don't initialize - internal_.connected is false by default
    StreamManager sm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackSuccess = true;
    std::string callbackError;

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    sm.xadd("test-stream", "*", fields, [&](const AsyncResult<std::string>& result) {
        callbackCalled = true;
        callbackSuccess = result.success;
        callbackError = result.error;
    });

    // Process callback queue
    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE_FALSE(callbackSuccess);
    REQUIRE(callbackError == "Not connected to Redis");
}

TEST_CASE("StreamManager xread fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    StreamManager sm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackSuccess = true;
    std::string callbackError;

    sm.xread("test-stream", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
        callbackCalled = true;
        callbackSuccess = result.success;
        callbackError = result.error;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE_FALSE(callbackSuccess);
    REQUIRE(callbackError == "Not connected to Redis");
}

TEST_CASE("StreamManager xadd with null callback when disconnected", "[foundation][database]") {
    RedisManager redis;
    StreamManager sm(redis, redis.getInternal());

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};

    // Should not crash with null callback
    REQUIRE_NOTHROW(sm.xadd("test-stream", "*", fields));
}

TEST_CASE("StreamManager xread with null callback returns immediately", "[foundation][database]") {
    RedisManager redis;
    StreamManager sm(redis, redis.getInternal());

    // xread with null callback should return immediately without crashing
    REQUIRE_NOTHROW(sm.xread("test-stream", "0-0", nullptr));
}

TEST_CASE("StreamManager xadd disconnected increments commandsSent", "[foundation][database]") {
    RedisManager redis;
    StreamManager sm(redis, redis.getInternal());

    auto& internal = redis.getInternal();
    auto initialSent = internal.commandsSent_.load();

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    sm.xadd("test-stream", "*", fields);

    REQUIRE(internal.commandsSent_.load() == initialSent + 1);
}

TEST_CASE("StreamManager xadd disconnected increments commandsFailed", "[foundation][database]") {
    RedisManager redis;
    StreamManager sm(redis, redis.getInternal());

    auto& internal = redis.getInternal();
    auto initialFailed = internal.commandsFailed_.load();

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    sm.xadd("test-stream", "*", fields);

    REQUIRE(internal.commandsFailed_.load() == initialFailed + 1);
}

TEST_CASE("StreamManager xread disconnected increments commandsFailed", "[foundation][database]") {
    RedisManager redis;
    StreamManager sm(redis, redis.getInternal());

    auto& internal = redis.getInternal();
    auto initialFailed = internal.commandsFailed_.load();

    sm.xread("test-stream", "0-0", [](const AsyncResult<std::vector<StreamEntry>>&) {});

    REQUIRE(internal.commandsFailed_.load() == initialFailed + 1);
}

TEST_CASE("StreamManager disconnected metrics tracking", "[foundation][database]") {
    RedisManager redis;
    StreamManager sm(redis, redis.getInternal());

    auto& internal = redis.getInternal();

    // Multiple operations should all increment counters
    std::unordered_map<std::string, std::string> fields = {{"k", "v"}};
    sm.xadd("stream1", "*", fields);
    sm.xadd("stream2", "*", fields);
    sm.xread("stream1", "0-0", [](const AsyncResult<std::vector<StreamEntry>>&) {});

    REQUIRE(internal.commandsSent_.load() == 3);
    REQUIRE(internal.commandsFailed_.load() == 3);
}

// ============================================================================
// Connected Tests (require live Redis)
// ============================================================================

TEST_CASE("StreamManager xadd succeeds when connected", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");

    RedisManager redis;
    redis.initialize();
    StreamManager sm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackSuccess = false;
    std::string streamId;

    std::unordered_map<std::string, std::string> fields = {{"event", "test"}, {"value", "42"}};
    sm.xadd("test:stream:unit", "*", fields, [&](const AsyncResult<std::string>& result) {
        callbackCalled = true;
        callbackSuccess = result.success;
        streamId = result.value;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE(callbackSuccess);
    REQUIRE_FALSE(streamId.empty());
    // Stream IDs have format "timestamp-sequence"
    REQUIRE(streamId.find('-') != std::string::npos);

    redis.shutdown();
}

TEST_CASE("StreamManager xadd with explicit ID when connected", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");

    RedisManager redis;
    redis.initialize();
    StreamManager sm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackSuccess = false;

    std::unordered_map<std::string, std::string> fields = {{"data", "hello"}};
    sm.xadd("test:stream:explicit", "0-1", fields, [&](const AsyncResult<std::string>& result) {
        callbackCalled = true;
        callbackSuccess = result.success;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE(callbackSuccess);

    redis.shutdown();
}

TEST_CASE("StreamManager xread returns entries when connected", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");

    RedisManager redis;
    redis.initialize();
    StreamManager sm(redis, redis.getInternal());

    // First, add an entry
    std::atomic<bool> addDone{false};
    std::unordered_map<std::string, std::string> fields = {{"read_test", "yes"}};
    sm.xadd("test:stream:read", "*", fields, [&](const AsyncResult<std::string>&) {
        addDone = true;
    });
    redis.update();
    REQUIRE(addDone);

    // Now read from the beginning
    std::atomic<bool> readDone{false};
    bool readSuccess = false;
    std::vector<StreamEntry> entries;

    sm.xread("test:stream:read", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
        readDone = true;
        readSuccess = result.success;
        entries = result.value;
    });

    redis.update();

    REQUIRE(readDone);
    REQUIRE(readSuccess);
    REQUIRE_FALSE(entries.empty());

    redis.shutdown();
}

TEST_CASE("StreamManager xadd with multiple fields", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");

    RedisManager redis;
    redis.initialize();
    StreamManager sm(redis, redis.getInternal());

    std::atomic<bool> callbackCalled{false};
    bool callbackSuccess = false;

    std::unordered_map<std::string, std::string> fields = {
        {"player_id", "1234"},
        {"action", "attack"},
        {"target", "5678"},
        {"damage", "150"}
    };
    sm.xadd("test:stream:multi", "*", fields, [&](const AsyncResult<std::string>& result) {
        callbackCalled = true;
        callbackSuccess = result.success;
    });

    redis.update();

    REQUIRE(callbackCalled);
    REQUIRE(callbackSuccess);

    redis.shutdown();
}

TEST_CASE("StreamManager xadd null callback when connected", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");

    RedisManager redis;
    redis.initialize();
    StreamManager sm(redis, redis.getInternal());

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};

    // Should not crash with null callback even when connected
    REQUIRE_NOTHROW(sm.xadd("test:stream:null", "*", fields));

    // Give it a tick to process
    redis.update();

    redis.shutdown();
}

TEST_CASE("StreamManager connected increments commandsCompleted", "[foundation][database]") {
    if (!isRedisAvailable()) SKIP("Redis not available");

    RedisManager redis;
    redis.initialize();
    StreamManager sm(redis, redis.getInternal());

    auto& internal = redis.getInternal();
    auto initialCompleted = internal.commandsCompleted_.load();

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    sm.xadd("test:stream:metrics", "*", fields, [](const AsyncResult<std::string>&) {});

    redis.update();

    REQUIRE(internal.commandsCompleted_.load() == initialCompleted + 1);

    redis.shutdown();
}
