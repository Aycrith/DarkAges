// [NETWORK_AGENT] GameNetworkingSockets Implementation - WP-6-1
// Full GNS integration with connection quality monitoring, reliable/unreliable channels,
// IPv4/IPv6 dual-stack support, and DDoS protection integration.

#include "netcode/NetworkManager.hpp"
#include "Constants.hpp"

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <cstring>
#include <algorithm>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <string>

namespace DarkAges {

// ============================================================================
// GNS Internal Structures
// ============================================================================

// Connection state tracking
struct ConnectionState {
    HSteamNetConnection connection;
    uint32_t connectionId{0};
    std::string ipAddress;
    uint64_t connectTime{0};
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
    uint32_t packetsSent{0};
    uint32_t packetsReceived{0};
    bool isActive{false};
};

// Thread-safe message queue for cross-thread communication
struct ThreadSafeQueue {
    std::mutex mutex;
    std::queue<std::function<void()>> queue;
    
    void push(std::function<void()> func) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(func));
    }
    
    void processAll() {
        std::queue<std::function<void()>> localQueue;
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::swap(localQueue, queue);
        }
        while (!localQueue.empty()) {
            localQueue.front()();
            localQueue.pop();
        }
    }
};

// GNS Internal implementation
struct GNSInternal {
    ISteamNetworkingSockets* interface_{nullptr};
    HSteamListenSocket listenSocket_{k_HSteamListenSocket_Invalid};
    HSteamNetPollGroup pollGroup_{k_HSteamNetPollGroup_Invalid};
    
    // Connection management
    std::unordered_map<uint32_t, ConnectionState> connections;
    std::unordered_map<HSteamNetConnection, uint32_t> connToId;
    uint32_t nextConnectionId_{1};
    mutable std::mutex connectionsMutex;
    
    // Callback queue for thread-safe operations
    ThreadSafeQueue callbackQueue;
    
    // Stats
    uint64_t totalBytesSent_{0};
    uint64_t totalBytesReceived_{0};
    
    // Back-reference to NetworkManager for callbacks
    NetworkManager* manager_{nullptr};
    
    // Static callback for GNS connection status changes
    static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo);
    
    // Handle connection status change
    void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);
};

// Global pointer for callback redirection (GNS uses C-style callbacks)
// In production, this could be a map of listen sockets to managers
static std::mutex g_managerMutex;
static std::unordered_map<HSteamListenSocket, NetworkManager*> g_listenSocketToManager;

// ============================================================================
// GNS Callback Implementation
// ============================================================================

void GNSInternal::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    // Find the manager associated with this listen socket
    NetworkManager* manager = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_managerMutex);
        auto it = g_listenSocketToManager.find(pInfo->m_hListenSocket);
        if (it != g_listenSocketToManager.end()) {
            manager = it->second;
        }
    }
    
    if (manager && manager->internal_) {
        // Queue the callback to be processed on the main thread
        SteamNetConnectionStatusChangedCallback_t* pInfoCopy = 
            new SteamNetConnectionStatusChangedCallback_t(*pInfo);
        
        manager->internal_->callbackQueue.push([manager, pInfoCopy]() {
            if (manager->internal_) {
                manager->internal_->onConnectionStatusChanged(pInfoCopy);
            }
            delete pInfoCopy;
        });
    }
}

