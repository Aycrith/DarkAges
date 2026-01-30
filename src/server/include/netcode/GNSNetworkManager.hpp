#pragma once

// [NETWORK_AGENT] GameNetworkingSockets implementation
// Production-ready networking with encryption, reliable channels, and NAT traversal

#include "ecs/CoreTypes.hpp"
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>

// Forward declarations for GNS
struct ISteamNetworkingSockets;
struct ISteamNetworkingUtils;
struct SteamNetConnectionStatusChangedCallback_t;
struct SteamNetworkingIPAddr;
struct SteamNetworkingIdentity;

namespace DarkAges {
namespace Netcode {

// Connection state
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting
};

// Network statistics for a connection
struct ConnectionStats {
    float ping_ms = 0.0f;
    float packet_loss_percent = 0.0f;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint32_t packets_sent = 0;
    uint32_t packets_received = 0;
    float send_rate_kbps = 0.0f;
    float recv_rate_kbps = 0.0f;
};

// Pending client input
struct ClientInputPacket {
    ConnectionID connectionId;
    std::vector<uint8_t> data;
    uint32_t sequence;
    uint64_t receiveTimeMs;
};

// Configuration for GNS
struct GNSConfig {
    uint16_t port = 7777;
    uint32_t maxConnections = 1000;
    bool enableEncryption = true;
    bool enableNATPunchthrough = true;
    
    // Bandwidth limits (bytes/sec)
    uint32_t maxSendRate = 256 * 1024;    // 256 KB/s per client
    uint32_t maxRecvRate = 64 * 1024;     // 64 KB/s per client
    
    // Timeouts
    uint32_t connectionTimeoutMs = 10000;  // 10 seconds
    uint32_t heartbeatIntervalMs = 500;    // 500ms
};

// [NETWORK_AGENT] GameNetworkingSockets-based network manager
class GNSNetworkManager {
public:
    GNSNetworkManager();
    ~GNSNetworkManager();
    
    // Non-copyable
    GNSNetworkManager(const GNSNetworkManager&) = delete;
    GNSNetworkManager& operator=(const GNSNetworkManager&) = delete;
    
    // Initialize/shutdown
    bool initialize(const GNSConfig& config);
    bool initialize(uint16_t port); // Simple init with defaults
    void shutdown();
    
    // Update - call every tick
    void update(uint32_t currentTimeMs);
    
    // Send messages
    void sendToClient(ConnectionID connectionId, const void* data, size_t size, bool reliable = false);
    void broadcast(const void* data, size_t size, bool reliable = false);
    void broadcastExcept(ConnectionID excludeId, const void* data, size_t size, bool reliable = false);
    
    // Receive messages
    std::vector<ClientInputPacket> getPendingInputs();
    
    // Connection management
    void disconnectClient(ConnectionID connectionId, const char* reason = nullptr);
    void disconnectAll(const char* reason = nullptr);
    
    // Queries
    [[nodiscard]] bool isClientConnected(ConnectionID connectionId) const;
    [[nodiscard]] ConnectionStats getConnectionStats(ConnectionID connectionId) const;
    [[nodiscard]] size_t getConnectionCount() const;
    [[nodiscard]] std::vector<ConnectionID> getConnectedClients() const;
    
    // Callbacks (set before initialize)
    void setOnClientConnected(std::function<void(ConnectionID)> callback);
    void setOnClientDisconnected(std::function<void(ConnectionID, const char*)> callback);
    void setOnClientInput(std::function<void(ConnectionID, const void*, size_t)> callback);
    
    // Encryption
    bool setEncryptionKey(ConnectionID connectionId, const uint8_t* key, size_t keySize);
    void enableEncryptionGlobally(bool enable);
    
    // NAT punchthrough
    void beginNATPunchthrough(const char* targetAddress);
    
    // Debug
    void dumpStats() const;
    
private:
    // GNS callbacks (static trampolines to instance methods)
    static void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    void handleConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    
    // Message handling
    void processMessages();
    void processConnectionStateChanges();
    
    // Internal send
    void sendPacket(ConnectionID connectionId, const void* data, size_t size, int sendFlags);
    
    // Connection management
    void addConnection(ConnectionID id, uint32_t handle);
    void removeConnection(ConnectionID id);
    uint32_t getConnectionHandle(ConnectionID id) const;
    
private:
    // GNS interface
    ISteamNetworkingSockets* sockets_ = nullptr;
    ISteamNetworkingUtils* utils_ = nullptr;
    
    // Server listen socket
    uint32_t listenSocket_ = 0;
    
    // Configuration
    GNSConfig config_;
    
    // Connection mapping
    struct ConnectionInfo {
        uint32_t handle;
        ConnectionState state;
        ConnectionStats stats;
        uint64_t connectTimeMs;
        uint64_t lastActivityMs;
        std::string address;
    };
    
    mutable std::mutex connectionsMutex_;
    std::unordered_map<ConnectionID, ConnectionInfo> connections_;
    std::atomic<ConnectionID> nextConnectionId_{1};
    
    // Pending messages
    std::mutex inputMutex_;
    std::vector<ClientInputPacket> pendingInputs_;
    
    // Callbacks
    std::function<void(ConnectionID)> onClientConnected_;
    std::function<void(ConnectionID, const char*)> onClientDisconnected_;
    std::function<void(ConnectionID, const void*, size_t)> onClientInput_;
    
    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    uint32_t pollGroup_ = 0;
    
    // Static instance for callbacks
    static GNSNetworkManager* instance_;
};

} // namespace Netcode
} // namespace DarkAges
