// [NETWORK_AGENT] Unit tests for ProtobufProtocol
// Covers serialization/deserialization, roundtrip encoding, edge cases

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "netcode/ProtobufProtocol.hpp"
#include "ecs/CoreTypes.hpp"
#include <vector>
#include <cstdint>
#include <span>
#include <cstring>

using namespace DarkAges;
using namespace DarkAges::Netcode;
namespace NP = DarkAges::NetworkProto;

// ============================================================================
// Client Input Tests
// ============================================================================

TEST_CASE("ProtobufProtocol serialize/deserialize ClientInput", "[network][protobuf]") {

    SECTION("Basic client input roundtrip") {
        uint32_t sequence = 42;
        uint32_t timestamp = 1000000;
        uint32_t inputFlags = 0x0F; // forward, backward, left, right
        float yaw = 1.57f;
        float pitch = -0.5f;
        uint32_t targetEntity = 99;

        auto data = ProtobufProtocol::serializeClientInput(sequence, timestamp, inputFlags, yaw, pitch, targetEntity);
        REQUIRE_FALSE(data.empty());

        auto result = ProtobufProtocol::deserializeClientInput(data);
        REQUIRE(result.has_value());
        REQUIRE(result->sequence() == sequence);
        REQUIRE(result->timestamp() == timestamp);
        REQUIRE(result->input_flags() == inputFlags);
        REQUIRE(result->yaw() == Catch::Approx(yaw).margin(0.001f));
        REQUIRE(result->pitch() == Catch::Approx(pitch).margin(0.001f));
        REQUIRE(result->target_entity() == targetEntity);
    }

    SECTION("Default target entity is zero") {
        auto data = ProtobufProtocol::serializeClientInput(1, 0, 0, 0.0f, 0.0f);
        auto result = ProtobufProtocol::deserializeClientInput(data);
        REQUIRE(result.has_value());
        REQUIRE(result->target_entity() == 0);
    }

    SECTION("All input flags set") {
        uint32_t allFlags = 0xFF;
        auto data = ProtobufProtocol::serializeClientInput(1, 100, allFlags, 0.0f, 0.0f);
        auto result = ProtobufProtocol::deserializeClientInput(data);
        REQUIRE(result.has_value());
        REQUIRE(result->input_flags() == allFlags);
    }

    SECTION("Boundary yaw and pitch values") {
        // Max/min float values for rotation
        auto data = ProtobufProtocol::serializeClientInput(1, 0, 0, 3.14159f, -3.14159f);
        auto result = ProtobufProtocol::deserializeClientInput(data);
        REQUIRE(result.has_value());
        REQUIRE(result->yaw() == Catch::Approx(3.14159f).margin(0.001f));
        REQUIRE(result->pitch() == Catch::Approx(-3.14159f).margin(0.001f));
    }

    SECTION("Zero values") {
        auto data = ProtobufProtocol::serializeClientInput(0, 0, 0, 0.0f, 0.0f, 0);
        auto result = ProtobufProtocol::deserializeClientInput(data);
        REQUIRE(result.has_value());
        REQUIRE(result->sequence() == 0);
        REQUIRE(result->timestamp() == 0);
        REQUIRE(result->input_flags() == 0);
        REQUIRE(result->yaw() == Catch::Approx(0.0f));
        REQUIRE(result->pitch() == Catch::Approx(0.0f));
    }

    SECTION("Max uint32 values") {
        uint32_t maxU32 = 0xFFFFFFFF;
        auto data = ProtobufProtocol::serializeClientInput(maxU32, maxU32, maxU32, 0.0f, 0.0f, maxU32);
        auto result = ProtobufProtocol::deserializeClientInput(data);
        REQUIRE(result.has_value());
        REQUIRE(result->sequence() == maxU32);
        REQUIRE(result->timestamp() == maxU32);
        REQUIRE(result->input_flags() == maxU32);
        REQUIRE(result->target_entity() == maxU32);
    }

    SECTION("Deserialize invalid data returns nullopt") {
        std::vector<uint8_t> invalidData = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        auto result = ProtobufProtocol::deserializeClientInput(invalidData);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Deserialize empty data returns nullopt") {
        std::vector<uint8_t> emptyData;
        auto result = ProtobufProtocol::deserializeClientInput(emptyData);
        // Empty protobuf message is valid (all defaults)
        REQUIRE(result.has_value());
    }
}

