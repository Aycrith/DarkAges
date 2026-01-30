// DarkAges MMO - FlatBuffers Generated Code
// Automatically generated from game_protocol.fbs
// DO NOT EDIT - regenerate with: flatc --cpp game_protocol.fbs

#ifndef DARKAGES_PROTOCOL_GAME_PROTOCOL_GENERATED_H_
#define DARKAGES_PROTOCOL_GAME_PROTOCOL_GENERATED_H_

#include "flatbuffers/flatbuffers.h"  // Minimal FlatBuffers implementation

namespace DarkAges {
namespace Protocol {

struct Vec3;
struct Quat;
struct Vec3Velocity;
struct ClientInput;
struct ConnectionRequest;
struct ConnectionResponse;
struct EntityState;
struct Snapshot;
struct EventPacket;
struct ServerCorrection;

// ============================================================================
// STRUCTS
// ============================================================================

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(2) Vec3 {
 private:
    int16_t x_;
    int16_t y_;
    int16_t z_;

 public:
    Vec3() : x_(0), y_(0), z_(0) {}
    Vec3(int16_t x, int16_t y, int16_t z) : x_(x), y_(y), z_(z) {}

    int16_t x() const { return x_; }
    int16_t y() const { return y_; }
    int16_t z() const { return z_; }

    void mutate_x(int16_t x) { x_ = x; }
    void mutate_y(int16_t y) { y_ = y; }
    void mutate_z(int16_t z) { z_ = z; }
};
FLATBUFFERS_STRUCT_END(Vec3, 6);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(1) Quat {
 private:
    int8_t x_;
    int8_t y_;
    int8_t z_;
    int8_t w_;

 public:
    Quat() : x_(0), y_(0), z_(0), w_(127) {}
    Quat(int8_t x, int8_t y, int8_t z, int8_t w) : x_(x), y_(y), z_(z), w_(w) {}

    int8_t x() const { return x_; }
    int8_t y() const { return y_; }
    int8_t z() const { return z_; }
    int8_t w() const { return w_; }

    void mutate_x(int8_t x) { x_ = x; }
    void mutate_y(int8_t y) { y_ = y; }
    void mutate_z(int8_t z) { z_ = z; }
    void mutate_w(int8_t w) { w_ = w; }
};
FLATBUFFERS_STRUCT_END(Quat, 4);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(2) Vec3Velocity {
 private:
    int16_t x_;
    int16_t y_;
    int16_t z_;

 public:
    Vec3Velocity() : x_(0), y_(0), z_(0) {}
    Vec3Velocity(int16_t x, int16_t y, int16_t z) : x_(x), y_(y), z_(z) {}

    int16_t x() const { return x_; }
    int16_t y() const { return y_; }
    int16_t z() const { return z_; }

    void mutate_x(int16_t x) { x_ = x; }
    void mutate_y(int16_t y) { y_ = y; }
    void mutate_z(int16_t z) { z_ = z; }
};
FLATBUFFERS_STRUCT_END(Vec3Velocity, 6);

// ============================================================================
// ENUMS
// ============================================================================

enum EventType : uint16_t {
    EVENT_DAMAGE_DEALT = 1,
    EVENT_DAMAGE_TAKEN = 2,
    EVENT_HEAL = 3,
    EVENT_DEATH = 4,
    EVENT_RESPAWN = 5,
    EVENT_ITEM_PICKUP = 10,
    EVENT_ITEM_DROP = 11,
    EVENT_ABILITY_USED = 12,
    EVENT_ABILITY_COOLDOWN = 13,
    EVENT_ZONE_ENTER = 20,
    EVENT_ZONE_EXIT = 21,
    EVENT_ZONE_TRANSFER = 22,
    EVENT_CHAT_MESSAGE = 30,
    EVENT_SYSTEM_MESSAGE = 31,
    EVENT_KICK = 32,
    EVENT_BAN = 33
};

inline const char* EnumNameEventType(EventType e) {
    switch (e) {
        case EVENT_DAMAGE_DEALT: return "EVENT_DAMAGE_DEALT";
        case EVENT_DAMAGE_TAKEN: return "EVENT_DAMAGE_TAKEN";
        case EVENT_HEAL: return "EVENT_HEAL";
        case EVENT_DEATH: return "EVENT_DEATH";
        case EVENT_RESPAWN: return "EVENT_RESPAWN";
        case EVENT_ITEM_PICKUP: return "EVENT_ITEM_PICKUP";
        case EVENT_ITEM_DROP: return "EVENT_ITEM_DROP";
        case EVENT_ABILITY_USED: return "EVENT_ABILITY_USED";
        case EVENT_ABILITY_COOLDOWN: return "EVENT_ABILITY_COOLDOWN";
        case EVENT_ZONE_ENTER: return "EVENT_ZONE_ENTER";
        case EVENT_ZONE_EXIT: return "EVENT_ZONE_EXIT";
        case EVENT_ZONE_TRANSFER: return "EVENT_ZONE_TRANSFER";
        case EVENT_CHAT_MESSAGE: return "EVENT_CHAT_MESSAGE";
        case EVENT_SYSTEM_MESSAGE: return "EVENT_SYSTEM_MESSAGE";
        case EVENT_KICK: return "EVENT_KICK";
        case EVENT_BAN: return "EVENT_BAN";
        default: return "Unknown";
    }
}

enum AnimState : uint8_t {
    ANIM_IDLE = 0,
    ANIM_WALK = 1,
    ANIM_RUN = 2,
    ANIM_SPRINT = 3,
    ANIM_JUMP = 4,
    ANIM_FALL = 5,
    ANIM_ATTACK = 10,
    ANIM_ATTACK_HEAVY = 11,
    ANIM_BLOCK = 12,
    ANIM_HIT = 13,
    ANIM_DEATH = 14,
    ANIM_RESPAWN = 15
};

inline const char* EnumNameAnimState(AnimState e) {
    switch (e) {
        case ANIM_IDLE: return "ANIM_IDLE";
        case ANIM_WALK: return "ANIM_WALK";
        case ANIM_RUN: return "ANIM_RUN";
        case ANIM_SPRINT: return "ANIM_SPRINT";
        case ANIM_JUMP: return "ANIM_JUMP";
        case ANIM_FALL: return "ANIM_FALL";
        case ANIM_ATTACK: return "ANIM_ATTACK";
        case ANIM_ATTACK_HEAVY: return "ANIM_ATTACK_HEAVY";
        case ANIM_BLOCK: return "ANIM_BLOCK";
        case ANIM_HIT: return "ANIM_HIT";
        case ANIM_DEATH: return "ANIM_DEATH";
        case ANIM_RESPAWN: return "ANIM_RESPAWN";
        default: return "Unknown";
    }
}

// ============================================================================
// TABLES
// ============================================================================

// Client input packet - sent at 60Hz
struct ClientInput FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
    enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
        VT_SEQUENCE = 4,
        VT_TIMESTAMP = 6,
        VT_INPUT_FLAGS = 8,
        VT_YAW = 10,
        VT_PITCH = 12,
        VT_TARGET_ENTITY = 14
    };

