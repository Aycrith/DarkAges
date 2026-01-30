#pragma once

#include "ecs/CoreTypes.hpp"
#include "security/DDoSProtection.hpp"
#include <vector>
#include <functional>
#include <cstdint>
#include <span>
#include <optional>
#include <unordered_map>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>

// [NETWORK_AGENT] GameNetworkingSockets wrapper
// Handles UDP connections, reliable/unreliable channels, and connection quality

namespace DarkAges {

// Forward declarations for GNS types (to avoid header dependency)
struct GNSInternal;

// Connection handle
using ConnectionID = uint32_t;
static constexpr ConnectionID INVALID_CONNECTION = 0;

// Packet types
enum class PacketType : uint8_t {
    ClientInput = 1,      // Client -> Server: movement/actions
    ServerSnapshot = 2,   // Server -> Client: world state
    ReliableEvent = 3,    // Bidirectional: combat, pickups, etc.
    Ping = 4,             // Bidirectional: latency measurement
    Handshake = 5,        // Connection setup
    Disconnect = 6,       // Graceful disconnect
};

// Connection quality metrics
struct ConnectionQuality {
    uint32_t rttMs{0};
    float packetLoss{0.0f};
    float jitterMs{0.0f};
    uint32_t packetsSent{0};
    uint32_t packetsReceived{0};
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
};

// Client input packet (from network)
struct ClientInputPacket {
    ConnectionID connectionId{INVALID_CONNECTION};
    InputState input;
    uint32_t receiveTimeMs{0};
};

// Snapshot packet for serialization
struct SnapshotPacket {
    uint32_t serverTick{0};
    uint32_t baselineTick{0};
    std::vector<uint8_t> data;  // Serialized entity states
};

// [NETWORK_AGENT] Main network manager interface
class NetworkManager {
public:
    using ConnectionCallback = std::function<void(ConnectionID)>;
    using InputCallback = std::function<void(const ClientInputPacket&)>;
    using SnapshotCallback = std::function<void(ConnectionID, std::span<const uint8_t>)>;

public:
    NetworkManager();
    ~NetworkManager();
    
    // Initialize on port, return true if success
    bool initialize(uint16_t port = Constants::DEFAULT_SERVER_PORT);
    
    // Shutdown and cleanup
    void shutdown();
    
    // Non-blocking update - call every tick
    void update(uint32_t currentTimeMs);
    
    // Send unreliable snapshot to client (position updates)
    void sendSnapshot(ConnectionID connectionId, std::span<const uint8_t> data);
    
    // Send reliable event to client (combat, important state)
    void sendEvent(ConnectionID connectionId, std::span<const uint8_t> data);
    
    // Broadcast to all connected clients
    void broadcastSnapshot(std::span<const uint8_t> data);
    void broadcastEvent(std::span<const uint8_t> data);
    
    // Get pending inputs (call after update())
    [[nodiscard]] std::vector<ClientInputPacket> getPendingInputs();
    
    // Connection management
    void disconnect(ConnectionID connectionId, const char* reason = nullptr);
    [[nodiscard]] bool isConnected(ConnectionID connectionId) const;
    [[nodiscard]] ConnectionQuality getConnectionQuality(ConnectionID connectionId) const;
    
    // Callbacks
    void setOnClientConnected(ConnectionCallback callback) { onConnected_ = std::move(callback); }
    void setOnClientDisconnected(ConnectionCallback callback) { onDisconnected_ = std::move(callback); }
    void setOnInputReceived(InputCallback callback) { onInput_ = std::move(callback); }

    // Statistics
    [[nodiscard]] size_t getConnectionCount() const;
    [[nodiscard]] uint64_t getTotalBytesSent() const;
    [[nodiscard]] uint64_t getTotalBytesReceived() const;
    
    // Rate limiting check
    [[nodiscard]] bool isRateLimited(ConnectionID connectionId) const;
    
    // [SECURITY_AGENT] DDoS protection integration
    [[nodiscard]] Security::DDoSProtection& getDDoSProtection() { return ddosProtection_; }
    [[nodiscard]] const Security::DDoSProtection& getDDoSProtection() const { return ddosProtection_; }
    
    // Check if connection should be accepted (DDoS protection)
    [[nodiscard]] bool shouldAcceptConnection(const std::string& ipAddress);
    
