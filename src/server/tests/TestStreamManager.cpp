// StreamManager unit tests
// Tests: xadd/xread stub behavior, callback mechanics, command metrics,
//        validation (empty fields), disconnected state handling

#include <catch2/catch_test_macros.hpp>
#include "db/StreamManager.hpp"
#include "db/RedisManager.hpp"
#include "db/RedisInternal.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

using namespace DarkAges;

// =========================================================================
// Helper: create a StreamManager with controlled RedisInternal state
// =========================================================================
struct StreamTestContext {
    RedisManager redis;  // Provides valid internal_ via stub constructor
    StreamManager streams;

    StreamTestContext() 
        : redis()
        , streams(redis, redis.getInternal()) {}

    RedisInternal& internal() { return redis.getInternal(); }
};

// =========================================================================
// Test: xadd when not connected fires error callback
// =========================================================================
TEST_CASE("StreamManager xadd disconnected fires error callback", "[foundation][database]") {
    StreamTestContext ctx;
    
    // Stub default: connected = false
    REQUIRE_FALSE(ctx.internal().connected);

    std::atomic<bool> callbackFired{false};
    bool callbackSuccess = true;
    std::string callbackError;

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    ctx.streams.xadd("test-stream", "*", fields, [&](const AsyncResult<std::string>& result) {
        callbackFired = true;
        callbackSuccess = result.success;
        callbackError = result.error;
    });

    // Process callbacks from queue
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(ctx.internal().callbackMutex);
        if (!ctx.internal().callbackQueue.empty()) {
            cb = ctx.internal().callbackQueue.front().func;
            ctx.internal().callbackQueue.pop();
        }
    }
    if (cb) cb();

    REQUIRE(callbackFired);
    REQUIRE_FALSE(callbackSuccess);
    REQUIRE(callbackError == "Not connected to Redis");
}

// =========================================================================
// Test: xread when not connected fires error callback
// =========================================================================
TEST_CASE("StreamManager xread disconnected fires error callback", "[foundation][database]") {
    StreamTestContext ctx;
    REQUIRE_FALSE(ctx.internal().connected);

    std::atomic<bool> callbackFired{false};
    bool callbackSuccess = true;
    std::string callbackError;

    ctx.streams.xread("test-stream", "0-0", [&](const AsyncResult<std::vector<StreamEntry>>& result) {
        callbackFired = true;
        callbackSuccess = result.success;
        callbackError = result.error;
    });

    // Process callbacks
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(ctx.internal().callbackMutex);
        if (!ctx.internal().callbackQueue.empty()) {
            cb = ctx.internal().callbackQueue.front().func;
            ctx.internal().callbackQueue.pop();
        }
    }
    if (cb) cb();

    REQUIRE(callbackFired);
    REQUIRE_FALSE(callbackSuccess);
    REQUIRE(callbackError == "Not connected to Redis");
}

// =========================================================================
// Test: xadd with empty fields fires error callback when connected
// =========================================================================
TEST_CASE("StreamManager xadd empty fields validation", "[foundation][database]") {
    StreamTestContext ctx;
    ctx.internal().connected = true;

    std::atomic<bool> callbackFired{false};
    bool callbackSuccess = true;
    std::string callbackError;

    std::unordered_map<std::string, std::string> emptyFields;
    ctx.streams.xadd("test-stream", "*", emptyFields, [&](const AsyncResult<std::string>& result) {
        callbackFired = true;
        callbackSuccess = result.success;
        callbackError = result.error;
    });

    // Process callbacks
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(ctx.internal().callbackMutex);
        if (!ctx.internal().callbackQueue.empty()) {
            cb = ctx.internal().callbackQueue.front().func;
            ctx.internal().callbackQueue.pop();
        }
    }
    if (cb) cb();

    REQUIRE(callbackFired);
    REQUIRE_FALSE(callbackSuccess);
    REQUIRE(callbackError == "At least one field-value pair required");
}

