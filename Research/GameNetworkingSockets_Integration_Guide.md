# GameNetworkingSockets Integration Patterns Research

**Document Version:** 1.0  
**Target:** DarkAges MMO NetworkManager Implementation  
**GNS Version:** v1.4.1  
**Date:** 2026-01-30

---

## Executive Summary

This document provides comprehensive research on integrating Valve's GameNetworkingSockets (GNS) v1.4.1 into the DarkAges MMO server architecture. GNS provides production-grade UDP networking with reliable message streams, congestion control, and cross-platform support - essential for supporting 100-1000 concurrent players with <50ms input latency.

### Key Findings

- **Architecture Pattern:** Callback-driven with poll groups for efficient message processing
- **Threading:** Single game thread + GNS internal threads; callbacks queued to main thread
- **Windows Libraries:** ws2_32.lib, winmm.lib, crypt32.lib, iphlpapi.lib, OpenSSL/mbedTLS
- **Tick Integration:** Non-blocking `update()` called at 60Hz, processes all pending messages
- **Performance:** Poll groups enable O(1) message retrieval; support for 1000+ connections

---

## 1. GNS Integration Architecture

### 1.1 Recommended Server Architecture Pattern

```
┌─────────────────────────────────────────────────────────────────┐
│                    GAME THREAD (60Hz)                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Update Loop  │→ │ NetworkMgr   │→ │ Process Inputs       │  │
│  │ (16.67ms)    │  │ .update()    │  │ → ECS Systems        │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│           │              │                                       │
│           │              ↓                                       │
│           │       ┌──────────────┐                               │
│           │       │ GNS Poll     │                               │
│           │       │ ReceiveMsgs  │                               │
│           │       └──────────────┘                               │
│           │                                                      │
│  ┌────────┴──────────────────────────────────────────────────┐  │
│  │  Send Phase (snapshots/events)                           │  │
│  │  - broadcastSnapshot() @ 20Hz                            │  │
│  │  - sendEvent() as needed (reliable)                      │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────┼──────────────────────────────────┐
│        GNS INTERNAL THREADS │                                  │
│  ┌─────────────────────┐   │   ┌────────────────────────────┐ │
│  │ Connection Mgmt     │◄──┘   │ Callback Queue (thread-safe)│ │
│  │ - Accept/connect    │       │ → Queued to main thread    │ │
│  │ - Timeout handling  │       └────────────────────────────┘ │
│  └─────────────────────┘                                      │
│  ┌─────────────────────┐                                      │
│  │ Reliable Messages   │                                      │
│  │ - Retransmission    │                                      │
│  │ - ACK processing    │                                      │
│  └─────────────────────┘                                      │
└───────────────────────────────────────────────────────────────┘
```

### 1.2 Connection Management Best Practices

**Connection State Tracking:**
```cpp
struct ConnectionState {
    HSteamNetConnection connection;    // GNS handle
    uint32_t connectionId;              // Our ID (1, 2, 3...)
    std::string ipAddress;
    uint64_t connectTime;
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
    bool isActive{false};
};
```

**Key Principles:**
1. **Always use connection IDs, not handles** - Handles can be reused after disconnect
2. **Lock-protect connection maps** - GNS callbacks come from different threads
3. **Queue callbacks to main thread** - Never process game logic in GNS callbacks
4. **Validate before use** - Check `isActive` before sending

### 1.3 Callback vs Polling Approaches

**GNS uses both:**

| Approach | Use Case | Implementation |
|----------|----------|----------------|
| **Callbacks** | Connection state changes | `SteamNetConnectionStatusChangedCallback` |
| **Polling** | Message reception | `ReceiveMessagesOnPollGroup()` |

**Recommended Pattern:**
```cpp
void NetworkManager::update(uint32_t currentTimeMs) {
    // 1. Process queued callbacks from GNS threads
    callbackQueue.processAll();
    
    // 2. Run GNS callbacks (connection events)
    interface_->RunCallbacks();
    
    // 3. Poll for messages (game inputs)
    ISteamNetworkingMessage* msg = nullptr;
    while (interface_->ReceiveMessagesOnPollGroup(pollGroup_, &msg, 1) > 0) {
        processMessage(msg);
        msg->Release();
    }
}
```

