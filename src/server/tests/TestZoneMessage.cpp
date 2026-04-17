// ZoneMessage unit tests
// Tests serialization/deserialization round-trip, edge cases, and validation

#include <catch2/catch_test_macros.hpp>
#include "db/PubSubManager.hpp"
#include <cstring>

using namespace DarkAges;

TEST_CASE("ZoneMessage serialization round-trip", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::ENTITY_SYNC;
    msg.sourceZoneId = 42;
    msg.targetZoneId = 100;
    msg.timestamp = 1234567890;
    msg.sequence = 1;
    msg.payload = {0x01, 0x02, 0x03, 0x04};
    
    auto serialized = msg.serialize();
    REQUIRE(serialized.size() == 21 + 4); // 21 byte header + 4 byte payload
    
    auto deserialized = ZoneMessage::deserialize(serialized);
    REQUIRE(deserialized.has_value());
    REQUIRE(deserialized->type == ZoneMessageType::ENTITY_SYNC);
    REQUIRE(deserialized->sourceZoneId == 42);
    REQUIRE(deserialized->targetZoneId == 100);
    REQUIRE(deserialized->timestamp == 1234567890);
    REQUIRE(deserialized->sequence == 1);
    REQUIRE(deserialized->payload == std::vector<uint8_t>({0x01, 0x02, 0x03, 0x04}));
}

TEST_CASE("ZoneMessage empty payload", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::MIGRATION_REQUEST;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 2;
    msg.timestamp = 100;
    msg.sequence = 5;
    msg.payload = {};
    
    auto serialized = msg.serialize();
    REQUIRE(serialized.size() == 21); // Just header
    
    auto deserialized = ZoneMessage::deserialize(serialized);
    REQUIRE(deserialized.has_value());
    REQUIRE(deserialized->type == ZoneMessageType::MIGRATION_REQUEST);
    REQUIRE(deserialized->sourceZoneId == 1);
    REQUIRE(deserialized->targetZoneId == 2);
    REQUIRE(deserialized->timestamp == 100);
    REQUIRE(deserialized->sequence == 5);
    REQUIRE(deserialized->payload.empty());
}

TEST_CASE("ZoneMessage broadcast (target 0)", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::BROADCAST;
    msg.sourceZoneId = 5;
    msg.targetZoneId = 0; // broadcast
    msg.timestamp = 999;
    msg.sequence = 0;
    msg.payload = {0xFF};
    
    auto serialized = msg.serialize();
    auto deserialized = ZoneMessage::deserialize(serialized);
    REQUIRE(deserialized.has_value());
    REQUIRE(deserialized->targetZoneId == 0);
}

TEST_CASE("ZoneMessage deserialize too short", "[foundation][database]") {
    std::vector<uint8_t> tooShort(20, 0); // 20 bytes, minimum is 21
    auto result = ZoneMessage::deserialize(tooShort);
    REQUIRE_FALSE(result.has_value());
    
    std::vector<uint8_t> empty;
    result = ZoneMessage::deserialize(empty);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ZoneMessage deserialize payload too large", "[foundation][database]") {
    // Create a message claiming payload of 100 bytes but only provide 21 header bytes
    std::vector<uint8_t> data(21, 0);
    // Set payload size at offset 17 (after type(1) + source(4) + target(4) + timestamp(4) + sequence(4))
    *reinterpret_cast<uint32_t*>(&data[17]) = 100;
    
    auto result = ZoneMessage::deserialize(data);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ZoneMessage large payload", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = ZoneMessageType::CHAT;
    msg.sourceZoneId = 1;
    msg.targetZoneId = 2;
    msg.timestamp = 1000;
    msg.sequence = 10;
    
    // Create 1KB payload
    msg.payload.resize(1024);
    for (size_t i = 0; i < msg.payload.size(); ++i) {
        msg.payload[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    auto serialized = msg.serialize();
    REQUIRE(serialized.size() == 21 + 1024);
    
    auto deserialized = ZoneMessage::deserialize(serialized);
    REQUIRE(deserialized.has_value());
    REQUIRE(deserialized->payload.size() == 1024);
    REQUIRE(deserialized->payload[0] == 0);
    REQUIRE(deserialized->payload[255] == 255);
    REQUIRE(deserialized->payload[256] == 0); // wraps
}

TEST_CASE("ZoneMessage all message types", "[foundation][database]") {
    // Test that all enum values serialize/deserialize correctly
    std::vector<ZoneMessageType> types = {
        ZoneMessageType::ENTITY_SYNC,
        ZoneMessageType::MIGRATION_REQUEST,
        ZoneMessageType::MIGRATION_STATE,
        ZoneMessageType::MIGRATION_COMPLETE,
        ZoneMessageType::BROADCAST,
        ZoneMessageType::CHAT,
        ZoneMessageType::ZONE_STATUS
    };
    
    for (auto type : types) {
        ZoneMessage msg;
        msg.type = type;
        msg.sourceZoneId = 1;
        msg.targetZoneId = 2;
        msg.timestamp = 100;
        msg.sequence = 1;
        msg.payload = {0xAA, 0xBB};
        
        auto serialized = msg.serialize();
        auto deserialized = ZoneMessage::deserialize(serialized);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->type == type);
    }
}

TEST_CASE("ZoneMessage max values", "[foundation][database]") {
    ZoneMessage msg;
    msg.type = static_cast<ZoneMessageType>(255); // max uint8_t
    msg.sourceZoneId = UINT32_MAX;
    msg.targetZoneId = UINT32_MAX;
    msg.timestamp = UINT32_MAX;
    msg.sequence = UINT32_MAX;
    msg.payload = {0xFF};
    
    auto serialized = msg.serialize();
    auto deserialized = ZoneMessage::deserialize(serialized);
    REQUIRE(deserialized.has_value());
    REQUIRE(deserialized->type == static_cast<ZoneMessageType>(255));
    REQUIRE(deserialized->sourceZoneId == UINT32_MAX);
    REQUIRE(deserialized->targetZoneId == UINT32_MAX);
    REQUIRE(deserialized->timestamp == UINT32_MAX);
    REQUIRE(deserialized->sequence == UINT32_MAX);
}