void GNSInternal::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    switch (pInfo->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting: {
            // Extract IP address
            char ipAddrStr[128];
            pInfo->m_info.m_addrRemote.ToString(ipAddrStr, sizeof(ipAddrStr), false);
            
            std::string ipAddress(ipAddrStr);
            
            // Check DDoS protection
            if (!manager_->shouldAcceptConnection(ipAddress)) {
                interface_->CloseConnection(pInfo->m_hConn, k_ESteamNetConnectionEnd_App_Kick, 
                    "Connection rejected by DDoS protection", false);
                break;
            }
            
            // Accept the connection
            if (interface_->AcceptConnection(pInfo->m_hConn) != k_EResultOK) {
                interface_->CloseConnection(pInfo->m_hConn, k_ESteamNetConnectionEnd_App_Kick, 
                    "Failed to accept connection", false);
                break;
            }
            
            // Add to poll group
            interface_->SetConnectionPollGroup(pInfo->m_hConn, pollGroup_);
            
            // Create connection state
            uint32_t connId = nextConnectionId_++;
            ConnectionState state;
            state.connection = pInfo->m_hConn;
            state.connectionId = connId;
            state.ipAddress = ipAddress;
            state.connectTime = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            state.isActive = true;
            
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                connections[connId] = state;
                connToId[pInfo->m_hConn] = connId;
            }
            
            break;
        }
        
        case k_ESteamNetworkingConnectionState_Connected: {
            HSteamNetConnection conn = pInfo->m_hConn;
            uint32_t connId = 0;
            std::string ipAddress;
            
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connToId.find(conn);
                if (it != connToId.end()) {
                    connId = it->second;
                    auto connIt = connections.find(connId);
                    if (connIt != connections.end()) {
                        ipAddress = connIt->second.ipAddress;
                    }
                }
            }
            
            if (connId != 0 && manager_->onConnected_) {
                manager_->onConnected_(connId);
            }
            
            break;
        }
        
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            HSteamNetConnection conn = pInfo->m_hConn;
            uint32_t connId = 0;
            
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connToId.find(conn);
                if (it != connToId.end()) {
                    connId = it->second;
                    
                    // Update stats
                    auto connIt = connections.find(connId);
                    if (connIt != connections.end()) {
                        totalBytesSent_ += connIt->second.bytesSent;
                        totalBytesReceived_ += connIt->second.bytesReceived;
                        connIt->second.isActive = false;
                    }
                    
                    connToId.erase(it);
                    connections.erase(connId);
                }
            }
            
            // Notify DDoS protection of disconnection
            manager_->ddosProtection_.onConnectionClosed(connId);
            
            // Notify via callback
            if (connId != 0 && manager_->onDisconnected_) {
                manager_->onDisconnected_(connId);
            }
            
            // Close the connection handle
            interface_->CloseConnection(conn, 0, nullptr, false);
            
            break;
        }
        
        default:
            break;
    }
}

// ============================================================================
// NetworkManager Implementation
// ============================================================================

NetworkManager::NetworkManager() 
    : internal_(std::make_unique<GNSInternal>())
    , ddosProtection_() {
    internal_->manager_ = this;
}

NetworkManager::~NetworkManager() {
    shutdown();
}

bool NetworkManager::initialize(uint16_t port) {
    if (initialized_) {
        return true;
    }
    
    // Initialize DDoS protection
    if (!ddosProtection_.initialize()) {
        // Non-fatal, continue without DDoS protection
    }
    
    // Initialize GNS
    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        // Failed to initialize GNS
        return false;
    }
    
    internal_->interface_ = SteamNetworkingSockets();
    if (!internal_->interface_) {
        GameNetworkingSockets_Kill();
        return false;
    }
    
    // Configure listen socket options
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
               (void*)GNSInternal::SteamNetConnectionStatusChangedCallback);
    
    // Set connection timeout
    SteamNetworkingConfigValue_t timeoutOpt;
    timeoutOpt.SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, 10000);  // 10s initial timeout
    
    // Create bind address (dual-stack IPv4/IPv6)
    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.SetIPv6(::in6addr_any, port);  // Bind to all interfaces (IPv6 includes IPv4)
    
    // Create listen socket
    SteamNetworkingConfigValue_t opts[2] = {opt, timeoutOpt};
    internal_->listenSocket_ = internal_->interface_->CreateListenSocketIP(addr, 2, opts);
    
    if (internal_->listenSocket_ == k_HSteamListenSocket_Invalid) {
        GameNetworkingSockets_Kill();
        return false;
    }
    
    // Create poll group for efficient message polling
    internal_->pollGroup_ = internal_->interface_->CreatePollGroup();
    if (internal_->pollGroup_ == k_HSteamNetPollGroup_Invalid) {
        internal_->interface_->CloseListenSocket(internal_->listenSocket_);
        internal_->listenSocket_ = k_HSteamListenSocket_Invalid;
        GameNetworkingSockets_Kill();
        return false;
    }
    
    // Register for callback lookup
    {
        std::lock_guard<std::mutex> lock(g_managerMutex);
        g_listenSocketToManager[internal_->listenSocket_] = this;
    }
    
    initialized_ = true;
    return true;
}

void NetworkManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Close all connections
    {
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        for (const auto& [id, state] : internal_->connections) {
            if (state.isActive) {
                internal_->interface_->CloseConnection(state.connection, 
                    k_ESteamNetConnectionEnd_App_Generic, "Server shutdown", false);
            }
        }
        internal_->connections.clear();
        internal_->connToId.clear();
    }
    
    // Unregister from callback lookup
    if (internal_->listenSocket_ != k_HSteamListenSocket_Invalid) {
        std::lock_guard<std::mutex> lock(g_managerMutex);
        g_listenSocketToManager.erase(internal_->listenSocket_);
        
        internal_->interface_->CloseListenSocket(internal_->listenSocket_);
        internal_->listenSocket_ = k_HSteamListenSocket_Invalid;
    }
    
    if (internal_->pollGroup_ != k_HSteamNetPollGroup_Invalid) {
        internal_->interface_->DestroyPollGroup(internal_->pollGroup_);
        internal_->pollGroup_ = k_HSteamNetPollGroup_Invalid;
    }
    
    internal_->interface_ = nullptr;
    GameNetworkingSockets_Kill();
    
    initialized_ = false;
}

void NetworkManager::update(uint32_t currentTimeMs) {
    if (!initialized_ || !internal_->interface_) {
        return;
    }
    
    // Process any queued callbacks from GNS threads
    internal_->callbackQueue.processAll();
    
    // Run GNS callbacks (processes connection events)
    internal_->interface_->RunCallbacks();
    
    // Receive and process all pending messages
    ISteamNetworkingMessage* msg = nullptr;
    while (true) {
        int numMsgs = internal_->interface_->ReceiveMessagesOnPollGroup(
            internal_->pollGroup_, &msg, 1);
        
        if (numMsgs == 0) {
            break;
        }
        
        if (numMsgs < 0) {
            // Error
            break;
        }
        
        // Process the message
        HSteamNetConnection conn = msg->m_conn;
        uint32_t connId = 0;
        
        {
            std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
            auto it = internal_->connToId.find(conn);
            if (it != internal_->connToId.end()) {
                connId = it->second;
                auto connIt = internal_->connections.find(connId);
                if (connIt != internal_->connections.end()) {
                    connIt->second.bytesReceived += msg->m_cbSize;
                    connIt->second.packetsReceived++;
                }
            }
        }
        
        if (connId != 0) {
            // Check rate limiting
            std::string ipAddr;
            {
                std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
                auto it = internal_->connections.find(connId);
                if (it != internal_->connections.end()) {
                    ipAddr = it->second.ipAddress;
                }
            }
            
            if (!processPacket(connId, ipAddr, static_cast<uint32_t>(msg->m_cbSize), currentTimeMs)) {
                // Rate limited, drop packet
                msg->Release();
                continue;
            }
            
            // Parse packet type (first byte)
            if (msg->m_cbSize > 0) {
                uint8_t packetType = static_cast<const uint8_t*>(msg->m_pData)[0];
                
                switch (static_cast<PacketType>(packetType)) {
                    case PacketType::ClientInput: {
                        // Parse input packet
                        if (msg->m_cbSize >= sizeof(uint8_t) + sizeof(InputState)) {
                            InputState input;
                            std::memcpy(&input, 
                                static_cast<const uint8_t*>(msg->m_pData) + sizeof(uint8_t),
                                sizeof(InputState));
                            
                            ClientInputPacket packet;
                            packet.connectionId = connId;
                            packet.input = input;
                            packet.receiveTimeMs = currentTimeMs;
                            
                            pendingInputs_.push_back(packet);
                            
                            if (onInput_) {
                                onInput_(packet);
                            }
                        }
                        break;
                    }
                    
                    case PacketType::Ping: {
                        // Echo the ping back
                        sendEvent(connId, std::span<const uint8_t>(
                            static_cast<const uint8_t*>(msg->m_pData), 
                            msg->m_cbSize));
                        break;
                    }
                    
                    case PacketType::Disconnect: {
                        // Graceful disconnect
                        disconnect(connId, "Client requested disconnect");
                        break;
                    }
                    
                    default:
                        // Unknown packet type, ignore
                        break;
                }
            }
        }
        
        msg->Release();
    }
    
    // Update DDoS protection
    ddosProtection_.update(currentTimeMs);
}

