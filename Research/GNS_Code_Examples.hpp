// ============================================================================
// GameNetworkingSockets Minimal Examples - Copy-Paste Ready
// ============================================================================
// This file provides minimal, self-contained code examples for common GNS
// integration patterns. Use these as starting points for implementation.
//
// Dependencies:
//   - GameNetworkingSockets v1.4.1
//   - C++17 or later
//
// Windows Libraries:
//   ws2_32.lib, winmm.lib, crypt32.lib, iphlpapi.lib
//
// ============================================================================

#pragma once

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <chrono>
#include <thread>
#include <iostream>

// ============================================================================
// Example 1: Minimal Server
// ============================================================================

class MinimalGNSServer {
public:
    bool start(uint16_t port) {
        // Initialize GNS
        SteamDatagramErrMsg errMsg;
        if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
            std::cerr << "GNS init failed: " << errMsg << std::endl;
            return false;
        }
        
        interface_ = SteamNetworkingSockets();
        if (!interface_) {
            std::cerr << "Failed to get interface" << std::endl;
            return false;
        }
        
        // Setup callback
        SteamNetworkingConfigValue_t opt;
        opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                   (void*)StaticConnectionCallback);
        
        // Bind to IPv6 any (enables dual-stack)
        SteamNetworkingIPAddr addr;
        addr.Clear();
        addr.SetIPv6(::in6addr_any, port);
        
        listenSocket_ = interface_->CreateListenSocketIP(addr, 1, &opt);
        if (listenSocket_ == k_HSteamListenSocket_Invalid) {
            std::cerr << "Failed to create listen socket" << std::endl;
            return false;
        }
        
        // Create poll group for efficient message handling
        pollGroup_ = interface_->CreatePollGroup();
        if (pollGroup_ == k_HSteamNetPollGroup_Invalid) {
            std::cerr << "Failed to create poll group" << std::endl;
            return false;
        }
        
        // Register for callback lookup
        std::lock_guard<std::mutex> lock(g_serversMutex);
        g_servers[listenSocket_] = this;
        
        std::cout << "Server started on port " << port << std::endl;
        return true;
    }
    
    void stop() {
        // Unregister
        {
            std::lock_guard<std::mutex> lock(g_serversMutex);
            g_servers.erase(listenSocket_);
        }
        
        // Close all connections
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            for (const auto& [id, conn] : connections_) {
                interface_->CloseConnection(conn, 0, "Server shutdown", false);
            }
            connections_.clear();
        }
        
        if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
            interface_->DestroyPollGroup(pollGroup_);
        }
        if (listenSocket_ != k_HSteamListenSocket_Invalid) {
            interface_->CloseListenSocket(listenSocket_);
        }
        
        GameNetworkingSockets_Kill();
    }
    
    void update() {
        // Process callbacks
        interface_->RunCallbacks();
        
        // Process queued callbacks
        processQueuedCallbacks();
        
        // Receive messages
        ISteamNetworkingMessage* msg = nullptr;
        while (interface_->ReceiveMessagesOnPollGroup(pollGroup_, &msg, 1) > 0) {
            onMessageReceived(msg);
            msg->Release();
        }
    }
    
    void sendToClient(uint32_t clientId, const void* data, uint32_t size, bool reliable) {
        HSteamNetConnection conn = getConnection(clientId);
        if (conn == k_HSteamNetConnection_Invalid) return;
        
        int flags = reliable ? k_nSteamNetworkingSend_Reliable 
                             : k_nSteamNetworkingSend_Unreliable;
        interface_->SendMessageToConnection(conn, data, size, flags, nullptr);
    }
    
    void broadcast(const void* data, uint32_t size, bool reliable) {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        int flags = reliable ? k_nSteamNetworkingSend_Reliable 
                             : k_nSteamNetworkingSend_Unreliable;
        
        for (const auto& [id, conn] : connections_) {
            interface_->SendMessageToConnection(conn, data, size, flags, nullptr);
        }
    }

