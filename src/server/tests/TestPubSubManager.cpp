// PubSubManager unit tests - covers ZoneMessage serialization and ZoneMessageType enum
// ZoneMessage::serialize/deserialize are always compiled (no Redis dependency)
// PubSubManager class requires REDIS_AVAILABLE; tested via RedisManager stub pattern

#include <catch2/catch_test_macros.hpp>
#include "db/PubSubManager.hpp"
#include <cstring>

using namespace DarkAges;

// ============================================================================
// ZoneMessageType enum value tests
// ============================================================================

TEST_CASE("ZoneMessageType has correct enum values", "[foundation][database]") {
    REQUIRE(static_cast<uint8_t>(ZoneMessageType::ENTITY_SYNC) == 1);
    REQUIRE(static_cast<uint8_t>(ZoneMessageType::MIGRATION_REQUEST) == 2);
    REQUIRE(static_cast<uint8_t>(ZoneMessageType::MIGRATION_STATE) == 3);
    REQUIRE(static_cast<uint8_t>(ZoneMessageType::MIGRATION_COMPLETE) == 4);
    REQUIRE(static_cast<uint8_t>(ZoneMessageType::BROADCAST) == 5);
    REQUIRE(static_cast<uint8_t>(ZoneMessageType::CHAT) == 6);
    REQUIRE(static_cast<uint8_t>(ZoneMessageType::ZONE_STATUS) == 7);
}

// ============================================================================
// ZoneMessage serialization tests
// ============================================================================

TEST_CASE("ZoneMessage serialize produces correct header size", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::ENTITY_SYNC;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 2;
    msg.timestamp = 1000;
    msg.sequence = 42;
    msg.payload = {0xAA, 0xBB, 0xCC};

    auto data = msg.serialize();

    // Header: type(1) + source(4) + target(4) + timestamp(4) + sequence(4) + payload_size(4) = 21
    // Plus payload: 3 bytes
    REQUIRE(data.size() == 24);
}

TEST_CASE("ZoneMessage serialize with empty payload", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::BROADCAST;
    msg.sourceZoneId = 10;
    msg.targetZoneId = 0;
    msg.timestamp = 5000;
    msg.sequence = 1;
    msg.payload = {};

    auto data = msg.serialize();

    // Header only: 21 bytes
    REQUIRE(data.size() == 21);
}

TEST_CASE("ZoneMessage serialize type byte is correct", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::CHAT;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 2;
    msg.timestamp = 0;
    msg.sequence = 0;
    msg.payload = {};

    auto data = msg.serialize();

    REQUIRE(data[0] == static_cast<uint8_t>(ZoneMessageType::CHAT));
}

TEST_CASE("ZoneMessage round-trip with various types", "[foundation][database]") {
    // Test all message types survive serialization round-trip
    auto testRoundTrip = [](ZoneMessageType type) {
        ZoneMessage msg;
        msg.type = type;
        msg.sourceZoneId = 42;
        msg.targetZoneId = 99;
        msg.timestamp = 1234567890;
        msg.sequence = 777;
        msg.payload = {0x01, 0x02, 0x03, 0x04, 0x05};

        auto data = msg.serialize();
        auto result = ZoneMessage::deserialize(data);

        REQUIRE(result.has_value());
        REQUIRE(result->type == type);
        REQUIRE(result->sourceZoneId == 42);
        REQUIRE(result->targetZoneId == 99);
        REQUIRE(result->timestamp == 1234567890);
        REQUIRE(result->sequence == 777);
        REQUIRE(result->payload == msg.payload);
    };

    SECTION("ENTITY_SYNC") { testRoundTrip(ZoneMessageType::ENTITY_SYNC); }
    SECTION("MIGRATION_REQUEST") { testRoundTrip(ZoneMessageType::MIGRATION_REQUEST); }
    SECTION("MIGRATION_STATE") { testRoundTrip(ZoneMessageType::MIGRATION_STATE); }
    SECTION("MIGRATION_COMPLETE") { testRoundTrip(ZoneMessageType::MIGRATION_COMPLETE); }
    SECTION("BROADCAST") { testRoundTrip(ZoneMessageType::BROADCAST); }
    SECTION("CHAT") { testRoundTrip(ZoneMessageType::CHAT); }
    SECTION("ZONE_STATUS") { testRoundTrip(ZoneMessageType::ZONE_STATUS); }
}