std::vector<ClientInputPacket> NetworkManager::getPendingInputs() {
    std::vector<ClientInputPacket> result;
    std::swap(result, pendingInputs_);
    return result;
}

void NetworkManager::clearProcessedInputs(uint32_t upToSequence) {
    // Remove all inputs with sequence <= upToSequence
    pendingInputs_.erase(
        std::remove_if(pendingInputs_.begin(), pendingInputs_.end(),
            [upToSequence](const ClientInputPacket& packet) {
                return packet.input.sequence <= upToSequence;
            }),
        pendingInputs_.end()
    );
}

void NetworkManager::sendSnapshot(ConnectionID connectionId, std::span<const uint8_t> data) {
    if (!initialized_ || data.empty()) {
        return;
    }
    
    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    
    {
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        auto it = internal_->connections.find(connectionId);
        if (it != internal_->connections.end() && it->second.isActive) {
            conn = it->second.connection;
        }
    }
    
    if (conn != k_HSteamNetConnection_Invalid) {
        // Send unreliable (for position updates - latest only, dropped packets OK)
        internal_->interface_->SendMessageToConnection(
            conn,
            data.data(),
            static_cast<uint32_t>(data.size()),
            k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoDelay | k_nSteamNetworkingSend_NoNagle,
            nullptr
        );
        
        // Update stats
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        auto it = internal_->connections.find(connectionId);
        if (it != internal_->connections.end()) {
            it->second.bytesSent += data.size();
            it->second.packetsSent++;
        }
    }
}

void NetworkManager::sendEvent(ConnectionID connectionId, std::span<const uint8_t> data) {
    if (!initialized_ || data.empty()) {
        return;
    }
    
    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    
    {
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        auto it = internal_->connections.find(connectionId);
        if (it != internal_->connections.end() && it->second.isActive) {
            conn = it->second.connection;
        }
    }
    
    if (conn != k_HSteamNetConnection_Invalid) {
        // Send reliable (for combat events - must arrive)
        internal_->interface_->SendMessageToConnection(
            conn,
            data.data(),
            static_cast<uint32_t>(data.size()),
            k_nSteamNetworkingSend_Reliable,
            nullptr
        );
        
        // Update stats
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        auto it = internal_->connections.find(connectionId);
        if (it != internal_->connections.end()) {
            it->second.bytesSent += data.size();
            it->second.packetsSent++;
        }
    }
}