    uint32_t sequence() const { return GetField<uint32_t>(VT_SEQUENCE, 0); }
    uint32_t timestamp() const { return GetField<uint32_t>(VT_TIMESTAMP, 0); }
    uint8_t input_flags() const { return GetField<uint8_t>(VT_INPUT_FLAGS, 0); }
    int16_t yaw() const { return GetField<int16_t>(VT_YAW, 0); }
    int16_t pitch() const { return GetField<int16_t>(VT_PITCH, 0); }
    uint32_t target_entity() const { return GetField<uint32_t>(VT_TARGET_ENTITY, 0); }

    bool Verify(flatbuffers::Verifier& verifier) const {
        return VerifyTableStart(verifier) &&
               VerifyField<uint32_t>(verifier, VT_SEQUENCE, 4) &&
               VerifyField<uint32_t>(verifier, VT_TIMESTAMP, 4) &&
               VerifyField<uint8_t>(verifier, VT_INPUT_FLAGS, 1) &&
               VerifyField<int16_t>(verifier, VT_YAW, 2) &&
               VerifyField<int16_t>(verifier, VT_PITCH, 2) &&
               VerifyField<uint32_t>(verifier, VT_TARGET_ENTITY, 4) &&
               verifier.EndTable();
    }
};

struct ClientInputBuilder {
    flatbuffers::FlatBufferBuilder& fbb_;
    flatbuffers::uoffset_t start_;