    // Process packet with DDoS protection
    [[nodiscard]] bool processPacket(ConnectionID connectionId, 
                                     const std::string& ipAddress,
                                     uint32_t packetSize,
                                     uint32_t currentTimeMs);

private:
    std::unique_ptr<GNSInternal> internal_;
    Security::DDoSProtection ddosProtection_;
    
    ConnectionCallback onConnected_;
    ConnectionCallback onDisconnected_;
    InputCallback onInput_;
    
    std::vector<ClientInputPacket> pendingInputs_;
    
    bool initialized_{false};
};

// [NETWORK_AGENT] Protocol serialization helpers
namespace Protocol {
    // Serialize input state to binary
    std::vector<uint8_t> serializeInput(const InputState& input);
    
    // Deserialize input state from binary
    bool deserializeInput(std::span<const uint8_t> data, InputState& outInput);
    
    // Serialize entity state for snapshot
    struct EntityStateData {
        EntityID entity;
        Position position;
        Velocity velocity;
        Rotation rotation;
        uint8_t healthPercent{0};
        uint8_t animState{0};
        uint8_t entityType{0};  // 0=player, 1=projectile, 2=loot, 3=NPC
        uint32_t timestamp{0};  // For lag compensation and reconciliation
        
        // Equality comparison for delta detection
        [[nodiscard]] bool equalsPosition(const EntityStateData& other) const;
        [[nodiscard]] bool equalsRotation(const EntityStateData& other) const;
        [[nodiscard]] bool equalsVelocity(const EntityStateData& other) const;
    };
    
    // Delta entity state - only includes changed fields
    struct DeltaEntityState {
        EntityID entity;
        uint16_t changedFields;  // Bit mask of changed fields
        Position position;       // Only valid if bit 0 set
        Rotation rotation;       // Only valid if bit 1 set
        Velocity velocity;       // Only valid if bit 2 set
        uint8_t healthPercent;   // Only valid if bit 3 set
        uint8_t animState;       // Only valid if bit 4 set
        uint8_t entityType;      // Only valid if bit 5 set (for new entities)
    };
    
    // Bit masks for changedFields
    enum DeltaFieldMask : uint16_t {
        DELTA_POSITION = 1 << 0,
        DELTA_ROTATION = 1 << 1,
        DELTA_VELOCITY = 1 << 2,
        DELTA_HEALTH = 1 << 3,
        DELTA_ANIM_STATE = 1 << 4,
        DELTA_ENTITY_TYPE = 1 << 5,
        DELTA_NEW_ENTITY = 0xFFFF  // All fields for new entities
    };
    
    // Delta compression for snapshots
    // If baselineEntities is empty, sends full state for all entities
    std::vector<uint8_t> createDeltaSnapshot(
        uint32_t serverTick,
        uint32_t baselineTick,
        std::span<const EntityStateData> currentEntities,
        std::span<const EntityID> removedEntities,
        std::span<const EntityStateData> baselineEntities = {}
    );
    
    // Apply delta snapshot - reconstructs full state from baseline + delta
    // Returns true on success, false if baseline mismatch
    bool applyDeltaSnapshot(
        std::span<const uint8_t> data,
        std::vector<EntityStateData>& outEntities,
        uint32_t& outServerTick,
        uint32_t& outBaselineTick,
        std::vector<EntityID>& outRemovedEntities
    );
    
    // Position delta encoding helpers
    namespace DeltaEncoding {
        // Encode a position delta using variable-length encoding
        // Returns bytes written (1, 3, or 6 bytes per component based on delta magnitude)
        size_t encodePositionDelta(uint8_t* buffer, size_t bufferSize,
                                   const Position& current, const Position& baseline);
        
        // Decode a position delta
        // Returns bytes read
        size_t decodePositionDelta(const uint8_t* buffer, size_t bufferSize,
                                   Position& outPosition, const Position& baseline);
        
        // Thresholds for variable-length encoding (in fixed-point units)
        inline constexpr int32_t SMALL_DELTA_THRESHOLD = 127;      // Fits in int8
        inline constexpr int32_t MEDIUM_DELTA_THRESHOLD = 32767;   // Fits in int16
    }
    
    // [NETWORK_AGENT] Serialize server correction for client reconciliation
    // Sent when server detects client misprediction
    std::vector<uint8_t> serializeCorrection(
        uint32_t serverTick,
        const Position& position,
        const Velocity& velocity,
        uint32_t lastProcessedInput
    );
}

} // namespace DarkAges
