// [NETWORK_AGENT] FlatBuffers Protocol Implementation
// WP-6-4: FlatBuffers Protocol Implementation

#include "netcode/NetworkManager.hpp"
#include "proto/game_protocol_generated.h"
#include <cstring>
#include <algorithm>
#include <unordered_map>

namespace DarkAges {
namespace Protocol {

// ============================================================================
// CONSTANTS
// ============================================================================

// Quantization constants (must match schema)
constexpr float POSITION_QUANTIZE = 64.0f;       // ~1.5cm precision
constexpr float YAW_PITCH_QUANTIZE = 10000.0f;   // 0.0001 radian precision
constexpr float VELOCITY_QUANTIZE = 256.0f;      // ~0.004 m/s precision
constexpr float QUAT_QUANTIZE = 127.0f;          // -127..127 maps to -1.0..1.0

// Protocol versioning
constexpr uint32_t PROTOCOL_VERSION_MAJOR = 1;
constexpr uint32_t PROTOCOL_VERSION_MINOR = 0;
constexpr uint32_t PROTOCOL_VERSION = (PROTOCOL_VERSION_MAJOR << 16) | PROTOCOL_VERSION_MINOR;

// Delta compression constants
constexpr size_t MAX_BASELINES = 30;             // 30 ticks @ 20Hz = 1.5 seconds
// Note: DELTA_* constants are defined in NetworkManager.hpp

// ============================================================================
// INPUT SERIALIZATION
// ============================================================================

std::vector<uint8_t> serializeInput(const InputState& input) {
    // Pre-allocate buffer (typical ClientInput size ~32 bytes)
    flatbuffers::FlatBufferBuilder builder(64);
    
    // Quantize yaw and pitch to int16
    int16_t yawQuantized = static_cast<int16_t>(input.yaw * YAW_PITCH_QUANTIZE);
    int16_t pitchQuantized = static_cast<int16_t>(input.pitch * YAW_PITCH_QUANTIZE);
    
    // Pack input flags
    uint8_t inputFlags = 0;
    if (input.forward)  inputFlags |= 0x01;
    if (input.backward) inputFlags |= 0x02;
    if (input.left)     inputFlags |= 0x04;
    if (input.right)    inputFlags |= 0x08;
    if (input.jump)     inputFlags |= 0x10;
    if (input.attack)   inputFlags |= 0x20;
    if (input.block)    inputFlags |= 0x40;
    if (input.sprint)   inputFlags |= 0x80;
    
    // Build ClientInput table
    auto clientInput = DarkAges::Protocol::CreateClientInput(
        builder,
        input.sequence,
        input.timestamp_ms,
        inputFlags,
        yawQuantized,
        pitchQuantized,
        0  // target_entity - not used in basic input
    );
    
    builder.Finish(clientInput);
    
    // Copy to vector
    uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    return std::vector<uint8_t>(buf, buf + size);
}

bool deserializeInput(std::span<const uint8_t> data, InputState& outInput) {
    if (data.size() < sizeof(flatbuffers::uoffset_t)) {
        return false;  // Too small to be valid
    }
    
    // Verify buffer
    flatbuffers::Verifier verifier(data.data(), data.size());
    if (!DarkAges::Protocol::VerifyClientInputBuffer(verifier)) {
        return false;
    }
    
    // Parse
    auto input = DarkAges::Protocol::GetClientInput(data.data());
    if (!input) {
        return false;
    }
    
    // Extract input flags
    uint8_t flags = input->input_flags();
    outInput.forward  = (flags & 0x01) != 0;
    outInput.backward = (flags & 0x02) != 0;
    outInput.left     = (flags & 0x04) != 0;
    outInput.right    = (flags & 0x08) != 0;
    outInput.jump     = (flags & 0x10) != 0;
    outInput.attack   = (flags & 0x20) != 0;
    outInput.block    = (flags & 0x40) != 0;
    outInput.sprint   = (flags & 0x80) != 0;
    
    // Dequantize yaw/pitch
    outInput.yaw = input->yaw() / YAW_PITCH_QUANTIZE;
    outInput.pitch = input->pitch() / YAW_PITCH_QUANTIZE;
    
    // Metadata
    outInput.sequence = input->sequence();
    outInput.timestamp_ms = input->timestamp();
    
    return true;
}

// ============================================================================
// ENTITY STATE COMPARISON
// ============================================================================

bool EntityStateData::equalsPosition(const EntityStateData& other) const {
    // Compare with small epsilon for fixed-point values
    constexpr float EPSILON = 0.02f;  // ~2cm tolerance
    float dx = (position.x - other.position.x) * Constants::FIXED_TO_FLOAT;
    float dy = (position.y - other.position.y) * Constants::FIXED_TO_FLOAT;
    float dz = (position.z - other.position.z) * Constants::FIXED_TO_FLOAT;
    return (dx * dx + dy * dy + dz * dz) < (EPSILON * EPSILON);
}

bool EntityStateData::equalsRotation(const EntityStateData& other) const {
    // Compare rotation angles
    float yawDiff = std::abs(rotation.yaw - other.rotation.yaw);
    float pitchDiff = std::abs(rotation.pitch - other.rotation.pitch);
    // Handle yaw wrap-around
    if (yawDiff > 3.14159f) yawDiff = 6.28318f - yawDiff;
    return yawDiff < 0.01f && pitchDiff < 0.01f;
}

bool EntityStateData::equalsVelocity(const EntityStateData& other) const {
    // Compare velocity with tolerance
    constexpr float EPSILON = 0.05f;
    float dx = (velocity.dx - other.velocity.dx) * Constants::FIXED_TO_FLOAT;
    float dy = (velocity.dy - other.velocity.dy) * Constants::FIXED_TO_FLOAT;
    float dz = (velocity.dz - other.velocity.dz) * Constants::FIXED_TO_FLOAT;
    return (dx * dx + dy * dy + dz * dz) < (EPSILON * EPSILON);
}

// ============================================================================
// DELTA COMPRESSION
// ============================================================================

// Helper: Calculate which fields changed between two entity states
static uint16_t calculateFieldMask(
    const EntityStateData& current,
    const EntityStateData& baseline
) {
    uint16_t mask = 0;
    
    if (!current.equalsPosition(baseline)) {
        mask |= DELTA_POSITION;
    }
    if (!current.equalsRotation(baseline)) {
        mask |= DELTA_ROTATION;
    }
    if (!current.equalsVelocity(baseline)) {
        mask |= DELTA_VELOCITY;
    }
    if (current.healthPercent != baseline.healthPercent) {
        mask |= DELTA_HEALTH;
    }
    if (current.animState != baseline.animState) {
        mask |= DELTA_ANIM_STATE;
    }
    if (current.entityType != baseline.entityType) {
        mask |= DELTA_ENTITY_TYPE;
    }
    
    return mask;
}

// Helper: Build a FlatBuffers EntityState from EntityStateData
static flatbuffers::Offset<DarkAges::Protocol::EntityState>
buildEntityState(
    flatbuffers::FlatBufferBuilder& builder,
    const EntityStateData& data,
    uint16_t fieldMask
) {
    using namespace DarkAges::Protocol;
    
    // Calculate present mask - which fields are valid
    uint16_t presentMask = fieldMask;
    if (presentMask == 0) {
        presentMask = 0xFFFF;  // All fields present (new entity)
    }
    
    // Quantize position
    Vec3 pos(
        static_cast<int16_t>(data.position.x * Constants::FIXED_TO_FLOAT * POSITION_QUANTIZE),
        static_cast<int16_t>(data.position.y * Constants::FIXED_TO_FLOAT * POSITION_QUANTIZE),
        static_cast<int16_t>(data.position.z * Constants::FIXED_TO_FLOAT * POSITION_QUANTIZE)
    );
    
    // Quantize rotation to quaternion (simplified - just store yaw as quaternion around Y)
    // Full implementation would convert Euler angles to quaternion
    float halfYaw = data.rotation.yaw * 0.5f;
    float cy = std::cos(halfYaw);
    float sy = std::sin(halfYaw);
    Quat rot(
        static_cast<int8_t>(0),  // x
        static_cast<int8_t>(sy * QUAT_QUANTIZE),  // y
        static_cast<int8_t>(0),  // z
        static_cast<int8_t>(cy * QUAT_QUANTIZE)   // w
    );
    
    // Quantize velocity
    Vec3Velocity vel(
        static_cast<int16_t>(data.velocity.dx * Constants::FIXED_TO_FLOAT * VELOCITY_QUANTIZE),
        static_cast<int16_t>(data.velocity.dy * Constants::FIXED_TO_FLOAT * VELOCITY_QUANTIZE),
        static_cast<int16_t>(data.velocity.dz * Constants::FIXED_TO_FLOAT * VELOCITY_QUANTIZE)
    );
    
    // Use EntityStateBuilder to create the entity state
    EntityStateBuilder es_builder(builder);
    es_builder.add_id(static_cast<uint32_t>(data.entity));
    es_builder.add_type(data.entityType);
    es_builder.add_present_mask(presentMask);
    es_builder.add_position(&pos);
    es_builder.add_rotation(&rot);
    es_builder.add_velocity(&vel);
    es_builder.add_health_percent(data.healthPercent);
    es_builder.add_anim_state(data.animState);
    es_builder.add_team_id(0);  // team_id - not in EntityStateData yet
    return es_builder.Finish();
}

std::vector<uint8_t> createDeltaSnapshot(
    uint32_t serverTick,
    uint32_t baselineTick,
    std::span<const EntityStateData> currentEntities,
    std::span<const EntityID> removedEntities,
    std::span<const EntityStateData> baselineEntities
) {
    // Pre-allocate builder (estimate: 100 bytes header + 50 bytes per entity)
    flatbuffers::FlatBufferBuilder builder(1024 + currentEntities.size() * 64);
    
    // Build entity state vectors
    std::vector<flatbuffers::Offset<DarkAges::Protocol::EntityState>> entityOffsets;
    entityOffsets.reserve(currentEntities.size());
    
    // Create lookup for baseline entities
    std::unordered_map<EntityID, const EntityStateData*> baselineMap;
    for (const auto& baseline : baselineEntities) {
        baselineMap[baseline.entity] = &baseline;
    }
    
    // Build entity states with delta compression
    for (const auto& current : currentEntities) {
        uint16_t fieldMask = DELTA_NEW_ENTITY;  // Default: send all fields
        
        // Check if entity exists in baseline
        auto it = baselineMap.find(current.entity);
        if (it != baselineMap.end()) {
            // Calculate actual delta
            fieldMask = calculateFieldMask(current, *it->second);
            
            // Skip if no changes
            if (fieldMask == 0) {
                continue;
            }
        }
        
        auto entityOffset = buildEntityState(builder, current, fieldMask);
        entityOffsets.push_back(entityOffset);
    }
    
    // Create removed entities vector
    std::vector<uint32_t> removedIds;
    removedIds.reserve(removedEntities.size());
    for (const auto& id : removedEntities) {
        removedIds.push_back(static_cast<uint32_t>(id));
    }
    
    // Create vectors in buffer
    auto entitiesVector = builder.CreateVector(entityOffsets);
    auto removedVector = removedIds.empty() 
        ? flatbuffers::Offset<flatbuffers::Vector<uint32_t>>{0}
        : builder.CreateVector(removedIds);
    
    // Build snapshot
    auto snapshot = DarkAges::Protocol::CreateSnapshot(
        builder,
        serverTick,
        baselineTick,
        0,  // server_time - not used in delta snapshots
        entitiesVector,
        removedVector,
        0   // last_processed_input - set by caller
    );
    
    builder.Finish(snapshot);
    
    // Copy to output vector
    uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    return std::vector<uint8_t>(buf, buf + size);
}

bool applyDeltaSnapshot(
    std::span<const uint8_t> data,
    std::vector<EntityStateData>& outEntities,
    uint32_t& outServerTick,
    uint32_t& outBaselineTick,
    std::vector<EntityID>& outRemovedEntities
) {
    if (data.size() < sizeof(flatbuffers::uoffset_t)) {
        return false;
    }
    
    // Verify buffer
    flatbuffers::Verifier verifier(data.data(), data.size());
    if (!DarkAges::Protocol::VerifySnapshotBuffer(verifier)) {
        return false;
    }
    
    // Parse snapshot
    auto snapshot = DarkAges::Protocol::GetSnapshot(data.data());
    if (!snapshot) {
        return false;
    }
    
    outServerTick = snapshot->server_tick();
    outBaselineTick = snapshot->baseline_tick();
    
    outEntities.clear();
    
    // Process entities
    auto entities = snapshot->entities();
    if (entities) {
        outEntities.reserve(entities->size());
        
        for (const auto* fbEntity : *entities) {
            if (!fbEntity) continue;
            
            EntityStateData data;
            data.entity = static_cast<EntityID>(fbEntity->id());
            data.entityType = fbEntity->type();
            
            uint16_t presentMask = fbEntity->present_mask();
            
            // Extract position if present
            auto pos = fbEntity->position();
            if (pos && (presentMask & DELTA_POSITION || presentMask == DELTA_NEW_ENTITY)) {
                data.position.x = static_cast<Constants::Fixed>(pos->x() / POSITION_QUANTIZE * Constants::FLOAT_TO_FIXED);
                data.position.y = static_cast<Constants::Fixed>(pos->y() / POSITION_QUANTIZE * Constants::FLOAT_TO_FIXED);
                data.position.z = static_cast<Constants::Fixed>(pos->z() / POSITION_QUANTIZE * Constants::FLOAT_TO_FIXED);
            }
            
            // Extract rotation if present
            auto rot = fbEntity->rotation();
            if (rot && (presentMask & DELTA_ROTATION || presentMask == DELTA_NEW_ENTITY)) {
                // Convert quaternion back to yaw (simplified)
                float y = rot->y() / QUAT_QUANTIZE;
                float w = rot->w() / QUAT_QUANTIZE;
                data.rotation.yaw = std::atan2(2.0f * y * w, 1.0f - 2.0f * y * y);
                data.rotation.pitch = 0.0f;  // Not stored in simplified format
            }
            
            // Extract velocity if present
            auto vel = fbEntity->velocity();
            if (vel && (presentMask & DELTA_VELOCITY || presentMask == DELTA_NEW_ENTITY)) {
                data.velocity.dx = static_cast<Constants::Fixed>(vel->x() / VELOCITY_QUANTIZE * Constants::FLOAT_TO_FIXED);
                data.velocity.dy = static_cast<Constants::Fixed>(vel->y() / VELOCITY_QUANTIZE * Constants::FLOAT_TO_FIXED);
                data.velocity.dz = static_cast<Constants::Fixed>(vel->z() / VELOCITY_QUANTIZE * Constants::FLOAT_TO_FIXED);
            }
            
            // Extract other fields
            if (presentMask & DELTA_HEALTH || presentMask == DELTA_NEW_ENTITY) {
                data.healthPercent = fbEntity->health_percent();
            }
            if (presentMask & DELTA_ANIM_STATE || presentMask == DELTA_NEW_ENTITY) {
                data.animState = fbEntity->anim_state();
            }
            
            outEntities.push_back(data);
        }
    }
    
    // Process removed entities
    outRemovedEntities.clear();
    auto removed = snapshot->removed_entities();
    if (removed) {
        outRemovedEntities.reserve(removed->size());
        for (uint32_t id : *removed) {
            outRemovedEntities.push_back(static_cast<EntityID>(id));
        }
    }
    
    return true;
}

// ============================================================================
// SERVER CORRECTION
// ============================================================================

std::vector<uint8_t> serializeCorrection(
    uint32_t serverTick,
    const Position& position,
    const Velocity& velocity,
    uint32_t lastProcessedInput
) {
    flatbuffers::FlatBufferBuilder builder(64);
    
    // Quantize position
    DarkAges::Protocol::Vec3 pos(
        static_cast<int16_t>(position.x * Constants::FIXED_TO_FLOAT * POSITION_QUANTIZE),
        static_cast<int16_t>(position.y * Constants::FIXED_TO_FLOAT * POSITION_QUANTIZE),
        static_cast<int16_t>(position.z * Constants::FIXED_TO_FLOAT * POSITION_QUANTIZE)
    );
    
    // Quantize velocity
    DarkAges::Protocol::Vec3Velocity vel(
        static_cast<int16_t>(velocity.dx * Constants::FIXED_TO_FLOAT * VELOCITY_QUANTIZE),
        static_cast<int16_t>(velocity.dy * Constants::FIXED_TO_FLOAT * VELOCITY_QUANTIZE),
        static_cast<int16_t>(velocity.dz * Constants::FIXED_TO_FLOAT * VELOCITY_QUANTIZE)
    );
    
    auto correction = DarkAges::Protocol::CreateServerCorrection(
        builder,
        serverTick,
        &pos,
        &vel,
        lastProcessedInput
    );
    
    builder.Finish(correction);
    
    uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    return std::vector<uint8_t>(buf, buf + size);
}

// ============================================================================
// VERSIONING
// ============================================================================

uint32_t getProtocolVersion() {
    return PROTOCOL_VERSION;
}

bool isVersionCompatible(uint32_t clientVersion) {
    uint16_t clientMajor = static_cast<uint16_t>(clientVersion >> 16);
    uint16_t serverMajor = static_cast<uint16_t>(PROTOCOL_VERSION >> 16);
    
    // Major version must match for compatibility
    return clientMajor == serverMajor;
}

} // namespace Protocol
} // namespace DarkAges