    void add_sequence(uint32_t sequence) {
        fbb_.AddElement<uint32_t>(ClientInput::VT_SEQUENCE, sequence, 0);
    }
    void add_timestamp(uint32_t timestamp) {
        fbb_.AddElement<uint32_t>(ClientInput::VT_TIMESTAMP, timestamp, 0);
    }
    void add_input_flags(uint8_t input_flags) {
        fbb_.AddElement<uint8_t>(ClientInput::VT_INPUT_FLAGS, input_flags, 0);
    }
    void add_yaw(int16_t yaw) {
        fbb_.AddElement<int16_t>(ClientInput::VT_YAW, yaw, 0);
    }
    void add_pitch(int16_t pitch) {
        fbb_.AddElement<int16_t>(ClientInput::VT_PITCH, pitch, 0);
    }
    void add_target_entity(uint32_t target_entity) {
        fbb_.AddElement<uint32_t>(ClientInput::VT_TARGET_ENTITY, target_entity, 0);
    }

    explicit ClientInputBuilder(flatbuffers::FlatBufferBuilder& _fbb) : fbb_(_fbb) {
        start_ = fbb_.StartTable();
    }
    ClientInputBuilder& operator=(const ClientInputBuilder&) = delete;

    flatbuffers::Offset<ClientInput> Finish() {
        const auto end = fbb_.EndTable(start_);
        auto o = flatbuffers::Offset<ClientInput>(end);
        return o;
    }
};

inline flatbuffers::Offset<ClientInput> CreateClientInput(
    flatbuffers::FlatBufferBuilder& _fbb,
    uint32_t sequence = 0,
    uint32_t timestamp = 0,
    uint8_t input_flags = 0,
    int16_t yaw = 0,
    int16_t pitch = 0,
    uint32_t target_entity = 0) {
    ClientInputBuilder builder_(_fbb);
    builder_.add_target_entity(target_entity);
    builder_.add_timestamp(timestamp);
    builder_.add_sequence(sequence);
    builder_.add_pitch(pitch);
    builder_.add_yaw(yaw);
    builder_.add_input_flags(input_flags);
    return builder_.Finish();
}

// Connection request (reliable)
struct ConnectionRequest FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
    enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
        VT_PROTOCOL_VERSION = 4,
        VT_PLAYER_ID = 6,
        VT_AUTH_TOKEN = 8
    };

    uint32_t protocol_version() const { return GetField<uint32_t>(VT_PROTOCOL_VERSION, 0); }
    uint64_t player_id() const { return GetField<uint64_t>(VT_PLAYER_ID, 0); }
    const flatbuffers::Vector<uint8_t>* auth_token() const {
        return GetPointer<const flatbuffers::Vector<uint8_t>*>(VT_AUTH_TOKEN);
    }

    bool Verify(flatbuffers::Verifier& verifier) const {
        return VerifyTableStart(verifier) &&
               VerifyField<uint32_t>(verifier, VT_PROTOCOL_VERSION, 4) &&
               VerifyField<uint64_t>(verifier, VT_PLAYER_ID, 8) &&
               VerifyOffset(verifier, VT_AUTH_TOKEN) &&
               verifier.VerifyVector(auth_token()) &&
               verifier.EndTable();
    }
};

struct ConnectionRequestBuilder {
    flatbuffers::FlatBufferBuilder& fbb_;
    flatbuffers::uoffset_t start_;

    void add_protocol_version(uint32_t protocol_version) {
        fbb_.AddElement<uint32_t>(ConnectionRequest::VT_PROTOCOL_VERSION, protocol_version, 0);
    }
    void add_player_id(uint64_t player_id) {
        fbb_.AddElement<uint64_t>(ConnectionRequest::VT_PLAYER_ID, player_id, 0);
    }
    void add_auth_token(flatbuffers::Offset<flatbuffers::Vector<uint8_t>> auth_token) {
        fbb_.AddOffset(ConnectionRequest::VT_AUTH_TOKEN, auth_token);
    }