void NetworkManager::broadcastSnapshot(std::span<const uint8_t> data) {
    if (!initialized_ || data.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
    
    for (auto& [id, state] : internal_->connections) {
        if (state.isActive) {
            internal_->interface_->SendMessageToConnection(
                state.connection,
                data.data(),
                static_cast<uint32_t>(data.size()),
                k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoDelay | k_nSteamNetworkingSend_NoNagle,
                nullptr
            );
            
            state.bytesSent += data.size();
            state.packetsSent++;
        }
    }
}

void NetworkManager::broadcastEvent(std::span<const uint8_t> data) {
    if (!initialized_ || data.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
    
    for (auto& [id, state] : internal_->connections) {
        if (state.isActive) {
            internal_->interface_->SendMessageToConnection(
                state.connection,
                data.data(),
                static_cast<uint32_t>(data.size()),
                k_nSteamNetworkingSend_Reliable,
                nullptr
            );
            
            state.bytesSent += data.size();
            state.packetsSent++;
        }
    }
}

void NetworkManager::sendToMultiple(const std::vector<ConnectionID>& conns, std::span<const uint8_t> data) {
    if (!initialized_ || data.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
    
    for (ConnectionID connId : conns) {
        auto it = internal_->connections.find(connId);
        if (it != internal_->connections.end() && it->second.isActive) {
            internal_->interface_->SendMessageToConnection(
                it->second.connection,
                data.data(),
                static_cast<uint32_t>(data.size()),
                k_nSteamNetworkingSend_Reliable,
                nullptr
            );
            
            it->second.bytesSent += data.size();
            it->second.packetsSent++;
        }
    }
}

void NetworkManager::disconnect(ConnectionID connectionId, const char* reason) {
    if (!initialized_) {
        return;
    }
    
    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    
    {
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        auto it = internal_->connections.find(connectionId);
        if (it != internal_->connections.end()) {
            conn = it->second.connection;
            
            // Update totals before removing
            internal_->totalBytesSent_ += it->second.bytesSent;
            internal_->totalBytesReceived_ += it->second.bytesReceived;
            
            internal_->connToId.erase(conn);
            internal_->connections.erase(it);
        }
    }
    
    if (conn != k_HSteamNetConnection_Invalid) {
        internal_->interface_->CloseConnection(conn, 
            k_ESteamNetConnectionEnd_App_Generic, 
            reason ? reason : "Server disconnected", 
            false);
    }
    
    // Notify DDoS protection
    ddosProtection_.onConnectionClosed(connectionId);
    
    // Notify callback
    if (onDisconnected_) {
        onDisconnected_(connectionId);
    }
}

bool NetworkManager::isConnected(ConnectionID connectionId) const {
    std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
    auto it = internal_->connections.find(connectionId);
    return it != internal_->connections.end() && it->second.isActive;
}

ConnectionQuality NetworkManager::getConnectionQuality(ConnectionID connectionId) const {
    ConnectionQuality quality{};
    
    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    
    {
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        auto it = internal_->connections.find(connectionId);
        if (it != internal_->connections.end()) {
            conn = it->second.connection;
            quality.bytesSent = it->second.bytesSent;
            quality.bytesReceived = it->second.bytesReceived;
            quality.packetsSent = it->second.packetsSent;
            quality.packetsReceived = it->second.packetsReceived;
        }
    }
    
    if (conn != k_HSteamNetConnection_Invalid && internal_->interface_) {
        SteamNetConnectionRealTimeStatus_t status;
        if (internal_->interface_->GetConnectionRealTimeStatus(conn, &status, 0, nullptr)) {
            quality.rttMs = static_cast<uint32_t>(status.m_nPing);
            quality.packetLoss = status.m_flPacketsDroppedPct;
            quality.jitterMs = status.m_flJitterConnection;
        }
    }
    
    return quality;
}

size_t NetworkManager::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
    return internal_->connections.size();
}

uint64_t NetworkManager::getTotalBytesSent() const {
    std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
    uint64_t total = internal_->totalBytesSent_;
    for (const auto& [id, state] : internal_->connections) {
        total += state.bytesSent;
    }
    return total;
}

uint64_t NetworkManager::getTotalBytesReceived() const {
    std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
    uint64_t total = internal_->totalBytesReceived_;
    for (const auto& [id, state] : internal_->connections) {
        total += state.bytesReceived;
    }
    return total;
}

bool NetworkManager::isRateLimited(ConnectionID connectionId) const {
    std::string ipAddress;
    {
        std::lock_guard<std::mutex> lock(internal_->connectionsMutex);
        auto it = internal_->connections.find(connectionId);
        if (it != internal_->connections.end()) {
            ipAddress = it->second.ipAddress;
        }
    }
    
    if (ipAddress.empty()) {
        return false;
    }
    
    // Use DDoS protection for rate limit check
    return !ddosProtection_.shouldAcceptConnection(ipAddress);
}

bool NetworkManager::shouldAcceptConnection(const std::string& ipAddress) {
    return ddosProtection_.shouldAcceptConnection(ipAddress);
}

bool NetworkManager::processPacket(ConnectionID connectionId, 
                                   const std::string& ipAddress,
                                   uint32_t packetSize,
                                   uint32_t currentTimeMs) {
    return ddosProtection_.processPacket(connectionId, ipAddress, packetSize, currentTimeMs);
}

} // namespace DarkAges