TEST_CASE("ZoneMessage round-trip with broadcast target", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::BROADCAST;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 0;  // 0 = broadcast to all
    msg.timestamp = 100;
    msg.sequence = 1;
    msg.payload = {0xFF};

    auto data = msg.serialize();
    auto result = ZoneMessage::deserialize(data);

    REQUIRE(result.has_value());
    REQUIRE(result->targetZoneId == 0);
    REQUIRE(result->type == ZoneMessageType::BROADCAST);
}

TEST_CASE("ZoneMessage round-trip with large payload", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::ENTITY_SYNC;
    msg.sourceZoneId = 5;
    msg.targetZoneId = 6;
    msg.timestamp = 9999;
    msg.sequence = 42;

    // Create a payload of 1000 bytes
    msg.payload.resize(1000);
    for (size_t i = 0; i < msg.payload.size(); ++i) {
        msg.payload[i] = static_cast<uint8_t>(i % 256);
    }

    auto data = msg.serialize();
    auto result = ZoneMessage::deserialize(data);

    REQUIRE(result.has_value());
    REQUIRE(result->payload.size() == 1000);
    REQUIRE(result->payload == msg.payload);
}

TEST_CASE("ZoneMessage round-trip with zero values", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::ENTITY_SYNC;
    msg.sourceZoneId = 0;
    msg.targetZoneId = 0;
    msg.timestamp = 0;
    msg.sequence = 0;
    msg.payload = {};

    auto data = msg.serialize();
    auto result = ZoneMessage::deserialize(data);

    REQUIRE(result.has_value());
    REQUIRE(result->sourceZoneId == 0);
    REQUIRE(result->targetZoneId == 0);
    REQUIRE(result->timestamp == 0);
    REQUIRE(result->sequence == 0);
    REQUIRE(result->payload.empty());
}

TEST_CASE("ZoneMessage round-trip with max uint32 values", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::ZONE_STATUS;
    msg.sourceZoneId = 0xFFFFFFFF;
    msg.targetZoneId = 0xFFFFFFFF;
    msg.timestamp = 0xFFFFFFFF;
    msg.sequence = 0xFFFFFFFF;
    msg.payload = {0x42};

    auto data = msg.serialize();
    auto result = ZoneMessage::deserialize(data);

    REQUIRE(result.has_value());
    REQUIRE(result->sourceZoneId == 0xFFFFFFFF);
    REQUIRE(result->targetZoneId == 0xFFFFFFFF);
    REQUIRE(result->timestamp == 0xFFFFFFFF);
    REQUIRE(result->sequence == 0xFFFFFFFF);
    REQUIRE(result->payload.size() == 1);
}

// ============================================================================
// ZoneMessage deserialization edge cases
// ============================================================================

TEST_CASE("ZoneMessage deserialize returns nullopt for too-small data", "[foundation][database]") {
    // Minimum header is 21 bytes
    std::vector<uint8_t> tooSmall(20, 0x00);
    auto result = ZoneMessage::deserialize(tooSmall);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ZoneMessage deserialize returns nullopt for empty data", "[foundation][database]") {
    std::vector<uint8_t> empty;
    auto result = ZoneMessage::deserialize(empty);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ZoneMessage deserialize returns nullopt for truncated payload", "[foundation][database]") {
    // Create a valid message with payload
    ZoneMessage msg;
    msg.type = ZoneMessageType::CHAT;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 2;
    msg.timestamp = 100;
    msg.sequence = 1;
    msg.payload = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto data = msg.serialize();

    // Truncate the data (remove last 3 bytes from payload)
    data.resize(data.size() - 3);

    auto result = ZoneMessage::deserialize(data);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ZoneMessage deserialize with single byte payload", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::MIGRATION_COMPLETE;
    msg.sourceZoneId = 100;
    msg.targetZoneId = 200;
    msg.timestamp = 5555;
    msg.sequence = 10;
    msg.payload = {0x42};

    auto data = msg.serialize();
    auto result = ZoneMessage::deserialize(data);

    REQUIRE(result.has_value());
    REQUIRE(result->payload.size() == 1);
    REQUIRE(result->payload[0] == 0x42);
}