### 1.4 Threading Models

**GNS Internal Threads:**
- Connection management thread
- Reliable message processing thread
- UDP socket thread(s)

**Game Thread Requirements:**
- Must call `RunCallbacks()` regularly
- Must process message queue
- Must NOT block in network operations

**Thread-Safe Queue for Callbacks:**
```cpp
struct ThreadSafeQueue {
    std::mutex mutex;
    std::queue<std::function<void()>> queue;
    
    void push(std::function<void()> func) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(func));
    }
    
    void processAll() {
        std::queue<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::swap(local, queue);
        }
        while (!local.empty()) {
            local.front()();
            local.pop();
        }
    }
};
```

---

## 2. Windows Implementation Details

### 2.1 Required Libraries

**Link against these Windows libraries:**

```cmake
if(WIN32)
    target_link_libraries(your_target PUBLIC
        ws2_32      # Windows Sockets 2
        winmm       # Windows Multimedia (timers)
        crypt32     # Cryptography (certificate validation)
        iphlpapi    # IP Helper API (network adapters)
    )
endif()
```

| Library | Purpose | Required By |
|---------|---------|-------------|
| `ws2_32.lib` | Windows sockets, IPv4/IPv6 | Core networking |
| `winmm.lib` | High-resolution timers | Latency measurement |
| `crypt32.lib` | Certificate chain validation | TLS/encryption |
| `iphlpapi.lib` | Network adapter enumeration | Local address discovery |

### 2.2 OpenSSL Dependency Handling

**Option 1: Use mbedTLS (Built-in)**
```cmake
set(USE_CRYPTO MBEDTLS CACHE STRING "" FORCE)
```
- No external dependencies
- Slightly larger binary
- Sufficient for game networking

**Option 2: Use OpenSSL (System)**
```cmake
set(USE_CRYPTO OpenSSL CACHE STRING "" FORCE)
find_package(OpenSSL REQUIRED)
```
- Requires OpenSSL installed on system
- Smaller binary if OpenSSL shared
- Better performance for high-throughput

**Windows OpenSSL Installation:**
```powershell
# Using winget
winget install ShiningLight.OpenSSL

# Or download from https://slproweb.com/products/Win32OpenSSL.html
```

### 2.3 IPv4/IPv6 Dual-Stack Setup

**Creating Dual-Stack Listen Socket:**
```cpp
SteamNetworkingIPAddr addr;
addr.Clear();
// Binding to IPv6 any address enables dual-stack
addr.SetIPv6(::in6addr_any, port);

// Create listen socket
SteamNetworkingConfigValue_t opt;
opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
           (void*)ConnectionStatusChangedCallback);

HSteamListenSocket listenSocket = interface_->CreateListenSocketIP(addr, 1, &opt);
```

**Key Points:**
- Binding to `::` (IPv6 any) automatically accepts IPv4 connections
- IPv4 addresses appear as IPv4-mapped IPv6 (`::ffff:192.168.1.1`)
- No special handling needed for dual-stack

### 2.4 Firewall Considerations

**Windows Firewall Rules:**
```powershell
# Create inbound rule for game server
New-NetFirewallRule -DisplayName "DarkAges Server" `
    -Direction Inbound `
    -Protocol UDP `
    -LocalPort 7777 `
    -Action Allow
```

**Common Issues:**
1. **Windows Defender** - May block by default
2. **Corporate Networks** - UDP often restricted
3. **Router NAT** - Requires port forwarding for external access
4. **UPnP** - Can request automatic port forwarding

---

## 3. Server Loop Integration

### 3.1 Integrating with Game Tick (60Hz)