private:
    struct CallbackInfo {
        HSteamListenSocket listenSocket;
        SteamNetConnectionStatusChangedCallback_t info;
    };
    
    static std::mutex g_serversMutex;
    static std::unordered_map<HSteamListenSocket, MinimalGNSServer*> g_servers;
    
    static void StaticConnectionCallback(SteamNetConnectionStatusChangedCallback_t* info) {
        std::lock_guard<std::mutex> lock(g_serversMutex);
        auto it = g_servers.find(info->m_hListenSocket);
        if (it != g_servers.end()) {
            // Queue callback to main thread
            auto* copy = new SteamNetConnectionStatusChangedCallback_t(*info);
            it->second->queueCallback(copy);
        }
    }
    
    void queueCallback(SteamNetConnectionStatusChangedCallback_t* info) {
        std::lock_guard<std::mutex> lock(callbackQueueMutex_);
        callbackQueue_.push(info);
    }
    
    void processQueuedCallbacks() {
        std::queue<SteamNetConnectionStatusChangedCallback_t*> queue;
        {
            std::lock_guard<std::mutex> lock(callbackQueueMutex_);
            std::swap(queue, callbackQueue_);
        }
        
        while (!queue.empty()) {
            auto* info = queue.front();
            handleConnectionStatusChanged(info);
            delete info;
            queue.pop();
        }
    }
    
    void handleConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo) {
        switch (pInfo->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting: {
                // Accept connection
                if (interface_->AcceptConnection(pInfo->m_hConn) == k_EResultOK) {
                    interface_->SetConnectionPollGroup(pInfo->m_hConn, pollGroup_);
                    
                    uint32_t id = nextClientId_++;
                    {
                        std::lock_guard<std::mutex> lock(connectionsMutex_);
                        connections_[id] = pInfo->m_hConn;
                        connToId_[pInfo->m_hConn] = id;
                    }
                    
                    std::cout << "Client " << id << " connected" << std::endl;
                    if (onClientConnected_) onClientConnected_(id);
                }
                break;
            }
            
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
                auto it = connToId_.find(pInfo->m_hConn);
                if (it != connToId_.end()) {
                    uint32_t id = it->second;
                    std::cout << "Client " << id << " disconnected" << std::endl;
                    
                    {
                        std::lock_guard<std::mutex> lock(connectionsMutex_);
                        connections_.erase(id);
                        connToId_.erase(it);
                    }
                    
                    if (onClientDisconnected_) onClientDisconnected_(id);
                }
                interface_->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
                break;
            }
        }
    }
    
    void onMessageReceived(ISteamNetworkingMessage* msg) {
        auto it = connToId_.find(msg->m_conn);
        if (it == connToId_.end()) return;
        
        uint32_t clientId = it->second;
        if (onMessage_) {
            onMessage_(clientId, msg->m_pData, msg->m_cbSize);
        }
    }
    
    HSteamNetConnection getConnection(uint32_t clientId) {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        auto it = connections_.find(clientId);
        return (it != connections_.end()) ? it->second : k_HSteamNetConnection_Invalid;
    }

public:
    std::function<void(uint32_t)> onClientConnected_;
    std::function<void(uint32_t)> onClientDisconnected_;
    std::function<void(uint32_t, const void*, uint32_t)> onMessage_;

private:
    ISteamNetworkingSockets* interface_ = nullptr;
    HSteamListenSocket listenSocket_ = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup pollGroup_ = k_HSteamNetPollGroup_Invalid;
    
    std::mutex connectionsMutex_;
    std::unordered_map<uint32_t, HSteamNetConnection> connections_;
    std::unordered_map<HSteamNetConnection, uint32_t> connToId_;
    uint32_t nextClientId_ = 1;
    
    std::mutex callbackQueueMutex_;
    std::queue<SteamNetConnectionStatusChangedCallback_t*> callbackQueue_;
};

std::mutex MinimalGNSServer::g_serversMutex;
std::unordered_map<HSteamListenSocket, MinimalGNSServer*> MinimalGNSServer::g_servers;

// ============================================================================
// Example 2: Minimal Client
// ============================================================================

class MinimalGNSClient {
public:
    bool connect(const char* address, uint16_t port) {
        SteamDatagramErrMsg errMsg;
        if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
            return false;
        }
        
        interface_ = SteamNetworkingSockets();
        if (!interface_) return false;
        
