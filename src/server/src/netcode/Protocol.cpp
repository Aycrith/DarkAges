// [NETWORK_AGENT] Protocol serialization using Protobuf
// Implements binary serialization for client-server communication

#include "netcode/NetworkManager.hpp"
#include "generated_proto/network_protocol.pb.h"
#include <cstring>

namespace DarkAges {
namespace Protocol {

// Protocol version: 1.0
constexpr uint32_t PROTOCOL_VERSION_MAJOR = 1;
constexpr uint32_t PROTOCOL_VERSION_MINOR = 0;
constexpr uint32_t PROTOCOL_VERSION = (PROTOCOL_VERSION_MAJOR << 16) | PROTOCOL_VERSION_MINOR;

// ============================================================================
// Input State Serialization (Legacy - for backwards compatibility)
// ============================================================================

std::vector<uint8_t> serializeInput(const InputState& input) {
    // Use Protobuf for serialization
    NetworkProto::ClientInput proto_input;
    proto_input.set_sequence(input.sequence);
    proto_input.set_timestamp(input.timestamp_ms);
    
    // Pack input flags
    uint32_t flags = 0;
    flags |= (input.forward ? 1 : 0) << 0;
    flags |= (input.backward ? 1 : 0) << 1;
    flags |= (input.left ? 1 : 0) << 2;
    flags |= (input.right ? 1 : 0) << 3;
    flags |= (input.jump ? 1 : 0) << 4;
    flags |= (input.attack ? 1 : 0) << 5;
    flags |= (input.block ? 1 : 0) << 6;
    flags |= (input.sprint ? 1 : 0) << 7;
    proto_input.set_input_flags(flags);
    
    proto_input.set_yaw(input.yaw);
    proto_input.set_pitch(input.pitch);
    
    std::vector<uint8_t> data;
    data.resize(proto_input.ByteSizeLong());
    proto_input.SerializeToArray(data.data(), static_cast<int>(data.size()));
    return data;
}

bool deserializeInput(std::span<const uint8_t> data, InputState& outInput) {
    NetworkProto::ClientInput proto_input;
    if (!proto_input.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return false;
    }
    
    outInput.sequence = proto_input.sequence();
    outInput.timestamp_ms = proto_input.timestamp();
    
    // Unpack flags
    uint32_t flags = proto_input.input_flags();
    outInput.forward = (flags >> 0) & 1;
    outInput.backward = (flags >> 1) & 1;
    outInput.left = (flags >> 2) & 1;
    outInput.right = (flags >> 3) & 1;
    outInput.jump = (flags >> 4) & 1;
    outInput.attack = (flags >> 5) & 1;
    outInput.block = (flags >> 6) & 1;
    outInput.sprint = (flags >> 7) & 1;
    
    outInput.yaw = proto_input.yaw();
    outInput.pitch = proto_input.pitch();
    
    return true;
}

// ============================================================================
// Entity State Serialization
// ============================================================================

namespace {
    // Quantization helpers
    constexpr float POSITION_SCALE = 64.0f;  // ~1.5cm precision
    constexpr float ROTATION_SCALE = 32767.0f / 3.14159265f;  // Precision for radians
    
    inline int32_t quantizePosition(float value) {
        return static_cast<int32_t>(value * POSITION_SCALE);
    }
    
    inline float dequantizePosition(int32_t value) {
        return value / POSITION_SCALE;
    }
    
    inline int32_t quantizeRotation(float value) {
        return static_cast<int32_t>(value * ROTATION_SCALE);
    }
    