**Recommended Tick Structure:**
```cpp
class ZoneServer {
    NetworkManager network_;
    entt::registry registry_;
    
    static constexpr auto TICK_INTERVAL = std::chrono::milliseconds(16);
    
public:
    void run() {
        auto lastTick = std::chrono::steady_clock::now();
        uint32_t tickNumber = 0;
        
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - lastTick;
            
            if (elapsed >= TICK_INTERVAL) {
                // 1. Network update (non-blocking)
                uint32_t timeMs = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count());
                network_.update(timeMs);
                
                // 2. Get and process inputs
                auto inputs = network_.getPendingInputs();
                processInputs(inputs);
                
                // 3. Game simulation
                updateGame(tickNumber);
                
                // 4. Send snapshots (every 3rd tick = 20Hz)
                if (tickNumber % 3 == 0) {
                    broadcastSnapshots();
                }
                
                lastTick = now;
                tickNumber++;
            }
            
            // Yield to prevent CPU spinning
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
};
```

### 3.2 Message Receive Patterns

**Pattern 1: Immediate Processing (Recommended for Inputs)**
```cpp
void NetworkManager::update(uint32_t currentTimeMs) {
    // Process all pending messages
    ISteamNetworkingMessage* msg = nullptr;
    while (interface_->ReceiveMessagesOnPollGroup(pollGroup_, &msg, 1) > 0) {
        // Parse packet type (first byte)
        uint8_t packetType = static_cast<const uint8_t*>(msg->m_pData)[0];
        
        switch (static_cast<PacketType>(packetType)) {
            case PacketType::ClientInput:
                processInput(msg);
                break;
            case PacketType::Ping:
                processPing(msg);
                break;
            // ...
        }
        
        msg->Release();  // CRITICAL: Always release
    }
}
```

**Pattern 2: Buffered Processing (For complex pipelines)**
```cpp
void NetworkManager::update(uint32_t currentTimeMs) {
    // Collect all messages
    std::vector<ISteamNetworkingMessage*> messages;
    ISteamNetworkingMessage* msg = nullptr;
    
    while (interface_->ReceiveMessagesOnPollGroup(pollGroup_, &msg, 1) > 0) {
        messages.push_back(msg);
    }
    
    // Process in batch
    for (auto* m : messages) {
        processMessage(m);
        m->Release();
    }
}
```

### 3.3 Send Queue Management

**Unreliable Messages (Snapshots):**
```cpp
void NetworkManager::sendSnapshot(ConnectionID connId, std::span<const uint8_t> data) {
    // Send flags for snapshots:
    // - Unreliable: Dropped packets OK
    // - NoDelay: Send immediately (don't wait for Nagle)
    // - NoNagle: Disable Nagle algorithm
    const int flags = k_nSteamNetworkingSend_Unreliable 
                    | k_nSteamNetworkingSend_NoDelay 
                    | k_nSteamNetworkingSend_NoNagle;
    
    interface_->SendMessageToConnection(conn, data.data(), data.size(), flags, nullptr);
}
```

**Reliable Messages (Events):**
```cpp
void NetworkManager::sendEvent(ConnectionID connId, std::span<const uint8_t> data) {
    // Send flags for events:
    // - Reliable: Guaranteed delivery
    // - Auto retry on failure
    const int flags = k_nSteamNetworkingSend_Reliable;
    
    interface_->SendMessageToConnection(conn, data.data(), data.size(), flags, nullptr);
}
```

**Send Flags Reference:**

| Flag | Use Case | Latency | Reliability |
|------|----------|---------|-------------|
| `k_nSteamNetworkingSend_Unreliable` | Snapshots | Lowest | None |
| `k_nSteamNetworkingSend_Reliable` | Events, chat | Higher | Guaranteed |
| `k_nSteamNetworkingSend_NoDelay` | Time-critical | Lowest | Varies |
| `k_nSteamNetworkingSend_NoNagle` | Disable buffering | Lower | Varies |

### 3.4 Connection Quality Monitoring

**Real-time Status:**
```cpp
ConnectionQuality getConnectionQuality(HSteamNetConnection conn) {
    SteamNetConnectionRealTimeStatus_t status;
    interface_->GetConnectionRealTimeStatus(conn, &status, 0, nullptr);
    
    ConnectionQuality q;
    q.rttMs = static_cast<uint32_t>(status.m_nPing);
    q.packetLoss = status.m_flPacketsDroppedPct;
    q.jitterMs = status.m_flJitterConnection;
    return q;
}
```