        // Parse address
        SteamNetworkingIPAddr addr;
        addr.Clear();
        if (!addr.ParseString(address)) {
            // Try with port
            std::string addrWithPort = std::string(address) + ":" + std::to_string(port);
            if (!addr.ParseString(addrWithPort.c_str())) {
                std::cerr << "Failed to parse address" << std::endl;
                return false;
            }
        }
        if (addr.m_port == 0) {
            addr.m_port = port;
        }
        
        // Setup callback
        SteamNetworkingConfigValue_t opt;
        opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                   (void*)StaticConnectionCallback);
        
        // Connect
        connection_ = interface_->ConnectByIPAddress(addr, 1, &opt);
        if (connection_ == k_HSteamNetConnection_Invalid) {
            return false;
        }
        
        // Register for callbacks
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        g_clients[connection_] = this;
        
        return true;
    }
    
    void disconnect() {
        if (connection_ != k_HSteamNetConnection_Invalid) {
            interface_->CloseConnection(connection_, 0, "Client disconnect", true);
            
            std::lock_guard<std::mutex> lock(g_clientsMutex);
            g_clients.erase(connection_);
            
            connection_ = k_HSteamNetConnection_Invalid;
        }
        GameNetworkingSockets_Kill();
    }
    
    void update() {
        interface_->RunCallbacks();
        processQueuedCallbacks();
        
        // Receive messages
        ISteamNetworkingMessage* msg = nullptr;
        while (interface_->ReceiveMessagesOnConnection(connection_, &msg, 1) > 0) {
            if (onMessage_) {
                onMessage_(msg->m_pData, msg->m_cbSize);
            }
            msg->Release();
        }
    }
    
    void send(const void* data, uint32_t size, bool reliable) {
        if (connection_ == k_HSteamNetConnection_Invalid) return;
        
        int flags = reliable ? k_nSteamNetworkingSend_Reliable 
                             : k_nSteamNetworkingSend_Unreliable;
        interface_->SendMessageToConnection(connection_, data, size, flags, nullptr);
    }
    
    bool isConnected() const {
        return connected_;
    }

private:
    struct CallbackInfo {
        HSteamNetConnection connection;
        SteamNetConnectionStatusChangedCallback_t info;
    };
    
    static std::mutex g_clientsMutex;
    static std::unordered_map<HSteamNetConnection, MinimalGNSClient*> g_clients;
    
    static void StaticConnectionCallback(SteamNetConnectionStatusChangedCallback_t* info) {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        auto it = g_clients.find(info->m_hConn);
        if (it != g_clients.end()) {
            auto* copy = new SteamNetConnectionStatusChangedCallback_t(*info);
            it->second->queueCallback(copy);
        }
    }
    
    void queueCallback(SteamNetConnectionStatusChangedCallback_t* info) {
        std::lock_guard<std::mutex> lock(callbackQueueMutex_);
        callbackQueue_.push(info);
    }
    
    void processQueuedCallbacks() {
        std::queue<SteamNetConnectionStatusChangedCallback_t*> queue;
        {
            std::lock_guard<std::mutex> lock(callbackQueueMutex_);
            std::swap(queue, callbackQueue_);
        }
        
        while (!queue.empty()) {
            auto* info = queue.front();
            handleConnectionStatusChanged(info);
            delete info;
            queue.pop();
        }
    }
    
    void handleConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo) {
        switch (pInfo->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connected:
                connected_ = true;
                std::cout << "Connected to server" << std::endl;
                if (onConnected_) onConnected_();
                break;
                
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                connected_ = false;
                std::cout << "Disconnected from server" << std::endl;
                if (onDisconnected_) onDisconnected_();
                break;
        }
    }

public:
    std::function<void()> onConnected_;
    std::function<void()> onDisconnected_;
    std::function<void(const void*, uint32_t)> onMessage_;

private:
    ISteamNetworkingSockets* interface_ = nullptr;
    HSteamNetConnection connection_ = k_HSteamNetConnection_Invalid;
    bool connected_ = false;
    
    std::mutex callbackQueueMutex_;
    std::queue<SteamNetConnectionStatusChangedCallback_t*> callbackQueue_;
};