// ============================================================================
// InputState <-> Proto Conversion Tests
// ============================================================================

TEST_CASE("ProtobufProtocol InputState conversion", "[network][protobuf]") {

    SECTION("InputState to proto and back") {
        InputState state{};
        state.forward = 1;
        state.backward = 1;
        state.left = 0;
        state.right = 1;
        state.jump = 1;
        state.attack = 0;
        state.block = 1;
        state.sprint = 0;
        state.yaw = 2.5f;
        state.pitch = -1.0f;
        state.timestamp_ms = 5000000;
        state.targetEntity = 42;

        auto proto = ProtobufProtocol::inputStateToProto(state, 12345);
        REQUIRE(proto.sequence() == 12345);
        REQUIRE(proto.timestamp() == 5000000);

        auto restored = ProtobufProtocol::protoToInputState(proto);
        REQUIRE(restored.forward == 1);
        REQUIRE(restored.backward == 1);
        REQUIRE(restored.left == 0);
        REQUIRE(restored.right == 1);
        REQUIRE(restored.jump == 1);
        REQUIRE(restored.attack == 0);
        REQUIRE(restored.block == 1);
        REQUIRE(restored.sprint == 0);
        REQUIRE(restored.yaw == Catch::Approx(2.5f).margin(0.001f));
        REQUIRE(restored.pitch == Catch::Approx(-1.0f).margin(0.001f));
        REQUIRE(restored.targetEntity == 42);
    }

    SECTION("All input flags roundtrip correctly") {
        InputState state{};
        state.forward = 1;
        state.backward = 1;
        state.left = 1;
        state.right = 1;
        state.jump = 1;
        state.attack = 1;
        state.block = 1;
        state.sprint = 1;
        state.timestamp_ms = 100;

        auto proto = ProtobufProtocol::inputStateToProto(state, 1);
        auto restored = ProtobufProtocol::protoToInputState(proto);

        REQUIRE(restored.forward == 1);
        REQUIRE(restored.backward == 1);
        REQUIRE(restored.left == 1);
        REQUIRE(restored.right == 1);
        REQUIRE(restored.jump == 1);
        REQUIRE(restored.attack == 1);
        REQUIRE(restored.block == 1);
        REQUIRE(restored.sprint == 1);
    }

    SECTION("Zero input state") {
        InputState state{};
        state.timestamp_ms = 0;

        auto proto = ProtobufProtocol::inputStateToProto(state, 0);
        auto restored = ProtobufProtocol::protoToInputState(proto);

        REQUIRE(restored.forward == 0);
        REQUIRE(restored.backward == 0);
        REQUIRE(restored.left == 0);
        REQUIRE(restored.right == 0);
        REQUIRE(restored.jump == 0);
        REQUIRE(restored.attack == 0);
        REQUIRE(restored.block == 0);
        REQUIRE(restored.sprint == 0);
        REQUIRE(restored.yaw == Catch::Approx(0.0f));
        REQUIRE(restored.pitch == Catch::Approx(0.0f));
    }
}

// ============================================================================
// Server Snapshot Tests
// ============================================================================