TEST_CASE("ZoneMessage deserialize header-only message", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::ZONE_STATUS;
    msg.sourceZoneId = 3;
    msg.targetZoneId = 4;
    msg.timestamp = 777;
    msg.sequence = 88;
    msg.payload = {};

    auto data = msg.serialize();
    REQUIRE(data.size() == 21);  // Exactly header size

    auto result = ZoneMessage::deserialize(data);
    REQUIRE(result.has_value());
    REQUIRE(result->type == ZoneMessageType::ZONE_STATUS);
    REQUIRE(result->sourceZoneId == 3);
    REQUIRE(result->targetZoneId == 4);
    REQUIRE(result->timestamp == 777);
    REQUIRE(result->sequence == 88);
    REQUIRE(result->payload.empty());
}

// ============================================================================
// PubSubManager stub behavior (when Redis is not available)
// ============================================================================

#ifdef REDIS_AVAILABLE

#include "db/RedisManager.hpp"
#include "db/RedisInternal.hpp"

static bool isRedisAvailable() {
    static bool checked = false;
    static bool available = false;
    if (!checked) {
        checked = true;
        DarkAges::RedisManager testRedis;
        available = testRedis.initialize("localhost", 6379);
        if (available) {
            testRedis.shutdown();
        }
    }
    return available;
}

TEST_CASE("PubSubManager publish fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    PubSubManager psm(redis, redis.getInternal());

    // Should not crash or throw when disconnected
    psm.publish("test-channel", "test-message");
    // No assertion needed - just verifying no crash
    SUCCEED();
}

TEST_CASE("PubSubManager subscribe fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    PubSubManager psm(redis, redis.getInternal());

    bool callbackCalled = false;
    psm.subscribe("test-channel", [&](std::string_view, std::string_view) {
        callbackCalled = true;
    });

    REQUIRE_FALSE(callbackCalled);
}

TEST_CASE("PubSubManager unsubscribe fails when disconnected", "[foundation][database]") {
    RedisManager redis;
    PubSubManager psm(redis, redis.getInternal());

    // Should not crash or throw when disconnected
    psm.unsubscribe("test-channel");
    SUCCEED();
}

TEST_CASE("PubSubManager processSubscriptions is no-op when disconnected", "[foundation][database]") {
    RedisManager redis;
    PubSubManager psm(redis, redis.getInternal());

    psm.processSubscriptions();
    SUCCEED();
}

TEST_CASE("PubSubManager publishToZone with ZoneMessage", "[foundation][database]") {
    RedisManager redis;
    PubSubManager psm(redis, redis.getInternal());

    ZoneMessage msg;
    msg.type = ZoneMessageType::BROADCAST;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 0;
    msg.timestamp = 1000;
    msg.sequence = 1;
    msg.payload = {0x01, 0x02};

    // Should not crash when disconnected
    psm.publishToZone(1, msg);
    SUCCEED();
}

TEST_CASE("PubSubManager broadcastToAllZones with ZoneMessage", "[foundation][database]") {
    RedisManager redis;
    PubSubManager psm(redis, redis.getInternal());

    ZoneMessage msg;
    msg.type = ZoneMessageType::CHAT;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 0;
    msg.timestamp = 2000;
    msg.sequence = 5;
    msg.payload = {0x41, 0x42, 0x43};  // "ABC"

    // Should not crash when disconnected
    psm.broadcastToAllZones(msg);
    SUCCEED();
}

TEST_CASE("PubSubManager subscribeToZoneChannel when disconnected", "[foundation][database]") {
    RedisManager redis;
    PubSubManager psm(redis, redis.getInternal());

    bool callbackCalled = false;
    psm.subscribeToZoneChannel(1, [&](const ZoneMessage&) {
        callbackCalled = true;
    });

    REQUIRE_FALSE(callbackCalled);
}

TEST_CASE("PubSubManager connected publish increments commands", "[foundation][database][!mayfail]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available at localhost:6379");
    }

    RedisManager redis;
    redis.initialize("localhost", 6379);
    PubSubManager psm(redis, redis.getInternal());

    auto& internal = redis.getInternal();
    auto initialSent = internal.commandsSent_.load();

    psm.publish("test-pubsub-channel", "hello");

    REQUIRE(internal.commandsSent_.load() > initialSent);
    redis.shutdown();
}

#endif // REDIS_AVAILABLE