std::mutex MinimalGNSClient::g_clientsMutex;
std::unordered_map<HSteamNetConnection, MinimalGNSClient*> MinimalGNSClient::g_clients;

// ============================================================================
// Example 3: Connection Quality Monitor
// ============================================================================

struct ConnectionQuality {
    uint32_t pingMs = 0;
    float packetLossPct = 0.0f;
    float jitterMs = 0.0f;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint32_t packetsSent = 0;
    uint32_t packetsReceived = 0;
};

class ConnectionQualityMonitor {
public:
    ConnectionQuality getQuality(ISteamNetworkingSockets* iface, HSteamNetConnection conn) {
        ConnectionQuality quality;
        
        // Get real-time stats
        SteamNetConnectionRealTimeStatus_t status;
        if (iface->GetConnectionRealTimeStatus(conn, &status, 0, nullptr)) {
            quality.pingMs = static_cast<uint32_t>(status.m_nPing);
            quality.packetLossPct = status.m_flPacketsDroppedPct;
            quality.jitterMs = status.m_flJitterConnection;
        }
        
        // Get detailed stats
        SteamNetConnectionInfo_t info;
        if (iface->GetConnectionInfo(conn, &info)) {
            // Additional info available here
        }
        
        return quality;
    }
    
    void printQuality(const ConnectionQuality& q) {
        std::cout << "Connection Quality:" << std::endl;
        std::cout << "  Ping: " << q.pingMs << "ms" << std::endl;
        std::cout << "  Packet Loss: " << (q.packetLossPct * 100.0f) << "%" << std::endl;
        std::cout << "  Jitter: " << q.jitterMs << "ms" << std::endl;
        std::cout << "  Sent: " << q.bytesSent << " bytes (" << q.packetsSent << " pkts)" << std::endl;
        std::cout << "  Received: " << q.bytesReceived << " bytes (" << q.packetsReceived << " pkts)" << std::endl;
    }
};

// ============================================================================
// Example 4: Usage Example
// ============================================================================

inline void exampleUsage() {
    // Server usage
    {
        MinimalGNSServer server;
        
        server.onClientConnected_ = [](uint32_t id) {
            std::cout << "Client " << id << " connected!" << std::endl;
        };
        
        server.onClientDisconnected_ = [](uint32_t id) {
            std::cout << "Client " << id << " disconnected!" << std::endl;
        };
        
        server.onMessage_ = [&server](uint32_t id, const void* data, uint32_t size) {
            // Echo back
            server.sendToClient(id, data, size, false);
        };
        
        if (server.start(7777)) {
            std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
            
            // Main loop
            while (true) {
                server.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }
        
        server.stop();
    }
    
    // Client usage
    {
        MinimalGNSClient client;
        
        client.onConnected_ = []() {
            std::cout << "Connected!" << std::endl;
        };
        
        client.onDisconnected_ = []() {
            std::cout << "Disconnected!" << std::endl;
        };
        
        client.onMessage_ = [](const void* data, uint32_t size) {
            std::cout << "Received " << size << " bytes" << std::endl;
        };
        
        if (client.connect("127.0.0.1", 7777)) {
            // Wait for connection
            while (!client.isConnected()) {
                client.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            
            // Send a message
            const char* msg = "Hello, Server!";
            client.send(msg, std::strlen(msg) + 1, true);
            
            // Main loop
            while (client.isConnected()) {
                client.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }
        
        client.disconnect();
    }
}

// ============================================================================
// Build Instructions (CMake)
// ============================================================================
/*
# CMakeLists.txt snippet:

find_package(GameNetworkingSockets QUIET)
if(NOT GameNetworkingSockets_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        GameNetworkingSockets
        GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
        GIT_TAG v1.4.1
        GIT_SHALLOW TRUE
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(USE_CRYPTO MBEDTLS CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(GameNetworkingSockets)
endif()

target_link_libraries(your_target PRIVATE
    GameNetworkingSockets::GameNetworkingSockets
)

if(WIN32)
    target_link_libraries(your_target PRIVATE
        ws2_32
        winmm
        crypt32
        iphlpapi
    )
endif()
*/