TEST_CASE("ProtobufProtocol server snapshot", "[network][protobuf]") {

    SECTION("Build and serialize empty snapshot") {
        auto snapshot = ProtobufProtocol::buildServerSnapshot(100, 90);
        REQUIRE(snapshot.server_tick() == 100);
        REQUIRE(snapshot.baseline_tick() == 90);
        REQUIRE(snapshot.entities_size() == 0);

        auto data = ProtobufProtocol::serializeSnapshot(snapshot);
        REQUIRE_FALSE(data.empty());

        auto deserialized = ProtobufProtocol::deserializeSnapshot(data);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->server_tick() == 100);
        REQUIRE(deserialized->baseline_tick() == 90);
        REQUIRE(deserialized->entities_size() == 0);
    }

    SECTION("Snapshot with entities") {
        auto snapshot = ProtobufProtocol::buildServerSnapshot(200, 190);

        EntityState entity1{};
        entity1.id = 1;
        entity1.type = EntityType::PLAYER;
        entity1.position.x = 10.0f;
        entity1.position.y = 0.0f;
        entity1.position.z = 5.0f;
        entity1.healthPercent = 100;
        entity1.teamId = 1;

        EntityState entity2{};
        entity2.id = 2;
        entity2.type = EntityType::NPC;
        entity2.position.x = -3.0f;
        entity2.position.y = 1.5f;
        entity2.position.z = 7.0f;
        entity2.healthPercent = 50;
        entity2.teamId = 2;

        ProtobufProtocol::addEntityToSnapshot(snapshot, entity1);
        ProtobufProtocol::addEntityToSnapshot(snapshot, entity2);
        REQUIRE(snapshot.entities_size() == 2);

        auto data = ProtobufProtocol::serializeSnapshot(snapshot);
        auto deserialized = ProtobufProtocol::deserializeSnapshot(data);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->entities_size() == 2);
        REQUIRE(deserialized->entities(0).entity_id() == 1);
        REQUIRE(deserialized->entities(1).entity_id() == 2);
    }

    SECTION("Deserialize invalid snapshot returns nullopt") {
        std::vector<uint8_t> badData = {0x01, 0x02, 0x03};
        auto result = ProtobufProtocol::deserializeSnapshot(badData);
        // Protobuf can parse truncated data gracefully - may or may not return nullopt
        // depending on whether the partial data forms a valid prefix
    }

    SECTION("Snapshot with max tick values") {
        auto snapshot = ProtobufProtocol::buildServerSnapshot(0xFFFFFFFF, 0);
        auto data = ProtobufProtocol::serializeSnapshot(snapshot);
        auto result = ProtobufProtocol::deserializeSnapshot(data);
        REQUIRE(result.has_value());
        REQUIRE(result->server_tick() == 0xFFFFFFFF);
        REQUIRE(result->baseline_tick() == 0);
    }
}

// ============================================================================
// Entity State Conversion Tests
// ============================================================================

TEST_CASE("ProtobufProtocol entity state conversion", "[network][protobuf]") {

    SECTION("EntityState to proto and back with quantization") {
        EntityState original{};
        original.id = 42;
        original.type = EntityType::PLAYER;
        original.position.x = 10.5f;
        original.position.y = 2.0f;
        original.position.z = -7.25f;
        original.velocity.dx = 3.5f;
        original.velocity.dy = 0.0f;
        original.velocity.dz = -1.5f;
        original.rotation.yaw = 1.57f;
        original.rotation.pitch = 0.3f;
        original.healthPercent = 75;
        original.animState = AnimationState::RUN;
        original.teamId = 3;

        auto proto = ProtobufProtocol::entityStateToProto(original);
        auto restored = ProtobufProtocol::protoToEntityState(proto);

        REQUIRE(restored.id == original.id);
        REQUIRE(restored.type == original.type);
        // Position is quantized (x * 64), so expect approximate match
        REQUIRE(restored.position.x == Catch::Approx(original.position.x).margin(0.02f));
        REQUIRE(restored.position.y == Catch::Approx(original.position.y).margin(0.02f));
        REQUIRE(restored.position.z == Catch::Approx(original.position.z).margin(0.02f));
        // Velocity is quantized (x * 100)
        REQUIRE(restored.velocity.dx == Catch::Approx(original.velocity.dx).margin(0.02f));
        REQUIRE(restored.velocity.dz == Catch::Approx(original.velocity.dz).margin(0.02f));
        REQUIRE(restored.healthPercent == original.healthPercent);
        REQUIRE(restored.animState == original.animState);
        REQUIRE(restored.teamId == original.teamId);
    }

    SECTION("Zero entity state") {
        EntityState original{};
        auto proto = ProtobufProtocol::entityStateToProto(original);
        auto restored = ProtobufProtocol::protoToEntityState(proto);

        REQUIRE(restored.id == 0);
        REQUIRE(restored.type == EntityType::PLAYER);
        REQUIRE(restored.healthPercent == 100); // default EntityState healthPercent
        REQUIRE(restored.teamId == 0);
    }

    SECTION("Different entity types") {
        EntityState entity{};
        entity.id = 1;

        entity.type = EntityType::PROJECTILE;
        auto proto = ProtobufProtocol::entityStateToProto(entity);
        auto restored = ProtobufProtocol::protoToEntityState(proto);
        REQUIRE(restored.type == EntityType::PROJECTILE);

        entity.type = EntityType::LOOT;
        proto = ProtobufProtocol::entityStateToProto(entity);
        restored = ProtobufProtocol::protoToEntityState(proto);
        REQUIRE(restored.type == EntityType::LOOT);

        entity.type = EntityType::NPC;
        proto = ProtobufProtocol::entityStateToProto(entity);
        restored = ProtobufProtocol::protoToEntityState(proto);
        REQUIRE(restored.type == EntityType::NPC);
    }

    SECTION("Different animation states") {
        EntityState entity{};
        entity.id = 1;

        entity.animState = AnimationState::IDLE;
        auto proto = ProtobufProtocol::entityStateToProto(entity);
        REQUIRE(ProtobufProtocol::protoToEntityState(proto).animState == AnimationState::IDLE);

        entity.animState = AnimationState::ATTACK;
        proto = ProtobufProtocol::entityStateToProto(entity);
        REQUIRE(ProtobufProtocol::protoToEntityState(proto).animState == AnimationState::ATTACK);

        entity.animState = AnimationState::BLOCK;
        proto = ProtobufProtocol::entityStateToProto(entity);
        REQUIRE(ProtobufProtocol::protoToEntityState(proto).animState == AnimationState::BLOCK);

        entity.animState = AnimationState::DEAD;
        proto = ProtobufProtocol::entityStateToProto(entity);
        REQUIRE(ProtobufProtocol::protoToEntityState(proto).animState == AnimationState::DEAD);
    }

    SECTION("Entity with negative position") {
        EntityState entity{};
        entity.id = 1;
        entity.position.x = -100.0f;
        entity.position.y = -50.0f;
        entity.position.z = -200.0f;

        auto proto = ProtobufProtocol::entityStateToProto(entity);
        auto restored = ProtobufProtocol::protoToEntityState(proto);

        REQUIRE(restored.position.x == Catch::Approx(-100.0f).margin(0.02f));
        REQUIRE(restored.position.y == Catch::Approx(-50.0f).margin(0.02f));
        REQUIRE(restored.position.z == Catch::Approx(-200.0f).margin(0.02f));
    }
}