// =========================================================================
// Test: xadd with fields when connected (stub mode returns success)
// =========================================================================
TEST_CASE("StreamManager xadd connected stub returns success", "[foundation][database]") {
    StreamTestContext ctx;
    ctx.internal().connected = true;

    std::atomic<bool> callbackFired{false};
    bool callbackSuccess = false;
    std::string callbackId;

    std::unordered_map<std::string, std::string> fields = {{"event", "damage"}, {"amount", "50"}};
    ctx.streams.xadd("combat-log", "*", fields, [&](const AsyncResult<std::string>& result) {
        callbackFired = true;
        callbackSuccess = result.success;
        callbackId = result.value;
    });

    // Process callbacks
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(ctx.internal().callbackMutex);
        if (!ctx.internal().callbackQueue.empty()) {
            cb = ctx.internal().callbackQueue.front().func;
            ctx.internal().callbackQueue.pop();
        }
    }
    if (cb) cb();

    REQUIRE(callbackFired);
    REQUIRE(callbackSuccess);
    REQUIRE(callbackId == "0-0");  // Stub returns fake ID
}

// =========================================================================
// Test: xadd without callback doesn't crash
// =========================================================================
TEST_CASE("StreamManager xadd no callback is safe", "[foundation][database]") {
    StreamTestContext ctx;
    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    REQUIRE_NOTHROW(ctx.streams.xadd("test-stream", "*", fields, nullptr));
}

// =========================================================================
// Test: xread without callback is safe
// =========================================================================
TEST_CASE("StreamManager xread no callback is safe", "[foundation][database]") {
    StreamTestContext ctx;
    REQUIRE_NOTHROW(ctx.streams.xread("test-stream", "0-0", nullptr));
}

// =========================================================================
// Test: command metrics track xadd operations when disconnected
// =========================================================================
TEST_CASE("StreamManager xadd updates command metrics", "[foundation][database]") {
    StreamTestContext ctx;
    // connected = false by default

    uint64_t sentBefore = ctx.internal().commandsSent_.load();
    uint64_t failedBefore = ctx.internal().commandsFailed_.load();

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    ctx.streams.xadd("test-stream", "*", fields, nullptr);

    // When disconnected, xadd increments commandsSent_ and commandsFailed_
    REQUIRE(ctx.internal().commandsSent_.load() > sentBefore);
    REQUIRE(ctx.internal().commandsFailed_.load() > failedBefore);
}

// =========================================================================
// Test: command metrics track xread operations when disconnected
// =========================================================================
TEST_CASE("StreamManager xread updates command metrics", "[foundation][database]") {
    StreamTestContext ctx;

    uint64_t sentBefore = ctx.internal().commandsSent_.load();
    uint64_t failedBefore = ctx.internal().commandsFailed_.load();

    ctx.streams.xread("test-stream", "0-0", [](const auto&) {});

    REQUIRE(ctx.internal().commandsSent_.load() > sentBefore);
    REQUIRE(ctx.internal().commandsFailed_.load() > failedBefore);
}

// =========================================================================
// Test: xadd connected increments completed (not failed)
// =========================================================================
TEST_CASE("StreamManager xadd connected increments completed", "[foundation][database]") {
    StreamTestContext ctx;
    ctx.internal().connected = true;

    uint64_t completedBefore = ctx.internal().commandsCompleted_.load();

    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};
    ctx.streams.xadd("test-stream", "*", fields, nullptr);

    REQUIRE(ctx.internal().commandsCompleted_.load() > completedBefore);
}

// =========================================================================
// Test: multiple xadd operations accumulate metrics
// =========================================================================
TEST_CASE("StreamManager multiple operations accumulate metrics", "[foundation][database]") {
    StreamTestContext ctx;
    // disconnected - each xadd increments sent and failed

    uint64_t sentBefore = ctx.internal().commandsSent_.load();
    std::unordered_map<std::string, std::string> fields = {{"key", "value"}};

    for (int i = 0; i < 5; i++) {
        ctx.streams.xadd("test-stream", "*", fields, nullptr);
    }

    REQUIRE(ctx.internal().commandsSent_.load() >= sentBefore + 5);
}

// =========================================================================
// Test: StreamEntry type is accessible and functional
// =========================================================================
TEST_CASE("StreamEntry struct is properly defined", "[foundation][database]") {
    StreamEntry entry;
    entry.id = "1234567890-0";
    entry.fields["key1"] = "value1";
    entry.fields["key2"] = "value2";

    REQUIRE(entry.id == "1234567890-0");
    REQUIRE(entry.fields.size() == 2);
    REQUIRE(entry.fields.at("key1") == "value1");
    REQUIRE(entry.fields.at("key2") == "value2");
}
