#pragma once

// [NETWORK_AGENT] Protobuf Protocol Wrapper for DarkAges MMO
// Wraps the generated protobuf code for clean integration with NetworkManager
// WP-8-6: GameNetworkingSockets Protobuf Integration

#include "ecs/CoreTypes.hpp"
#include <vector>
#include <cstdint>
#include <span>
#include <optional>

// Include generated protobuf headers
#include "network_protocol.pb.h"

namespace DarkAges {
// Alias protobuf namespace for convenience
namespace NetworkProto = DarkAges::Network;

namespace Netcode {

// [NETWORK_AGENT] Protobuf-based protocol serialization
// Replaces manual binary packing with type-safe protobuf messages
class ProtobufProtocol {
public:
    // =========================================================================
    // Client Input Serialization (Client -> Server)
    // =========================================================================
    
    // Serialize client input to protobuf binary format
    static std::vector<uint8_t> serializeClientInput(
        uint32_t sequence,
        uint32_t timestamp,
        uint32_t inputFlags,
        float yaw,
        float pitch,
        uint32_t targetEntity = 0
    );
    
    // Deserialize client input from protobuf binary
    static std::optional<NetworkProto::ClientInput> deserializeClientInput(
        std::span<const uint8_t> data
    );
    
    // Convert from our InputState to protobuf ClientInput
    static NetworkProto::ClientInput inputStateToProto(
        const InputState& input,
        uint32_t sequence
    );
    
    // Convert from protobuf ClientInput to our InputState
    static InputState protoToInputState(const NetworkProto::ClientInput& proto);

    // =========================================================================
    // Server Snapshot Serialization (Server -> Client)
    // =========================================================================
    
    // Build a server snapshot protobuf message
    static NetworkProto::ServerSnapshot buildServerSnapshot(
        uint32_t serverTick,
        uint32_t baselineTick
    );
    
    // Add entity state to snapshot
    static void addEntityToSnapshot(
        NetworkProto::ServerSnapshot& snapshot,
        const EntityState& entity
    );
    
    // Serialize snapshot to binary
    static std::vector<uint8_t> serializeSnapshot(
        const NetworkProto::ServerSnapshot& snapshot
    );
    
    // Deserialize snapshot from binary
    static std::optional<NetworkProto::ServerSnapshot> deserializeSnapshot(
        std::span<const uint8_t> data
    );

    // =========================================================================
    // Reliable Event Serialization (Bidirectional)
    // =========================================================================
    
    // Create a damage event
    static NetworkProto::ReliableEvent createDamageEvent(
        uint32_t sourceEntity,
        uint32_t targetEntity,
        int32_t damage,
        uint32_t abilityId = 0
    );
    
    // Create a player death event
    static NetworkProto::ReliableEvent createPlayerDeathEvent(
        uint32_t victimEntityId,
        uint32_t killerEntityId = 0
    );
    
    // Serialize event to binary
    static std::vector<uint8_t> serializeEvent(
        const NetworkProto::ReliableEvent& event
    );
    
    // Deserialize event from binary
    static std::optional<NetworkProto::ReliableEvent> deserializeEvent(
        std::span<const uint8_t> data
    );

    // =========================================================================
    // Handshake Messages
    // =========================================================================
    
    // Create handshake request (Client -> Server)
    static NetworkProto::Handshake createHandshake(
        uint32_t protocolVersion,
        uint32_t clientVersion,
        const std::string& authToken,
        const std::string& username
    );
    
    // Create handshake response (Server -> Client)
    static NetworkProto::HandshakeResponse createHandshakeResponse(
        bool accepted,
        uint32_t serverTick,
        uint32_t entityId,
        float spawnX, float spawnY, float spawnZ,
        const std::string& rejectReason = ""
    );

    // =========================================================================
    // Ping/Pong (Latency Measurement)
    // =========================================================================
    
    static std::vector<uint8_t> serializePing(uint32_t clientTimestamp);
    static std::vector<uint8_t> serializePong(
        uint32_t clientTimestamp, 
        uint32_t serverTimestamp
    );

    // =========================================================================
    // Top-Level Message Wrapper (oneof-based wrapping)
    // =========================================================================

    // Wrap a ClientInput into a NetworkMessage envelope
    static std::vector<uint8_t> wrapClientInput(
        const NetworkProto::ClientInput& input,
        uint32_t sequence = 0
    );

    // Wrap a ReliableEvent into a NetworkMessage envelope
    static std::vector<uint8_t> wrapReliableEvent(
        const NetworkProto::ReliableEvent& event,
        uint32_t sequence = 0
    );

    // Wrap a Handshake into a NetworkMessage envelope
    static std::vector<uint8_t> wrapHandshake(
        const NetworkProto::Handshake& handshake,
        uint32_t sequence = 0
    );

    // Wrap a HandshakeResponse into a NetworkMessage envelope
    static std::vector<uint8_t> wrapHandshakeResponse(
        const NetworkProto::HandshakeResponse& response,
        uint32_t sequence = 0
    );

    // Wrap a ServerSnapshot into a NetworkMessage envelope
    static std::vector<uint8_t> wrapServerSnapshot(
        const NetworkProto::ServerSnapshot& snapshot,
        uint32_t sequence = 0
    );

    // Wrap a ServerCorrection into a NetworkMessage envelope
    static std::vector<uint8_t> wrapServerCorrection(
        const NetworkProto::ServerCorrection& correction,
        uint32_t sequence = 0
    );

    // Wrap a ServerConfig into a NetworkMessage envelope
    static std::vector<uint8_t> wrapServerConfig(
        const NetworkProto::ServerConfig& config,
        uint32_t sequence = 0
    );

    // Wrap a Ping into a NetworkMessage envelope
    static std::vector<uint8_t> wrapPing(
        const NetworkProto::Ping& ping,
        uint32_t sequence = 0
    );

    // Wrap a Pong into a NetworkMessage envelope
    static std::vector<uint8_t> wrapPong(
        const NetworkProto::Pong& pong,
        uint32_t sequence = 0
    );

    // Wrap a Disconnect into a NetworkMessage envelope
    static std::vector<uint8_t> wrapDisconnect(
        const NetworkProto::Disconnect& disconnect,
        uint32_t sequence = 0
    );

    // Generic: wrap any NetworkMessage and serialize to bytes
    static std::vector<uint8_t> serializeNetworkMessage(
        const NetworkProto::NetworkMessage& msg
    );

    // Unwrap a NetworkMessage envelope
    static std::optional<NetworkProto::NetworkMessage> unwrapMessage(
        std::span<const uint8_t> data
    );

    // Extract payload type from a NetworkMessage
    static NetworkProto::NetworkMessage::MessageType getPayloadType(
        const NetworkProto::NetworkMessage& msg
    );

    // =========================================================================
    // Entity State Helpers
    // =========================================================================
    
    // Convert our EntityState to protobuf EntityState
    static NetworkProto::EntityState entityStateToProto(
        const EntityState& entity
    );
    
    // Convert protobuf EntityState to our EntityState
    static EntityState protoToEntityState(
        const NetworkProto::EntityState& proto
    );

    // =========================================================================
    // Bandwidth Optimization
    // =========================================================================
    
    // Calculate serialized size (for bandwidth monitoring)
    static size_t calculateSize(const google::protobuf::Message& message);
    
    // Get human-readable type name for debugging
    static const char* getMessageTypeName(
        NetworkProto::NetworkMessage::MessageType type
    );

private:
    // Private constructor - static utility class
    ProtobufProtocol() = delete;
    ~ProtobufProtocol() = delete;
};

} // namespace Netcode
} // namespace DarkAges