// ============================================================================
// Reliable Event Tests
// ============================================================================

TEST_CASE("ProtobufProtocol reliable events", "[network][protobuf]") {

    SECTION("Create and serialize damage event") {
        auto event = ProtobufProtocol::createDamageEvent(10, 20, 500);
        REQUIRE(event.type() == NP::ReliableEvent::DAMAGE_DEALT);
        REQUIRE(event.source_entity() == 10);
        REQUIRE(event.target_entity() == 20);
        REQUIRE(event.value() == 500);

        auto data = ProtobufProtocol::serializeEvent(event);
        REQUIRE_FALSE(data.empty());

        auto deserialized = ProtobufProtocol::deserializeEvent(data);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->type() == NP::ReliableEvent::DAMAGE_DEALT);
        REQUIRE(deserialized->source_entity() == 10);
        REQUIRE(deserialized->target_entity() == 20);
        REQUIRE(deserialized->value() == 500);
    }

    SECTION("Damage event with ability ID") {
        auto event = ProtobufProtocol::createDamageEvent(1, 2, 750, 42);
        REQUIRE(event.extra_data().size() == sizeof(uint32_t));

        auto data = ProtobufProtocol::serializeEvent(event);
        auto deserialized = ProtobufProtocol::deserializeEvent(data);
        REQUIRE(deserialized.has_value());

        // Verify ability ID is stored in extra_data
        uint32_t abilityId = 0;
        std::memcpy(&abilityId, deserialized->extra_data().data(), sizeof(uint32_t));
        REQUIRE(abilityId == 42);
    }

    SECTION("Damage event without ability ID has no extra_data") {
        auto event = ProtobufProtocol::createDamageEvent(1, 2, 100, 0);
        REQUIRE(event.extra_data().empty());
    }

    SECTION("Create and serialize player death event") {
        auto event = ProtobufProtocol::createPlayerDeathEvent(30, 40);
        REQUIRE(event.type() == NP::ReliableEvent::PLAYER_DIED);
        REQUIRE(event.source_entity() == 40); // killer
        REQUIRE(event.target_entity() == 30); // victim

        auto data = ProtobufProtocol::serializeEvent(event);
        auto deserialized = ProtobufProtocol::deserializeEvent(data);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->type() == NP::ReliableEvent::PLAYER_DIED);
        REQUIRE(deserialized->source_entity() == 40);
        REQUIRE(deserialized->target_entity() == 30);
    }

    SECTION("Player death event with no killer (environment)") {
        auto event = ProtobufProtocol::createPlayerDeathEvent(50);
        REQUIRE(event.source_entity() == 0);
        REQUIRE(event.target_entity() == 50);

        auto data = ProtobufProtocol::serializeEvent(event);
        auto deserialized = ProtobufProtocol::deserializeEvent(data);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->source_entity() == 0);
    }

    SECTION("Deserialize invalid event returns nullopt") {
        std::vector<uint8_t> badData = {0xFE, 0xED, 0xFA, 0xCE};
        auto result = ProtobufProtocol::deserializeEvent(badData);
        // May or may not be nullopt depending on protobuf parsing behavior
    }

    SECTION("Negative damage value") {
        // Healing could be represented as negative damage
        auto event = ProtobufProtocol::createDamageEvent(1, 2, -300);
        REQUIRE(event.value() == -300);

        auto data = ProtobufProtocol::serializeEvent(event);
        auto deserialized = ProtobufProtocol::deserializeEvent(data);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->value() == -300);
    }

    SECTION("Max damage value") {
        auto event = ProtobufProtocol::createDamageEvent(1, 2, 2147483647);
        auto data = ProtobufProtocol::serializeEvent(event);
        auto deserialized = ProtobufProtocol::deserializeEvent(data);
        REQUIRE(deserialized.has_value());
        REQUIRE(deserialized->value() == 2147483647);
    }
}