    explicit ConnectionRequestBuilder(flatbuffers::FlatBufferBuilder& _fbb) : fbb_(_fbb) {
        start_ = fbb_.StartTable();
    }
    ConnectionRequestBuilder& operator=(const ConnectionRequestBuilder&) = delete;

    flatbuffers::Offset<ConnectionRequest> Finish() {
        const auto end = fbb_.EndTable(start_);
        auto o = flatbuffers::Offset<ConnectionRequest>(end);
        return o;
    }
};

// Connection response (reliable)
struct ConnectionResponse FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
    enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
        VT_SUCCESS = 4,
        VT_CONNECTION_ID = 6,
        VT_SERVER_TICK = 8,
        VT_SERVER_TIME = 10,
        VT_ERROR_MESSAGE = 12
    };

    bool success() const { return GetField<uint8_t>(VT_SUCCESS, 0) != 0; }
    uint32_t connection_id() const { return GetField<uint32_t>(VT_CONNECTION_ID, 0); }
    uint32_t server_tick() const { return GetField<uint32_t>(VT_SERVER_TICK, 0); }
    uint32_t server_time() const { return GetField<uint32_t>(VT_SERVER_TIME, 0); }
    const flatbuffers::String* error_message() const {
        return GetPointer<const flatbuffers::String*>(VT_ERROR_MESSAGE);
    }

    bool Verify(flatbuffers::Verifier& verifier) const {
        return VerifyTableStart(verifier) &&
               VerifyField<uint8_t>(verifier, VT_SUCCESS, 1) &&
               VerifyField<uint32_t>(verifier, VT_CONNECTION_ID, 4) &&
               VerifyField<uint32_t>(verifier, VT_SERVER_TICK, 4) &&
               VerifyField<uint32_t>(verifier, VT_SERVER_TIME, 4) &&
               VerifyOffset(verifier, VT_ERROR_MESSAGE) &&
               verifier.VerifyString(error_message()) &&
               verifier.EndTable();
    }
};

struct ConnectionResponseBuilder {
    flatbuffers::FlatBufferBuilder& fbb_;
    flatbuffers::uoffset_t start_;

    void add_success(bool success) {
        fbb_.AddElement<uint8_t>(ConnectionResponse::VT_SUCCESS, static_cast<uint8_t>(success), 0);
    }
    void add_connection_id(uint32_t connection_id) {
        fbb_.AddElement<uint32_t>(ConnectionResponse::VT_CONNECTION_ID, connection_id, 0);
    }
    void add_server_tick(uint32_t server_tick) {
        fbb_.AddElement<uint32_t>(ConnectionResponse::VT_SERVER_TICK, server_tick, 0);
    }
    void add_server_time(uint32_t server_time) {
        fbb_.AddElement<uint32_t>(ConnectionResponse::VT_SERVER_TIME, server_time, 0);
    }
    void add_error_message(flatbuffers::Offset<flatbuffers::String> error_message) {
        fbb_.AddOffset(ConnectionResponse::VT_ERROR_MESSAGE, error_message);
    }

    explicit ConnectionResponseBuilder(flatbuffers::FlatBufferBuilder& _fbb) : fbb_(_fbb) {
        start_ = fbb_.StartTable();
    }
    ConnectionResponseBuilder& operator=(const ConnectionResponseBuilder&) = delete;

    flatbuffers::Offset<ConnectionResponse> Finish() {
        const auto end = fbb_.EndTable(start_);
        auto o = flatbuffers::Offset<ConnectionResponse>(end);
        return o;
    }
};