**Key Metrics to Track:**
- **RTT (Ping):** < 100ms good, 100-300ms playable, >300ms poor
- **Packet Loss:** < 1% good, 1-5% playable, >5% poor
- **Jitter:** < 10ms good, 10-50ms acceptable, >50ms problematic

---

## 4. Code Examples

### 4.1 Minimal Server Initialization

```cpp
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <iostream>

class MinimalServer {
    ISteamNetworkingSockets* interface_ = nullptr;
    HSteamListenSocket listenSocket_ = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup pollGroup_ = k_HSteamNetPollGroup_Invalid;
    
public:
    bool initialize(uint16_t port) {
        // Initialize GNS
        SteamDatagramErrMsg errMsg;
        if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
            std::cerr << "GNS init failed: " << errMsg << std::endl;
            return false;
        }
        
        interface_ = SteamNetworkingSockets();
        if (!interface_) {
            std::cerr << "Failed to get GNS interface" << std::endl;
            return false;
        }
        
        // Configure callback
        SteamNetworkingConfigValue_t opt;
        opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                   (void*)OnConnectionStatusChanged);
        
        // Create listen socket (dual-stack)
        SteamNetworkingIPAddr addr;
        addr.Clear();
        addr.SetIPv6(::in6addr_any, port);
        
        listenSocket_ = interface_->CreateListenSocketIP(addr, 1, &opt);
        if (listenSocket_ == k_HSteamListenSocket_Invalid) {
            std::cerr << "Failed to create listen socket" << std::endl;
            return false;
        }
        
        // Create poll group for efficient message retrieval
        pollGroup_ = interface_->CreatePollGroup();
        if (pollGroup_ == k_HSteamNetPollGroup_Invalid) {
            std::cerr << "Failed to create poll group" << std::endl;
            return false;
        }
        
        std::cout << "Server listening on port " << port << std::endl;
        return true;
    }
    
    void shutdown() {
        if (listenSocket_ != k_HSteamListenSocket_Invalid) {
            interface_->CloseListenSocket(listenSocket_);
        }
        if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
            interface_->DestroyPollGroup(pollGroup_);
        }
        GameNetworkingSockets_Kill();
    }
    
    static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
        // Handle connection state changes
        switch (info->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting:
                std::cout << "Connection attempt from " 
                          << info->m_info.m_addrRemote.GetIPv4() << std::endl;
                break;
            case k_ESteamNetworkingConnectionState_Connected:
                std::cout << "Client connected!" << std::endl;
                break;
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
                std::cout << "Client disconnected" << std::endl;
                break;
        }
    }
};
```

### 4.2 Connection Acceptance

```cpp
void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    switch (pInfo->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting: {
            // Extract IP address
            char ipAddrStr[128];
            pInfo->m_info.m_addrRemote.ToString(ipAddrStr, sizeof(ipAddrStr), false);
            
            // Validate connection (DDoS check, ban list, etc.)
            if (!shouldAcceptConnection(ipAddrStr)) {
                interface_->CloseConnection(pInfo->m_hConn, 
                    k_ESteamNetConnectionEnd_App_Kick, 
                    "Connection rejected", false);
                break;
            }
            
            // Accept connection
            if (interface_->AcceptConnection(pInfo->m_hConn) != k_EResultOK) {
                interface_->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
                break;
            }
            
            // Add to poll group
            interface_->SetConnectionPollGroup(pInfo->m_hConn, pollGroup_);
            
            // Track connection
            trackNewConnection(pInfo->m_hConn, ipAddrStr);
            break;
        }
        
        case k_ESteamNetworkingConnectionState_Connected: {
            // Notify game logic
            onClientConnected(getConnectionId(pInfo->m_hConn));
            break;
        }
        
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            auto connId = getConnectionId(pInfo->m_hConn);
            onClientDisconnected(connId);
            cleanupConnection(pInfo->m_hConn);
            break;
        }
    }
}
```

### 4.3 Reliable vs Unreliable Send