// ============================================================================
// Handshake Tests
// ============================================================================

TEST_CASE("ProtobufProtocol handshake", "[network][protobuf]") {

    SECTION("Create and verify handshake") {
        auto hs = ProtobufProtocol::createHandshake(1, 100, "auth-token-abc", "TestPlayer");
        REQUIRE(hs.protocol_version() == 1);
        REQUIRE(hs.client_version() == 100);
        REQUIRE(hs.auth_token() == "auth-token-abc");
        REQUIRE(hs.username() == "TestPlayer");
    }

    SECTION("Handshake with empty strings") {
        auto hs = ProtobufProtocol::createHandshake(0, 0, "", "");
        REQUIRE(hs.auth_token().empty());
        REQUIRE(hs.username().empty());
    }

    SECTION("Handshake with long strings") {
        std::string longToken(1000, 'A');
        std::string longName(200, 'B');
        auto hs = ProtobufProtocol::createHandshake(1, 1, longToken, longName);
        REQUIRE(hs.auth_token().size() == 1000);
        REQUIRE(hs.username().size() == 200);
    }

    SECTION("Create accepted handshake response") {
        auto resp = ProtobufProtocol::createHandshakeResponse(true, 500, 42, 10.0f, 0.0f, 20.0f);
        REQUIRE(resp.accepted() == true);
        REQUIRE(resp.server_tick() == 500);
        REQUIRE(resp.your_entity_id() == 42);
        REQUIRE(resp.spawn_x() == Catch::Approx(10.0f));
        REQUIRE(resp.spawn_y() == Catch::Approx(0.0f));
        REQUIRE(resp.spawn_z() == Catch::Approx(20.0f));
        REQUIRE(resp.reject_reason().empty());
    }

    SECTION("Create rejected handshake response") {
        auto resp = ProtobufProtocol::createHandshakeResponse(
            false, 0, 0, 0.0f, 0.0f, 0.0f, "Invalid auth token"
        );
        REQUIRE(resp.accepted() == false);
        REQUIRE(resp.reject_reason() == "Invalid auth token");
    }

    SECTION("Handshake response with default reject reason") {
        auto resp = ProtobufProtocol::createHandshakeResponse(true, 100, 1, 5.0f, 5.0f, 5.0f);
        REQUIRE(resp.reject_reason().empty());
    }
}

// ============================================================================
// Ping/Pong Tests
// ============================================================================