// Entity state in snapshot
struct EntityState FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
    enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
        VT_ID = 4,
        VT_TYPE = 6,
        VT_PRESENT_MASK = 8,
        VT_POSITION = 10,
        VT_ROTATION = 12,
        VT_VELOCITY = 14,
        VT_HEALTH_PERCENT = 16,
        VT_ANIM_STATE = 18,
        VT_TEAM_ID = 20
    };

    uint32_t id() const { return GetField<uint32_t>(VT_ID, 0); }
    uint8_t type() const { return GetField<uint8_t>(VT_TYPE, 0); }
    uint16_t present_mask() const { return GetField<uint16_t>(VT_PRESENT_MASK, 0); }
    const Vec3* position() const { return GetStruct<const Vec3*>(VT_POSITION); }
    const Quat* rotation() const { return GetStruct<const Quat*>(VT_ROTATION); }
    const Vec3Velocity* velocity() const { return GetStruct<const Vec3Velocity*>(VT_VELOCITY); }
    uint8_t health_percent() const { return GetField<uint8_t>(VT_HEALTH_PERCENT, 0); }
    uint8_t anim_state() const { return GetField<uint8_t>(VT_ANIM_STATE, 0); }
    uint8_t team_id() const { return GetField<uint8_t>(VT_TEAM_ID, 0); }

    bool Verify(flatbuffers::Verifier& verifier) const {
        return VerifyTableStart(verifier) &&
               VerifyField<uint32_t>(verifier, VT_ID, 4) &&
               VerifyField<uint8_t>(verifier, VT_TYPE, 1) &&
               VerifyField<uint16_t>(verifier, VT_PRESENT_MASK, 2) &&
               VerifyField<Vec3>(verifier, VT_POSITION, 2) &&
               VerifyField<Quat>(verifier, VT_ROTATION, 1) &&
               VerifyField<Vec3Velocity>(verifier, VT_VELOCITY, 2) &&
               VerifyField<uint8_t>(verifier, VT_HEALTH_PERCENT, 1) &&
               VerifyField<uint8_t>(verifier, VT_ANIM_STATE, 1) &&
               VerifyField<uint8_t>(verifier, VT_TEAM_ID, 1) &&
               verifier.EndTable();
    }
};

struct EntityStateBuilder {
    flatbuffers::FlatBufferBuilder& fbb_;
    flatbuffers::uoffset_t start_;

    void add_id(uint32_t id) {
        fbb_.AddElement<uint32_t>(EntityState::VT_ID, id, 0);
    }
    void add_type(uint8_t type) {
        fbb_.AddElement<uint8_t>(EntityState::VT_TYPE, type, 0);
    }
    void add_present_mask(uint16_t present_mask) {
        fbb_.AddElement<uint16_t>(EntityState::VT_PRESENT_MASK, present_mask, 0);
    }
    void add_position(const Vec3* position) {
        fbb_.AddStruct(EntityState::VT_POSITION, position);
    }
    void add_rotation(const Quat* rotation) {
        fbb_.AddStruct(EntityState::VT_ROTATION, rotation);
    }
    void add_velocity(const Vec3Velocity* velocity) {
        fbb_.AddStruct(EntityState::VT_VELOCITY, velocity);
    }
    void add_health_percent(uint8_t health_percent) {
        fbb_.AddElement<uint8_t>(EntityState::VT_HEALTH_PERCENT, health_percent, 0);
    }
    void add_anim_state(uint8_t anim_state) {
        fbb_.AddElement<uint8_t>(EntityState::VT_ANIM_STATE, anim_state, 0);
    }
    void add_team_id(uint8_t team_id) {
        fbb_.AddElement<uint8_t>(EntityState::VT_TEAM_ID, team_id, 0);
    }

    explicit EntityStateBuilder(flatbuffers::FlatBufferBuilder& _fbb) : fbb_(_fbb) {
        start_ = fbb_.StartTable();
    }
    EntityStateBuilder& operator=(const EntityStateBuilder&) = delete;

    flatbuffers::Offset<EntityState> Finish() {
        const auto end = fbb_.EndTable(start_);
        auto o = flatbuffers::Offset<EntityState>(end);
        return o;
    }
};