```cpp
enum class SendChannel {
    Unreliable,     // Position updates (latest only)
    Reliable,       // Combat events (must arrive)
    ReliableNoDelay // Critical events (immediate)
};

void sendMessage(HSteamNetConnection conn, std::span<const uint8_t> data, 
                 SendChannel channel) {
    int flags = 0;
    
    switch (channel) {
        case SendChannel::Unreliable:
            flags = k_nSteamNetworkingSend_Unreliable 
                  | k_nSteamNetworkingSend_NoDelay
                  | k_nSteamNetworkingSend_NoNagle;
            break;
            
        case SendChannel::Reliable:
            flags = k_nSteamNetworkingSend_Reliable;
            break;
            
        case SendChannel::ReliableNoDelay:
            flags = k_nSteamNetworkingSend_Reliable 
                  | k_nSteamNetworkingSend_NoDelay;
            break;
    }
    
    EResult result = interface_->SendMessageToConnection(
        conn, data.data(), static_cast<uint32_t>(data.size()), flags, nullptr);
    
    if (result != k_EResultOK) {
        // Handle error (connection likely dead)
        markConnectionForCleanup(conn);
    }
}
```

### 4.4 Message Receive in Game Loop

```cpp
void NetworkManager::update(uint32_t currentTimeMs) {
    // Process queued callbacks first
    callbackQueue.processAll();
    
    // Run GNS callbacks
    interface_->RunCallbacks();
    
    // Receive all pending messages
    ISteamNetworkingMessage* msg = nullptr;
    while (true) {
        int numMsgs = interface_->ReceiveMessagesOnPollGroup(pollGroup_, &msg, 1);
        
        if (numMsgs <= 0) {
            break;  // No more messages
        }
        
        // Get connection ID
        auto connId = getConnectionId(msg->m_conn);
        if (connId == INVALID_CONNECTION) {
            msg->Release();
            continue;
        }
        
        // Parse packet
        if (msg->m_cbSize > 0) {
            uint8_t packetType = static_cast<const uint8_t*>(msg->m_pData)[0];
            
            switch (packetType) {
                case PACKET_INPUT:
                    handleInput(connId, msg);
                    break;
                case PACKET_PING:
                    handlePing(connId, msg);
                    break;
                case PACKET_DISCONNECT:
                    handleDisconnect(connId);
                    break;
                default:
                    // Unknown packet type
                    break;
            }
        }
        
        // CRITICAL: Always release message
        msg->Release();
    }
    
    // Update rate limiting / DDoS protection
    ddosProtection_.update(currentTimeMs);
}
```

### 4.5 Cleanup/Shutdown

```cpp
void NetworkManager::shutdown() {
    if (!initialized_) return;
    
    // 1. Notify all clients
    std::vector<uint8_t> disconnectMsg = {PACKET_DISCONNECT, DISCONNECT_SERVER_SHUTDOWN};
    broadcastEvent(disconnectMsg);
    
    // 2. Close all connections gracefully
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        for (const auto& [id, state] : connections_) {
            if (state.isActive) {
                interface_->CloseConnection(state.connection,
                    k_ESteamNetConnectionEnd_App_Generic,
                    "Server shutdown", false);
            }
        }
    }
    
    // 3. Give time for disconnect messages to send (optional)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 4. Cleanup GNS resources
    if (listenSocket_ != k_HSteamListenSocket_Invalid) {
        interface_->CloseListenSocket(listenSocket_);
    }
    if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
        interface_->DestroyPollGroup(pollGroup_);
    }
    
    // 5. Shutdown GNS
    GameNetworkingSockets_Kill();
    
    initialized_ = false;
}
```

---

## 5. Common Issues & Solutions

### 5.1 Memory Leaks in Callbacks

**Problem:** GNS callbacks can fire after object destruction