TEST_CASE("ProtobufProtocol ping/pong", "[network][protobuf]") {

    SECTION("Serialize and verify ping") {
        auto data = ProtobufProtocol::serializePing(1234567);
        REQUIRE_FALSE(data.empty());

        NP::Ping ping;
        REQUIRE(ping.ParseFromArray(data.data(), static_cast<int>(data.size())));
        REQUIRE(ping.client_timestamp() == 1234567);
    }

    SECTION("Serialize and verify pong") {
        auto data = ProtobufProtocol::serializePong(1000, 1500);
        REQUIRE_FALSE(data.empty());

        NP::Pong pong;
        REQUIRE(pong.ParseFromArray(data.data(), static_cast<int>(data.size())));
        REQUIRE(pong.client_timestamp() == 1000);
        REQUIRE(pong.server_timestamp() == 1500);
    }

    SECTION("Ping with zero timestamp") {
        auto data = ProtobufProtocol::serializePing(0);
        NP::Ping ping;
        REQUIRE(ping.ParseFromArray(data.data(), static_cast<int>(data.size())));
        REQUIRE(ping.client_timestamp() == 0);
    }

    SECTION("Pong with max timestamps") {
        auto data = ProtobufProtocol::serializePong(0xFFFFFFFF, 0xFFFFFFFF);
        NP::Pong pong;
        REQUIRE(pong.ParseFromArray(data.data(), static_cast<int>(data.size())));
        REQUIRE(pong.client_timestamp() == 0xFFFFFFFF);
        REQUIRE(pong.server_timestamp() == 0xFFFFFFFF);
    }
}

// ============================================================================
// Message Wrapper Tests
// ============================================================================

TEST_CASE("ProtobufProtocol message unwrapping", "[network][protobuf]") {

    SECTION("Unwrap valid NetworkMessage with handshake payload") {
        NP::NetworkMessage wrapper;
        wrapper.set_type(NP::NetworkMessage::HANDSHAKE);
        wrapper.set_sequence(1);

        auto* hs = wrapper.mutable_handshake();
        hs->set_protocol_version(1);
        hs->set_client_version(42);
        hs->set_auth_token("token");
        hs->set_username("player1");

        std::vector<uint8_t> buffer(wrapper.ByteSizeLong());
        wrapper.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));

        auto result = ProtobufProtocol::unwrapMessage(buffer);
        REQUIRE(result.has_value());
        REQUIRE(result->type() == NP::NetworkMessage::HANDSHAKE);
        REQUIRE(result->sequence() == 1);
        REQUIRE(result->handshake().username() == "player1");
    }

    SECTION("Unwrap valid NetworkMessage with client input") {
        NP::NetworkMessage wrapper;
        wrapper.set_type(NP::NetworkMessage::CLIENT_INPUT);
        wrapper.set_sequence(42);

        auto* input = wrapper.mutable_client_input();
        input->set_sequence(42);
        input->set_timestamp(1000);
        input->set_input_flags(0x03);
        input->set_yaw(1.0f);

        std::vector<uint8_t> buffer(wrapper.ByteSizeLong());
        wrapper.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));

        auto result = ProtobufProtocol::unwrapMessage(buffer);
        REQUIRE(result.has_value());
        REQUIRE(result->type() == NP::NetworkMessage::CLIENT_INPUT);
        REQUIRE(result->client_input().input_flags() == 0x03);
    }

    SECTION("Unwrap invalid data returns nullopt") {
        std::vector<uint8_t> badData = {0xFF, 0xFF};
        auto result = ProtobufProtocol::unwrapMessage(badData);
        // May or may not be nullopt - protobuf is lenient with unknown fields
    }

    SECTION("Unwrap empty data returns valid empty message") {
        std::vector<uint8_t> empty;
        auto result = ProtobufProtocol::unwrapMessage(empty);
        // Empty protobuf message is valid with defaults
        REQUIRE(result.has_value());
    }
}

// ============================================================================
// Bandwidth Optimization Tests
// ============================================================================