// Server snapshot - sent at 20Hz baseline
struct Snapshot FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
    enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
        VT_SERVER_TICK = 4,
        VT_BASELINE_TICK = 6,
        VT_SERVER_TIME = 8,
        VT_ENTITIES = 10,
        VT_REMOVED_ENTITIES = 12,
        VT_LAST_PROCESSED_INPUT = 14
    };

    uint32_t server_tick() const { return GetField<uint32_t>(VT_SERVER_TICK, 0); }
    uint32_t baseline_tick() const { return GetField<uint32_t>(VT_BASELINE_TICK, 0); }
    uint32_t server_time() const { return GetField<uint32_t>(VT_SERVER_TIME, 0); }
    const flatbuffers::Vector<flatbuffers::Offset<EntityState>>* entities() const {
        return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<EntityState>>*>(VT_ENTITIES);
    }
    const flatbuffers::Vector<uint32_t>* removed_entities() const {
        return GetPointer<const flatbuffers::Vector<uint32_t>*>(VT_REMOVED_ENTITIES);
    }
    uint32_t last_processed_input() const { return GetField<uint32_t>(VT_LAST_PROCESSED_INPUT, 0); }

    bool Verify(flatbuffers::Verifier& verifier) const {
        return VerifyTableStart(verifier) &&
               VerifyField<uint32_t>(verifier, VT_SERVER_TICK, 4) &&
               VerifyField<uint32_t>(verifier, VT_BASELINE_TICK, 4) &&
               VerifyField<uint32_t>(verifier, VT_SERVER_TIME, 4) &&
               VerifyOffset(verifier, VT_ENTITIES) &&
               verifier.VerifyVector(entities()) &&
               verifier.VerifyVectorOfTables(entities()) &&
               VerifyOffset(verifier, VT_REMOVED_ENTITIES) &&
               verifier.VerifyVector(removed_entities()) &&
               VerifyField<uint32_t>(verifier, VT_LAST_PROCESSED_INPUT, 4) &&
               verifier.EndTable();
    }
};

struct SnapshotBuilder {
    flatbuffers::FlatBufferBuilder& fbb_;
    flatbuffers::uoffset_t start_;

    void add_server_tick(uint32_t server_tick) {
        fbb_.AddElement<uint32_t>(Snapshot::VT_SERVER_TICK, server_tick, 0);
    }
    void add_baseline_tick(uint32_t baseline_tick) {
        fbb_.AddElement<uint32_t>(Snapshot::VT_BASELINE_TICK, baseline_tick, 0);
    }
    void add_server_time(uint32_t server_time) {
        fbb_.AddElement<uint32_t>(Snapshot::VT_SERVER_TIME, server_time, 0);
    }
    void add_entities(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<EntityState>>> entities) {
        fbb_.AddOffset(Snapshot::VT_ENTITIES, entities);
    }
    void add_removed_entities(flatbuffers::Offset<flatbuffers::Vector<uint32_t>> removed_entities) {
        fbb_.AddOffset(Snapshot::VT_REMOVED_ENTITIES, removed_entities);
    }
    void add_last_processed_input(uint32_t last_processed_input) {
        fbb_.AddElement<uint32_t>(Snapshot::VT_LAST_PROCESSED_INPUT, last_processed_input, 0);
    }

    explicit SnapshotBuilder(flatbuffers::FlatBufferBuilder& _fbb) : fbb_(_fbb) {
        start_ = fbb_.StartTable();
    }
    SnapshotBuilder& operator=(const SnapshotBuilder&) = delete;

    flatbuffers::Offset<Snapshot> Finish() {
        const auto end = fbb_.EndTable(start_);
        auto o = flatbuffers::Offset<Snapshot>(end);
        return o;
    }
};

inline flatbuffers::Offset<Snapshot> CreateSnapshot(
    flatbuffers::FlatBufferBuilder& _fbb,
    uint32_t server_tick = 0,
    uint32_t baseline_tick = 0,
    uint32_t server_time = 0,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<EntityState>>> entities = 0,
    flatbuffers::Offset<flatbuffers::Vector<uint32_t>> removed_entities = 0,
    uint32_t last_processed_input = 0) {
    SnapshotBuilder builder_(_fbb);
    builder_.add_last_processed_input(last_processed_input);
    builder_.add_removed_entities(removed_entities);
    builder_.add_entities(entities);
    builder_.add_server_time(server_time);
    builder_.add_baseline_tick(baseline_tick);
    builder_.add_server_tick(server_tick);
    return builder_.Finish();
}