**Solution:** Use weak pointers or global lookup with cleanup
```cpp
// Global lookup with automatic cleanup
class NetworkManager {
    static std::unordered_map<HSteamListenSocket, NetworkManager*> g_instances;
    static std::mutex g_instancesMutex;
    
public:
    bool initialize() {
        // ... init code ...
        std::lock_guard<std::mutex> lock(g_instancesMutex);
        g_instances[listenSocket_] = this;
    }
    
    ~NetworkManager() {
        if (listenSocket_ != k_HSteamListenSocket_Invalid) {
            std::lock_guard<std::mutex> lock(g_instancesMutex);
            g_instances.erase(listenSocket_);
        }
    }
    
    static void StaticCallback(SteamNetConnectionStatusChangedCallback_t* info) {
        std::lock_guard<std::mutex> lock(g_instancesMutex);
        auto it = g_instances.find(info->m_hListenSocket);
        if (it != g_instances.end()) {
            // Queue to main thread, don't execute directly
            it->second->queueCallback(info);
        }
    }
};
```

### 5.2 Thread Safety Concerns

**Problem:** GNS callbacks come from different threads

**Solution:** Never access game state from callbacks
```cpp
// BAD: Accessing game state from callback
void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    gameWorld.spawnPlayer();  // RACE CONDITION!
}

// GOOD: Queue to main thread
void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    auto* copy = new SteamNetConnectionStatusChangedCallback_t(*info);
    callbackQueue.push([copy]() {
        processConnectionChange(copy);
        delete copy;
    });
}
```

### 5.3 Message Size Limits

**Limits:**
- **MTU:** ~1200 bytes (conservative for VPNs)
- **Max Reliable:** 512KB (GNS internal limit)
- **Max Unreliable:** MTU-sized

**Solutions:**
```cpp
// For large snapshots, fragment manually
void sendLargeSnapshot(ConnectionID conn, const std::vector<uint8_t>& data) {
    constexpr size_t CHUNK_SIZE = 1000;  // Leave room for headers
    
    if (data.size() <= CHUNK_SIZE) {
        // Small enough, send directly
        sendSnapshot(conn, data);
        return;
    }
    
    // Send as multiple fragments
    size_t offset = 0;
    uint8_t fragmentId = 0;
    uint8_t totalFragments = (data.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    while (offset < data.size()) {
        size_t chunkSize = std::min(CHUNK_SIZE, data.size() - offset);
        
        std::vector<uint8_t> fragment;
        fragment.reserve(chunkSize + 3);
        fragment.push_back(PACKET_SNAPSHOT_FRAGMENT);
        fragment.push_back(fragmentId);
        fragment.push_back(totalFragments);
        fragment.insert(fragment.end(), data.begin() + offset, 
                       data.begin() + offset + chunkSize);
        
        sendReliable(conn, fragment);  // Fragments must be reliable
        
        offset += chunkSize;
        fragmentId++;
    }
}
```

### 5.4 Connection Timeout Handling

**Configuration:**
```cpp
SteamNetworkingConfigValue_t timeoutOpt;
timeoutOpt.SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, 10000);    // 10s initial
timeoutOpt.SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 5000);   // 5s after connected
```

**Timeout States:**
```cpp
case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
    // Local timeout - didn't receive packets
    std::cerr << "Connection timeout (local)" << std::endl;
    break;
    
case k_ESteamNetworkingConnectionState_ClosedByPeer:
    // Remote end closed or timed out
    std::cerr << "Connection closed by peer" << std::endl;
    break;
```

---

## 6. Performance Optimization

### 6.1 Batch Sending

**Problem:** Individual sends have overhead

**Solution:** Use `SendMessages()` for batch sending
```cpp
void broadcastSnapshot(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    // Prepare message data once
    std::vector<uint8_t> packet;
    packet.reserve(data.size() + 1);
    packet.push_back(PACKET_SNAPSHOT);
    packet.insert(packet.end(), data.begin(), data.end());
    
    // Send to all connections
    for (const auto& [id, state] : connections_) {
        if (state.isActive) {
            interface_->SendMessageToConnection(
                state.connection,
                packet.data(),
                static_cast<uint32_t>(packet.size()),
                k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoNagle,
                nullptr
            );
        }
    }
}
```

### 6.2 Poll Group Usage

**Benefits:**
- O(1) message retrieval per connection
- No per-connection polling overhead
- Automatic connection grouping