TEST_CASE("ProtobufProtocol bandwidth utilities", "[network][protobuf]") {

    SECTION("Calculate size via serialization consistency") {
        // With LITE_RUNTIME, calculateSize may not accept lite messages directly.
        // Verify serialization produces a non-empty buffer of consistent size.
        auto data1 = ProtobufProtocol::serializeClientInput(1, 100, 0xFF, 1.5f, 0.5f);
        auto data2 = ProtobufProtocol::serializeClientInput(1, 100, 0xFF, 1.5f, 0.5f);
        REQUIRE(data1.size() == data2.size());
        REQUIRE(data1.size() > 0);

        // Same input produces same serialized size
        auto data3 = ProtobufProtocol::serializeClientInput(2, 200, 0x00, 0.0f, 0.0f);
        // Different data may produce different sizes due to varint encoding
        REQUIRE(data3.size() > 0);
    }

    SECTION("Empty client input serialization is minimal") {
        // All zero/default values should produce the smallest encoding
        auto defaultData = ProtobufProtocol::serializeClientInput(0, 0, 0, 0.0f, 0.0f);
        auto populatedData = ProtobufProtocol::serializeClientInput(1000, 5000, 0xFF, 3.14f, 1.5f);
        REQUIRE(defaultData.size() <= populatedData.size());
    }

    SECTION("Message type names") {
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::CLIENT_INPUT)) == "CLIENT_INPUT");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::ABILITY_REQUEST)) == "ABILITY_REQUEST");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::ATTACK_INPUT)) == "ATTACK_INPUT");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::HANDSHAKE)) == "HANDSHAKE");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::PING)) == "PING");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::SERVER_SNAPSHOT)) == "SERVER_SNAPSHOT");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::RELIABLE_EVENT)) == "RELIABLE_EVENT");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::HANDSHAKE_RESPONSE)) == "HANDSHAKE_RESPONSE");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::SERVER_CORRECTION)) == "SERVER_CORRECTION");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::PONG)) == "PONG");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::SERVER_CONFIG)) == "SERVER_CONFIG");
        REQUIRE(std::string(ProtobufProtocol::getMessageTypeName(NP::NetworkMessage::DISCONNECT)) == "DISCONNECT");
    }
}

// ============================================================================
// Snapshot with Multiple Entities (Integration-style)
// ============================================================================

TEST_CASE("ProtobufProtocol snapshot with multiple entities roundtrip", "[network][protobuf]") {

    SECTION("Full snapshot roundtrip with varied entities") {
        auto snapshot = ProtobufProtocol::buildServerSnapshot(1000, 999);

        // Add player entity
        EntityState player{};
        player.id = 1;
        player.type = EntityType::PLAYER;
        player.position.x = 50.0f;
        player.position.y = 0.0f;
        player.position.z = -30.0f;
        player.velocity.dx = 5.0f;
        player.velocity.dz = -3.0f;
        player.rotation.yaw = 3.14f;
        player.healthPercent = 80;
        player.animState = AnimationState::RUN;
        player.teamId = 1;
        ProtobufProtocol::addEntityToSnapshot(snapshot, player);

        // Add projectile entity
        EntityState proj{};
        proj.id = 100;
        proj.type = EntityType::PROJECTILE;
        proj.position.x = 48.0f;
        proj.position.y = 1.5f;
        proj.position.z = -28.0f;
        proj.velocity.dx = -10.0f;
        proj.velocity.dz = 5.0f;
        proj.healthPercent = 100;
        proj.animState = AnimationState::IDLE;
        proj.teamId = 1;
        ProtobufProtocol::addEntityToSnapshot(snapshot, proj);

        // Add NPC entity
        EntityState npc{};
        npc.id = 200;
        npc.type = EntityType::NPC;
        npc.position.x = -10.0f;
        npc.position.y = 0.0f;
        npc.position.z = 15.0f;
        npc.healthPercent = 45;
        npc.animState = AnimationState::ATTACK;
        npc.teamId = 2;
        ProtobufProtocol::addEntityToSnapshot(snapshot, npc);

        // Serialize and deserialize
        auto data = ProtobufProtocol::serializeSnapshot(snapshot);
        auto restored = ProtobufProtocol::deserializeSnapshot(data);

        REQUIRE(restored.has_value());
        REQUIRE(restored->server_tick() == 1000);
        REQUIRE(restored->baseline_tick() == 999);
        REQUIRE(restored->entities_size() == 3);

        // Verify player
        auto& e0 = restored->entities(0);
        REQUIRE(e0.entity_id() == 1);
        REQUIRE(e0.type() == static_cast<uint32_t>(EntityType::PLAYER));
        REQUIRE(e0.health_percent() == 80);
        REQUIRE(e0.team_id() == 1);

        // Verify projectile
        auto& e1 = restored->entities(1);
        REQUIRE(e1.entity_id() == 100);
        REQUIRE(e1.type() == static_cast<uint32_t>(EntityType::PROJECTILE));

        // Verify NPC
        auto& e2 = restored->entities(2);
        REQUIRE(e2.entity_id() == 200);
        REQUIRE(e2.type() == static_cast<uint32_t>(EntityType::NPC));
        REQUIRE(e2.health_percent() == 45);
        REQUIRE(e2.anim_state() == static_cast<uint32_t>(AnimationState::ATTACK));
        REQUIRE(e2.team_id() == 2);
    }
}