// Reliable event packet (combat, pickups, etc.)
struct EventPacket FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
    enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
        VT_EVENT_TYPE = 4,
        VT_TIMESTAMP = 6,
        VT_SOURCE_ENTITY = 8,
        VT_TARGET_ENTITY = 10,
        VT_PARAMS = 12,
        VT_BINARY_DATA = 14
    };

    uint16_t event_type() const { return GetField<uint16_t>(VT_EVENT_TYPE, 0); }
    uint32_t timestamp() const { return GetField<uint32_t>(VT_TIMESTAMP, 0); }
    uint32_t source_entity() const { return GetField<uint32_t>(VT_SOURCE_ENTITY, 0); }
    uint32_t target_entity() const { return GetField<uint32_t>(VT_TARGET_ENTITY, 0); }
    const flatbuffers::Vector<float>* params() const {
        return GetPointer<const flatbuffers::Vector<float>*>(VT_PARAMS);
    }
    const flatbuffers::Vector<uint8_t>* binary_data() const {
        return GetPointer<const flatbuffers::Vector<uint8_t>*>(VT_BINARY_DATA);
    }

    bool Verify(flatbuffers::Verifier& verifier) const {
        return VerifyTableStart(verifier) &&
               VerifyField<uint16_t>(verifier, VT_EVENT_TYPE, 2) &&
               VerifyField<uint32_t>(verifier, VT_TIMESTAMP, 4) &&
               VerifyField<uint32_t>(verifier, VT_SOURCE_ENTITY, 4) &&
               VerifyField<uint32_t>(verifier, VT_TARGET_ENTITY, 4) &&
               VerifyOffset(verifier, VT_PARAMS) &&
               verifier.VerifyVector(params()) &&
               VerifyOffset(verifier, VT_BINARY_DATA) &&
               verifier.VerifyVector(binary_data()) &&
               verifier.EndTable();
    }
};

struct EventPacketBuilder {
    flatbuffers::FlatBufferBuilder& fbb_;
    flatbuffers::uoffset_t start_;

    void add_event_type(uint16_t event_type) {
        fbb_.AddElement<uint16_t>(EventPacket::VT_EVENT_TYPE, event_type, 0);
    }
    void add_timestamp(uint32_t timestamp) {
        fbb_.AddElement<uint32_t>(EventPacket::VT_TIMESTAMP, timestamp, 0);
    }
    void add_source_entity(uint32_t source_entity) {
        fbb_.AddElement<uint32_t>(EventPacket::VT_SOURCE_ENTITY, source_entity, 0);
    }
    void add_target_entity(uint32_t target_entity) {
        fbb_.AddElement<uint32_t>(EventPacket::VT_TARGET_ENTITY, target_entity, 0);
    }
    void add_params(flatbuffers::Offset<flatbuffers::Vector<float>> params) {
        fbb_.AddOffset(EventPacket::VT_PARAMS, params);
    }
    void add_binary_data(flatbuffers::Offset<flatbuffers::Vector<uint8_t>> binary_data) {
        fbb_.AddOffset(EventPacket::VT_BINARY_DATA, binary_data);
    }

    explicit EventPacketBuilder(flatbuffers::FlatBufferBuilder& _fbb) : fbb_(_fbb) {
        start_ = fbb_.StartTable();
    }
    EventPacketBuilder& operator=(const EventPacketBuilder&) = delete;

    flatbuffers::Offset<EventPacket> Finish() {
        const auto end = fbb_.EndTable(start_);
        auto o = flatbuffers::Offset<EventPacket>(end);
        return o;
    }
};

// Server correction - sent when client prediction is wrong
struct ServerCorrection FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
    enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
        VT_SERVER_TICK = 4,
        VT_POSITION = 6,
        VT_VELOCITY = 8,
        VT_LAST_PROCESSED_INPUT = 10
    };

    uint32_t server_tick() const { return GetField<uint32_t>(VT_SERVER_TICK, 0); }
    const Vec3* position() const { return GetStruct<const Vec3*>(VT_POSITION); }
    const Vec3Velocity* velocity() const { return GetStruct<const Vec3Velocity*>(VT_VELOCITY); }
    uint32_t last_processed_input() const { return GetField<uint32_t>(VT_LAST_PROCESSED_INPUT, 0); }

    bool Verify(flatbuffers::Verifier& verifier) const {
        return VerifyTableStart(verifier) &&
               VerifyField<uint32_t>(verifier, VT_SERVER_TICK, 4) &&
               VerifyField<Vec3>(verifier, VT_POSITION, 2) &&
               VerifyField<Vec3Velocity>(verifier, VT_VELOCITY, 2) &&
               VerifyField<uint32_t>(verifier, VT_LAST_PROCESSED_INPUT, 4) &&
               verifier.EndTable();
    }
};