**Usage:**
```cpp
// Create poll group
HSteamNetPollGroup pollGroup = interface_->CreatePollGroup();

// Add connections to group
interface_->SetConnectionPollGroup(conn, pollGroup);

// Receive from all connections in group at once
ISteamNetworkingMessage* msg = nullptr;
while (interface_->ReceiveMessagesOnPollGroup(pollGroup, &msg, 1) > 0) {
    processMessage(msg);
    msg->Release();
}
```

### 6.3 Message Flags Optimization

| Scenario | Recommended Flags | Rationale |
|----------|-------------------|-----------|
| Position snapshots | `Unreliable \| NoDelay \| NoNagle` | Latest only, minimize latency |
| Combat events | `Reliable` | Must arrive, some buffering OK |
| Chat messages | `Reliable` | Guaranteed delivery |
| RPC calls | `Reliable \| NoDelay` | Must arrive, low latency |
| Voice data | `Unreliable \| NoNagle` | Time-sensitive, some loss OK |

### 6.4 Bandwidth Optimization

**Delta Compression:**
```cpp
// Only send changed fields
struct DeltaEntityState {
    uint32_t entityId;
    uint16_t changedMask;  // Bitmask of which fields follow
    // Only include fields that changed
};
```

**Interest Management:**
```cpp
// Don't send distant entities
bool shouldReplicateToPlayer(const Entity& entity, const Player& player) {
    float distance = glm::distance(entity.position, player.position);
    return distance < MAX_REPLICATION_DISTANCE;
}
```

**Update Frequency Scaling:**
```cpp
// Reduce update rate for distant entities
int getUpdateRate(float distance) {
    if (distance < 50.0f) return 20;   // 20Hz for nearby
    if (distance < 100.0f) return 10;  // 10Hz for mid-range
    if (distance < 200.0f) return 5;   // 5Hz for far
    return 0;                          // Don't send beyond 200m
}
```

---

## 7. Troubleshooting Checklist

### Connection Issues

| Symptom | Check | Solution |
|---------|-------|----------|
| Can't connect | Firewall | Allow UDP port in Windows Firewall |
| Can't connect | Port binding | Check if port already in use |
| Connection drops | Timeout values | Increase timeout for slow networks |
| High latency | Send flags | Use `NoDelay` and `NoNagle` |
| Packet loss | MTU size | Reduce to 1200 bytes for VPNs |

### Build Issues (Windows)

| Error | Cause | Solution |
|-------|-------|----------|
| `LNK2019: unresolved external` | Missing libs | Add ws2_32, winmm, crypt32, iphlpapi |
| `OpenSSL not found` | Missing OpenSSL | Install OpenSSL or use mbedTLS |
| `Cannot open include file` | Include path | Add GNS include directory |
| `SteamDatagramErrMsg undefined` | Wrong version | Use GNS v1.4.1 or later |

### Runtime Issues

| Symptom | Check | Solution |
|---------|-------|----------|
| Crash on callback | Thread safety | Queue callbacks to main thread |
| Memory leak | Message release | Always call `msg->Release()` |
| Stale connections | State tracking | Validate `isActive` before send |
| High CPU usage | Sleep in loop | Add `sleep_for` between ticks |
| Lag spikes | Message processing | Process messages in batches |

---

## 8. References

### Official Resources
- GameNetworkingSockets GitHub: https://github.com/ValveSoftware/GameNetworkingSockets
- Header files: `steamnetworkingsockets.h`, `isteamnetworkingutils.h`
- Example code: `examples/example_chat.cpp`

### DarkAges Integration
- `src/server/src/netcode/NetworkManager.cpp` - Full implementation
- `src/server/include/netcode/NetworkManager.hpp` - Interface definition
- `src/server/tests/TestGNSIntegration.cpp` - Unit tests
- `docs/network-protocol/PROTOCOL_SPEC.md` - Protocol specification

### Related Documents
- `ImplementationRoadmapGuide.md` - Technical specifications
- `AGENTS.md` - Agent coding standards
- `ResearchForPlans.md` - Architectural rationale

---

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-30 | Network Agent | Initial research document |

### Sign-off

- [ ] Architecture Review
- [ ] Security Review  
- [ ] Performance Review
- [ ] Integration Tested