    inline float dequantizeRotation(int32_t value) {
        return value / ROTATION_SCALE;
    }
}

void entityToProto(const EntityStateData& entity, NetworkProto::EntityState* proto) {
    proto->set_entity_id(static_cast<uint32_t>(entity.entity));
    proto->set_type(entity.entityType);
    
    // Quantize position
    proto->set_pos_x(quantizePosition(entity.position.x * Constants::FIXED_TO_FLOAT));
    proto->set_pos_y(quantizePosition(entity.position.y * Constants::FIXED_TO_FLOAT));
    proto->set_pos_z(quantizePosition(entity.position.z * Constants::FIXED_TO_FLOAT));
    
    // Quantize velocity
    proto->set_vel_x(quantizePosition(entity.velocity.dx * Constants::FIXED_TO_FLOAT));
    proto->set_vel_y(quantizePosition(entity.velocity.dy * Constants::FIXED_TO_FLOAT));
    proto->set_vel_z(quantizePosition(entity.velocity.dz * Constants::FIXED_TO_FLOAT));
    
    // Quantize rotation
    proto->set_yaw(quantizeRotation(entity.rotation.yaw));
    proto->set_pitch(quantizeRotation(entity.rotation.pitch));
    
    proto->set_health_percent(entity.healthPercent);
    proto->set_anim_state(entity.animState);
    proto->set_team_id(entity.teamId);
    proto->set_changed_fields(entity.timestamp);  // Use timestamp as changed fields marker
}

EntityStateData protoToEntity(const NetworkProto::EntityState& proto) {
    EntityStateData entity;
    entity.entity = static_cast<EntityID>(proto.entity_id());
    entity.entityType = static_cast<uint8_t>(proto.type());
    
    // Dequantize position
    entity.position.x = static_cast<Constants::Fixed>(dequantizePosition(proto.pos_x()) * Constants::FLOAT_TO_FIXED);
    entity.position.y = static_cast<Constants::Fixed>(dequantizePosition(proto.pos_y()) * Constants::FLOAT_TO_FIXED);
    entity.position.z = static_cast<Constants::Fixed>(dequantizePosition(proto.pos_z()) * Constants::FLOAT_TO_FIXED);
    
    // Dequantize velocity
    entity.velocity.dx = static_cast<Constants::Fixed>(dequantizePosition(proto.vel_x()) * Constants::FLOAT_TO_FIXED);
    entity.velocity.dy = static_cast<Constants::Fixed>(dequantizePosition(proto.vel_y()) * Constants::FLOAT_TO_FIXED);
    entity.velocity.dz = static_cast<Constants::Fixed>(dequantizePosition(proto.vel_z()) * Constants::FLOAT_TO_FIXED);
    
    // Dequantize rotation
    entity.rotation.yaw = dequantizeRotation(proto.yaw());
    entity.rotation.pitch = dequantizeRotation(proto.pitch());
    
    entity.healthPercent = static_cast<uint8_t>(proto.health_percent());
    entity.animState = static_cast<uint8_t>(proto.anim_state());
    entity.teamId = static_cast<uint8_t>(proto.team_id());
    entity.timestamp = proto.changed_fields();
    
    return entity;
}

// ============================================================================
// Snapshot Serialization
// ============================================================================

std::vector<uint8_t> serializeSnapshot(uint32_t serverTick, uint32_t baselineTick,
                                       std::span<const EntityStateData> entities,
                                       std::span<const EntityID> removed) {
    NetworkProto::ServerSnapshot proto_snapshot;
    proto_snapshot.set_server_tick(serverTick);
    proto_snapshot.set_baseline_tick(baselineTick);
    
    // Add entity states
    for (const auto& entity : entities) {
        auto* proto_entity = proto_snapshot.add_entities();
        entityToProto(entity, proto_entity);
    }
    
    // Add removed entities
    for (EntityID id : removed) {
        proto_snapshot.add_removed_entities(static_cast<uint32_t>(id));
    }
    
    std::vector<uint8_t> data;
    data.resize(proto_snapshot.ByteSizeLong());
    proto_snapshot.SerializeToArray(data.data(), static_cast<int>(data.size()));
    return data;
}

bool deserializeSnapshot(std::span<const uint8_t> data,
                         std::vector<EntityStateData>& outEntities,
                         std::vector<EntityID>& outRemoved,
                         uint32_t& outServerTick,
                         uint32_t& outBaselineTick) {
    NetworkProto::ServerSnapshot proto_snapshot;
    if (!proto_snapshot.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return false;
    }
    
    outServerTick = proto_snapshot.server_tick();
    outBaselineTick = proto_snapshot.baseline_tick();
    
    outEntities.clear();
    for (const auto& proto_entity : proto_snapshot.entities()) {
        outEntities.push_back(protoToEntity(proto_entity));
    }
    
    outRemoved.clear();
    for (uint32_t id : proto_snapshot.removed_entities()) {
        outRemoved.push_back(static_cast<EntityID>(id));
    }
    
    return true;
}

// ============================================================================
// Delta Compression
// ============================================================================

std::vector<uint8_t> createDeltaSnapshot(
    uint32_t serverTick,
    uint32_t baselineTick,
    std::span<const EntityStateData> currentEntities,
    std::span<const EntityID> removedEntities,
    std::span<const EntityStateData> baselineEntities) {
    
    // If no baseline, send full state
    if (baselineEntities.empty()) {
        return serializeSnapshot(serverTick, baselineTick, currentEntities, removedEntities);
    }
    
    // Build lookup for baseline entities
    std::unordered_map<EntityID, const EntityStateData*> baselineLookup;
    for (const auto& entity : baselineEntities) {
        baselineLookup[entity.entity] = &entity;
    }
    
    // Find changed entities
    std::vector<EntityStateData> changedEntities;
    for (const auto& current : currentEntities) {
        auto it = baselineLookup.find(current.entity);
        if (it == baselineLookup.end()) {
            // New entity - send full state
            changedEntities.push_back(current);
        } else {
            // Existing entity - check what changed
            const auto* baseline = it->second;
            EntityStateData delta = current;
            delta.timestamp = 0;  // Will set changed fields
            
            uint16_t changed = 0;
            if (!current.equalsPosition(*baseline)) {
                changed |= DELTA_POSITION;
            } else {
                delta.position = baseline->position;  // Same, will be compressed
            }
            
            if (!current.equalsRotation(*baseline)) {
                changed |= DELTA_ROTATION;
            } else {
                delta.rotation = baseline->rotation;
            }
            
            if (!current.equalsVelocity(*baseline)) {
                changed |= DELTA_VELOCITY;
            } else {
                delta.velocity = baseline->velocity;
            }
            
            if (current.healthPercent != baseline->healthPercent) {
                changed |= DELTA_HEALTH;
            }
            
            if (current.animState != baseline->animState) {
                changed |= DELTA_ANIM_STATE;
            }
            
            if (changed != 0) {
                delta.timestamp = changed;  // Store changed fields mask
                changedEntities.push_back(delta);
            }
        }
    }
    
    return serializeSnapshot(serverTick, baselineTick, changedEntities, removedEntities);
}

bool applyDeltaSnapshot(
    std::span<const uint8_t> data,
    std::vector<EntityStateData>& outEntities,
    uint32_t& outServerTick,
    uint32_t& outBaselineTick,
    std::vector<EntityID>& outRemovedEntities) {
    
    return deserializeSnapshot(data, outEntities, outRemovedEntities, outServerTick, outBaselineTick);
}

// ============================================================================
// Server Correction Serialization
// ============================================================================

std::vector<uint8_t> serializeCorrection(
    uint32_t serverTick,
    const Position& position,
    const Velocity& velocity,
    uint32_t lastProcessedInput) {
    
    NetworkProto::ServerCorrection proto_correction;
    proto_correction.set_server_tick(serverTick);
    proto_correction.set_last_processed_input(lastProcessedInput);
    
    proto_correction.set_pos_x(quantizePosition(position.x * Constants::FIXED_TO_FLOAT));
    proto_correction.set_pos_y(quantizePosition(position.y * Constants::FIXED_TO_FLOAT));
    proto_correction.set_pos_z(quantizePosition(position.z * Constants::FIXED_TO_FLOAT));
    
    proto_correction.set_vel_x(quantizePosition(velocity.dx * Constants::FIXED_TO_FLOAT));
    proto_correction.set_vel_y(quantizePosition(velocity.dy * Constants::FIXED_TO_FLOAT));
    proto_correction.set_vel_z(quantizePosition(velocity.dz * Constants::FIXED_TO_FLOAT));
    
    std::vector<uint8_t> data;
    data.resize(proto_correction.ByteSizeLong());
    proto_correction.SerializeToArray(data.data(), static_cast<int>(data.size()));
    return data;
}

// ============================================================================
// Version Management
// ============================================================================

uint32_t getProtocolVersion() {
    return PROTOCOL_VERSION;
}

bool isVersionCompatible(uint32_t clientVersion) {
    // For now, require exact match on major version
    uint32_t clientMajor = clientVersion >> 16;
    uint32_t serverMajor = PROTOCOL_VERSION >> 16;
    return clientMajor == serverMajor;
}

} // namespace Protocol
} // namespace DarkAges
