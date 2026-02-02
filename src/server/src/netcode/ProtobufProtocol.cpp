// [NETWORK_AGENT] Protobuf Protocol Implementation
// WP-8-6: GameNetworkingSockets Protobuf Integration

#include "netcode/ProtobufProtocol.hpp"
#include <cstring>
#include <google/protobuf/message.h>

namespace DarkAges {
namespace Netcode {

// =========================================================================
// Client Input Serialization
// =========================================================================

std::vector<uint8_t> ProtobufProtocol::serializeClientInput(
    uint32_t sequence,
    uint32_t timestamp,
    uint32_t inputFlags,
    float yaw,
    float pitch,
    uint32_t targetEntity
) {
    NetworkProto::ClientInput input;
    input.set_sequence(sequence);
    input.set_timestamp(timestamp);
    input.set_input_flags(inputFlags);
    input.set_yaw(yaw);
    input.set_pitch(pitch);
    input.set_target_entity(targetEntity);
    
    std::vector<uint8_t> buffer(input.ByteSizeLong());
    input.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    return buffer;
}

std::optional<NetworkProto::ClientInput> ProtobufProtocol::deserializeClientInput(
    std::span<const uint8_t> data
) {
    NetworkProto::ClientInput input;
    if (input.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return input;
    }
    return std::nullopt;
}

NetworkProto::ClientInput ProtobufProtocol::inputStateToProto(
    const InputState& state,
    uint32_t sequence
) {
    NetworkProto::ClientInput input;
    input.set_sequence(sequence);
    input.set_timestamp(state.timestamp_ms);
    
    // Pack input flags
    uint32_t flags = 0;
    flags |= (state.forward ? 1 : 0) << 0;
    flags |= (state.backward ? 1 : 0) << 1;
    flags |= (state.left ? 1 : 0) << 2;
    flags |= (state.right ? 1 : 0) << 3;
    flags |= (state.jump ? 1 : 0) << 4;
    flags |= (state.attack ? 1 : 0) << 5;
    flags |= (state.block ? 1 : 0) << 6;
    flags |= (state.sprint ? 1 : 0) << 7;
    
    input.set_input_flags(flags);
    input.set_yaw(state.yaw);
    input.set_pitch(state.pitch);
    input.set_target_entity(state.targetEntity);
    
    return input;
}

InputState ProtobufProtocol::protoToInputState(const NetworkProto::ClientInput& proto) {
    InputState state{};
    state.timestamp_ms = proto.timestamp();
    
    uint32_t flags = proto.input_flags();
    state.forward = (flags >> 0) & 1;
    state.backward = (flags >> 1) & 1;
    state.left = (flags >> 2) & 1;
    state.right = (flags >> 3) & 1;
    state.jump = (flags >> 4) & 1;
    state.attack = (flags >> 5) & 1;
    state.block = (flags >> 6) & 1;
    state.sprint = (flags >> 7) & 1;
    
    state.yaw = proto.yaw();
    state.pitch = proto.pitch();
    state.targetEntity = proto.target_entity();
    
    return state;
}

// =========================================================================
// Server Snapshot Serialization
// =========================================================================

NetworkProto::ServerSnapshot ProtobufProtocol::buildServerSnapshot(
    uint32_t serverTick,
    uint32_t baselineTick
) {
    NetworkProto::ServerSnapshot snapshot;
    snapshot.set_server_tick(serverTick);
    snapshot.set_baseline_tick(baselineTick);
    return snapshot;
}

void ProtobufProtocol::addEntityToSnapshot(
    NetworkProto::ServerSnapshot& snapshot,
    const EntityState& entity
) {
    NetworkProto::EntityState* protoEntity = snapshot.add_entities();
    *protoEntity = entityStateToProto(entity);
}

std::vector<uint8_t> ProtobufProtocol::serializeSnapshot(
    const NetworkProto::ServerSnapshot& snapshot
) {
    std::vector<uint8_t> buffer(snapshot.ByteSizeLong());
    snapshot.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    return buffer;
}

std::optional<NetworkProto::ServerSnapshot> ProtobufProtocol::deserializeSnapshot(
    std::span<const uint8_t> data
) {
    NetworkProto::ServerSnapshot snapshot;
    if (snapshot.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return snapshot;
    }
    return std::nullopt;
}

// =========================================================================
// Reliable Event Serialization
// =========================================================================

NetworkProto::ReliableEvent ProtobufProtocol::createDamageEvent(
    uint32_t sourceEntity,
    uint32_t targetEntity,
    int32_t damage,
    uint32_t abilityId
) {
    NetworkProto::ReliableEvent event;
    event.set_type(NetworkProto::ReliableEvent::DAMAGE_DEALT);
    event.set_source_entity(sourceEntity);
    event.set_target_entity(targetEntity);
    event.set_value(damage);
    
    // Store ability ID in extra_data if non-zero
    if (abilityId != 0) {
        event.set_extra_data(&abilityId, sizeof(abilityId));
    }
    
    return event;
}

NetworkProto::ReliableEvent ProtobufProtocol::createPlayerDeathEvent(
    uint32_t entityId
) {
    NetworkProto::ReliableEvent event;
    event.set_type(NetworkProto::ReliableEvent::PLAYER_DIED);
    event.set_source_entity(entityId);
    return event;
}

std::vector<uint8_t> ProtobufProtocol::serializeEvent(
    const NetworkProto::ReliableEvent& event
) {
    std::vector<uint8_t> buffer(event.ByteSizeLong());
    event.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    return buffer;
}

std::optional<NetworkProto::ReliableEvent> ProtobufProtocol::deserializeEvent(
    std::span<const uint8_t> data
) {
    NetworkProto::ReliableEvent event;
    if (event.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return event;
    }
    return std::nullopt;
}

// =========================================================================
// Handshake Messages
// =========================================================================

NetworkProto::Handshake ProtobufProtocol::createHandshake(
    uint32_t protocolVersion,
    uint32_t clientVersion,
    const std::string& authToken,
    const std::string& username
) {
    NetworkProto::Handshake handshake;
    handshake.set_protocol_version(protocolVersion);
    handshake.set_client_version(clientVersion);
    handshake.set_auth_token(authToken);
    handshake.set_username(username);
    return handshake;
}

NetworkProto::HandshakeResponse ProtobufProtocol::createHandshakeResponse(
    bool accepted,
    uint32_t serverTick,
    uint32_t entityId,
    float spawnX, float spawnY, float spawnZ,
    const std::string& rejectReason
) {
    NetworkProto::HandshakeResponse response;
    response.set_accepted(accepted);
    response.set_server_tick(serverTick);
    response.set_your_entity_id(entityId);
    response.set_spawn_x(spawnX);
    response.set_spawn_y(spawnY);
    response.set_spawn_z(spawnZ);
    response.set_reject_reason(rejectReason);
    return response;
}

// =========================================================================
// Ping/Pong
// =========================================================================

std::vector<uint8_t> ProtobufProtocol::serializePing(uint32_t clientTimestamp) {
    NetworkProto::Ping ping;
    ping.set_client_timestamp(clientTimestamp);
    
    std::vector<uint8_t> buffer(ping.ByteSizeLong());
    ping.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    return buffer;
}

std::vector<uint8_t> ProtobufProtocol::serializePong(
    uint32_t clientTimestamp,
    uint32_t serverTimestamp
) {
    NetworkProto::Pong pong;
    pong.set_client_timestamp(clientTimestamp);
    pong.set_server_timestamp(serverTimestamp);
    
    std::vector<uint8_t> buffer(pong.ByteSizeLong());
    pong.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    return buffer;
}

// =========================================================================
// Top-Level Message Wrapper
// =========================================================================

std::optional<NetworkProto::NetworkMessage> ProtobufProtocol::unwrapMessage(
    std::span<const uint8_t> data
) {
    NetworkProto::NetworkMessage wrapper;
    if (wrapper.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return wrapper;
    }
    return std::nullopt;
}

// =========================================================================
// Entity State Helpers
// =========================================================================

NetworkProto::EntityState ProtobufProtocol::entityStateToProto(
    const EntityState& entity
) {
    NetworkProto::EntityState proto;
    proto.set_entity_id(entity.id);
    proto.set_type(static_cast<uint32_t>(entity.type));
    
    // Quantize position (actual = value / 64.0)
    proto.set_pos_x(static_cast<int32_t>(entity.position.x * 64.0f));
    proto.set_pos_y(static_cast<int32_t>(entity.position.y * 64.0f));
    proto.set_pos_z(static_cast<int32_t>(entity.position.z * 64.0f));
    
    // Quantize velocity
    proto.set_vel_x(static_cast<int32_t>(entity.velocity.dx * 100.0f));
    proto.set_vel_y(static_cast<int32_t>(entity.velocity.dy * 100.0f));
    proto.set_vel_z(static_cast<int32_t>(entity.velocity.dz * 100.0f));
    
    // Quantize rotation
    proto.set_yaw(static_cast<int32_t>(entity.rotation.yaw * (32767.0f / 3.14159265f)));
    proto.set_pitch(static_cast<int32_t>(entity.rotation.pitch * (32767.0f / 3.14159265f)));
    
    proto.set_health_percent(entity.healthPercent);
    proto.set_anim_state(static_cast<uint32_t>(entity.animState));
    proto.set_team_id(entity.teamId);
    
    return proto;
}

EntityState ProtobufProtocol::protoToEntityState(
    const NetworkProto::EntityState& proto
) {
    EntityState entity{};
    entity.id = proto.entity_id();
    entity.type = static_cast<EntityType>(proto.type());
    
    // Dequantize position
    entity.position.x = proto.pos_x() / 64.0f;
    entity.position.y = proto.pos_y() / 64.0f;
    entity.position.z = proto.pos_z() / 64.0f;
    
    // Dequantize velocity
    entity.velocity.dx = proto.vel_x() / 100.0f;
    entity.velocity.dy = proto.vel_y() / 100.0f;
    entity.velocity.dz = proto.vel_z() / 100.0f;
    
    // Dequantize rotation
    entity.rotation.yaw = proto.yaw() * (3.14159265f / 32767.0f);
    entity.rotation.pitch = proto.pitch() * (3.14159265f / 32767.0f);
    
    entity.healthPercent = static_cast<uint8_t>(proto.health_percent());
    entity.animState = static_cast<AnimationState>(proto.anim_state());
    entity.teamId = proto.team_id();
    
    return entity;
}

// =========================================================================
// Bandwidth Optimization
// =========================================================================

size_t ProtobufProtocol::calculateSize(const google::protobuf::Message& message) {
    return message.ByteSizeLong();
}

const char* ProtobufProtocol::getMessageTypeName(
    NetworkProto::NetworkMessage::MessageType type
) {
    switch (type) {
        case NetworkProto::NetworkMessage::CLIENT_INPUT: return "CLIENT_INPUT";
        case NetworkProto::NetworkMessage::ABILITY_REQUEST: return "ABILITY_REQUEST";
        case NetworkProto::NetworkMessage::ATTACK_INPUT: return "ATTACK_INPUT";
        case NetworkProto::NetworkMessage::HANDSHAKE: return "HANDSHAKE";
        case NetworkProto::NetworkMessage::PING: return "PING";
        case NetworkProto::NetworkMessage::SERVER_SNAPSHOT: return "SERVER_SNAPSHOT";
        case NetworkProto::NetworkMessage::RELIABLE_EVENT: return "RELIABLE_EVENT";
        case NetworkProto::NetworkMessage::HANDSHAKE_RESPONSE: return "HANDSHAKE_RESPONSE";
        case NetworkProto::NetworkMessage::SERVER_CORRECTION: return "SERVER_CORRECTION";
        case NetworkProto::NetworkMessage::PONG: return "PONG";
        case NetworkProto::NetworkMessage::SERVER_CONFIG: return "SERVER_CONFIG";
        case NetworkProto::NetworkMessage::DISCONNECT: return "DISCONNECT";
        default: return "UNKNOWN";
    }
}

} // namespace Netcode
} // namespace DarkAges