struct ServerCorrectionBuilder {
    flatbuffers::FlatBufferBuilder& fbb_;
    flatbuffers::uoffset_t start_;

    void add_server_tick(uint32_t server_tick) {
        fbb_.AddElement<uint32_t>(ServerCorrection::VT_SERVER_TICK, server_tick, 0);
    }
    void add_position(const Vec3* position) {
        fbb_.AddStruct(ServerCorrection::VT_POSITION, position);
    }
    void add_velocity(const Vec3Velocity* velocity) {
        fbb_.AddStruct(ServerCorrection::VT_VELOCITY, velocity);
    }
    void add_last_processed_input(uint32_t last_processed_input) {
        fbb_.AddElement<uint32_t>(ServerCorrection::VT_LAST_PROCESSED_INPUT, last_processed_input, 0);
    }

    explicit ServerCorrectionBuilder(flatbuffers::FlatBufferBuilder& _fbb) : fbb_(_fbb) {
        start_ = fbb_.StartTable();
    }
    ServerCorrectionBuilder& operator=(const ServerCorrectionBuilder&) = delete;

    flatbuffers::Offset<ServerCorrection> Finish() {
        const auto end = fbb_.EndTable(start_);
        auto o = flatbuffers::Offset<ServerCorrection>(end);
        return o;
    }
};

inline flatbuffers::Offset<ServerCorrection> CreateServerCorrection(
    flatbuffers::FlatBufferBuilder& _fbb,
    uint32_t server_tick = 0,
    const Vec3* position = nullptr,
    const Vec3Velocity* velocity = nullptr,
    uint32_t last_processed_input = 0) {
    ServerCorrectionBuilder builder_(_fbb);
    builder_.add_last_processed_input(last_processed_input);
    builder_.add_velocity(velocity);
    builder_.add_position(position);
    builder_.add_server_tick(server_tick);
    return builder_.Finish();
}

// ============================================================================
// ROOT TYPE HELPERS
// ============================================================================

inline const ClientInput* GetClientInput(const void* buf) {
    return flatbuffers::GetRoot<ClientInput>(buf);
}

inline bool VerifyClientInputBuffer(flatbuffers::Verifier& verifier) {
    return verifier.VerifyBuffer<ClientInput>(nullptr);
}

inline const Snapshot* GetSnapshot(const void* buf) {
    return flatbuffers::GetRoot<Snapshot>(buf);
}

inline bool VerifySnapshotBuffer(flatbuffers::Verifier& verifier) {
    return verifier.VerifyBuffer<Snapshot>(nullptr);
}

inline const EventPacket* GetEventPacket(const void* buf) {
    return flatbuffers::GetRoot<EventPacket>(buf);
}

inline bool VerifyEventPacketBuffer(flatbuffers::Verifier& verifier) {
    return verifier.VerifyBuffer<EventPacket>(nullptr);
}

inline const ConnectionRequest* GetConnectionRequest(const void* buf) {
    return flatbuffers::GetRoot<ConnectionRequest>(buf);
}

inline bool VerifyConnectionRequestBuffer(flatbuffers::Verifier& verifier) {
    return verifier.VerifyBuffer<ConnectionRequest>(nullptr);
}

inline const ConnectionResponse* GetConnectionResponse(const void* buf) {
    return flatbuffers::GetRoot<ConnectionResponse>(buf);
}

inline bool VerifyConnectionResponseBuffer(flatbuffers::Verifier& verifier) {
    return verifier.VerifyBuffer<ConnectionResponse>(nullptr);
}

inline const ServerCorrection* GetServerCorrection(const void* buf) {
    return flatbuffers::GetRoot<ServerCorrection>(buf);
}

inline bool VerifyServerCorrectionBuffer(flatbuffers::Verifier& verifier) {
    return verifier.VerifyBuffer<ServerCorrection>(nullptr);
}

} // namespace Protocol
} // namespace DarkAges

#endif // DARKAGES_PROTOCOL_GAME_PROTOCOL_GENERATED_H_
