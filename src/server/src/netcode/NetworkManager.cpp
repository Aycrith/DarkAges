// [NETWORK_AGENT] GameNetworkingSockets wrapper implementation
// Handles UDP connections, reliability, and connection quality

#include "netcode/NetworkManager.hpp"
#include "Constants.hpp"
#include <cstring>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <queue>
#include <atomic>
#include <cmath>
#include <string>

// GNS availability check
#if __has_include(<steam/steamnetworkingsockets.h>)
    #include <steam/steamnetworkingsockets.h>
    #include <steam/isteamnetworkingutils.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
    #define GNS_AVAILABLE 1
#else
    #define GNS_AVAILABLE 0
#endif

namespace DarkAges {

// Rate limiting info per connection
struct RateLimitInfo {
    uint32_t inputCount{0};
    uint32_t windowStartMs{0};
};

// Internal implementation struct (PIMPL idiom)
struct GNSInternal {
#if GNS_AVAILABLE
    HSteamListenSocket listenSocket = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup pollGroup = k_HSteamNetPollGroup_Invalid;
    std::unordered_map<ConnectionID, HSteamNetConnection> connIdToGns;
    std::unordered_map<HSteamNetConnection, ConnectionID> gnsToConnId;
    std::unordered_map<ConnectionID, ConnectionQuality> connectionQuality;
    std::unordered_map<ConnectionID, RateLimitInfo> rateLimits;
    
    struct ConnectionEvent {
        enum Type { Connected, Disconnected } type;
        ConnectionID connectionId;
        HSteamNetConnection gnsConn;
    };
    std::queue<ConnectionEvent> connectionEvents;
    std::mutex eventMutex;
#else
    uint16_t port{0};
    std::unordered_map<ConnectionID, ConnectionQuality> connections;
    ConnectionID nextConnectionId{1};
#endif
    
    bool initialized{false};
    std::atomic<uint64_t> totalBytesSent{0};
    std::atomic<uint64_t> totalBytesReceived{0};
};

#if GNS_AVAILABLE
// GNS callback for connection status changes
static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    (void)pInfo;
}
#endif

NetworkManager::NetworkManager() 
    : internal_(std::make_unique<GNSInternal>()) {
    // Initialize DDoS protection with default config
    ddosProtection_.initialize();
}

NetworkManager::~NetworkManager() {
    shutdown();
}

bool NetworkManager::initialize(uint16_t port) {
    if (initialized_) {
        std::cerr << "[NETWORK] Already initialized!" << std::endl;
        return false;
    }

    std::cout << "[NETWORK] Initializing on port " << port << "..." << std::endl;

#if GNS_AVAILABLE
    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        std::cerr << "[NETWORK] GNS Init failed: " << errMsg << std::endl;
        return false;
    }
    
    internal_->pollGroup = SteamNetworkingSockets()->CreatePollGroup();
    if (internal_->pollGroup == k_HSteamNetPollGroup_Invalid) {
        std::cerr << "[NETWORK] Failed to create poll group" << std::endl;
        GameNetworkingSockets_Kill();
        return false;
    }
    
    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.m_port = port;
    
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, 
               (void*)OnConnectionStatusChanged);
    
    internal_->listenSocket = SteamNetworkingSockets()->CreateListenSocketIP(addr, 1, &opt);
    if (internal_->listenSocket == k_HSteamListenSocket_Invalid) {
        std::cerr << "[NETWORK] Failed to create listen socket" << std::endl;
        SteamNetworkingSockets()->DestroyPollGroup(internal_->pollGroup);
        GameNetworkingSockets_Kill();
        return false;
    }
    
    std::cout << "[NETWORK] GNS initialized successfully on port " << port << std::endl;
#else
    std::cout << "[NETWORK] Warning: GNS not available, using stub implementation" << std::endl;
    (void)port;
#endif
    
    internal_->initialized = true;
    initialized_ = true;
    return true;
}

void NetworkManager::shutdown() {
    if (!initialized_) return;
    
    std::cout << "[NETWORK] Shutting down..." << std::endl;

#if GNS_AVAILABLE
    {
        std::lock_guard<std::mutex> lock(internal_->eventMutex);
        for (const auto& [connId, gnsConn] : internal_->connIdToGns) {
            SteamNetworkingSockets()->CloseConnection(gnsConn, 0, "Server shutting down", true);
        }
        internal_->connIdToGns.clear();
        internal_->gnsToConnId.clear();
        internal_->connectionQuality.clear();
        internal_->rateLimits.clear();
    }
    
    if (internal_->listenSocket != k_HSteamListenSocket_Invalid) {
        SteamNetworkingSockets()->CloseListenSocket(internal_->listenSocket);
        internal_->listenSocket = k_HSteamListenSocket_Invalid;
    }
    
    if (internal_->pollGroup != k_HSteamNetPollGroup_Invalid) {
        SteamNetworkingSockets()->DestroyPollGroup(internal_->pollGroup);
        internal_->pollGroup = k_HSteamNetPollGroup_Invalid;
    }
    
    GameNetworkingSockets_Kill();
#else
    internal_->connections.clear();
#endif
    
    internal_->initialized = false;
    initialized_ = false;
}

void NetworkManager::update(uint32_t currentTimeMs) {
    if (!initialized_) return;
    
    // Update DDoS protection (cleanup expired blocks, etc.)
    ddosProtection_.update(currentTimeMs);

#if GNS_AVAILABLE
    SteamNetworkingSockets()->RunCallbacks();
    
    {
        std::lock_guard<std::mutex> lock(internal_->eventMutex);
        while (!internal_->connectionEvents.empty()) {
            auto event = std::move(internal_->connectionEvents.front());
            internal_->connectionEvents.pop();
            
            if (event.type == GNSInternal::ConnectionEvent::Connected) {
                if (onConnected_) {
                    onConnected_(event.connectionId);
                }
            } else {
                internal_->connIdToGns.erase(event.connectionId);
                internal_->connectionQuality.erase(event.connectionId);
                internal_->rateLimits.erase(event.connectionId);
                
                auto it = internal_->gnsToConnId.find(event.gnsConn);
                if (it != internal_->gnsToConnId.end()) {
                    internal_->gnsToConnId.erase(it);
                }
                
                if (onDisconnected_) {
                    onDisconnected_(event.connectionId);
                }
            }
        }
    }
    
    SteamNetworkingMessage_t* messages[256];
    int msgCount = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(
        internal_->pollGroup, messages, 256);
    
    for (int i = 0; i < msgCount; ++i) {
        SteamNetworkingMessage_t* msg = messages[i];
        
        ConnectionID connId = INVALID_CONNECTION;
        {
            std::lock_guard<std::mutex> lock(internal_->eventMutex);
            auto it = internal_->gnsToConnId.find(msg->m_conn);
            if (it != internal_->gnsToConnId.end()) {
                connId = it->second;
            }
        }
        
        if (connId != INVALID_CONNECTION && msg->m_cbSize > 0) {
            // [SECURITY_AGENT] DDoS protection - check packet rate
            // Get IP address for this connection (simplified - would come from GNS)
            std::string ipAddress = "0.0.0.0";  // Placeholder - real implementation would extract from GNS
            
            if (!ddosProtection_.processPacket(connId, ipAddress, 
                                               static_cast<uint32_t>(msg->m_cbSize),
                                               currentTimeMs)) {
                // Packet dropped due to rate limiting
                msg->Release();
                continue;
            }
            
            internal_->totalBytesReceived.fetch_add(msg->m_cbSize);
            
            {
                std::lock_guard<std::mutex> lock(internal_->eventMutex);
                auto& quality = internal_->connectionQuality[connId];
                quality.packetsReceived++;
                quality.bytesReceived += msg->m_cbSize;
                
                SteamNetConnectionRealTimeStatus_t status;
                if (SteamNetworkingSockets()->GetConnectionRealTimeStatus(
                        msg->m_conn, &status, 0, nullptr) == k_EResultOK) {
                    quality.rttMs = status.m_nPing;
                    quality.packetLoss = status.m_flPacketsDroppedPct * 100.0f;
                    quality.jitterMs = status.m_flInPacketsRecvTime - status.m_flOutPacketsSentTime;
                }
            }
            
            uint8_t packetType = static_cast<uint8_t>(msg->m_pData[0]);
            
            switch (static_cast<PacketType>(packetType)) {
                case PacketType::ClientInput: {
                    if (msg->m_cbSize > 1) {
                        std::span<const uint8_t> data(
                            static_cast<const uint8_t*>(msg->m_pData) + 1,
                            msg->m_cbSize - 1);
                        
                        InputState input;
                        if (Protocol::deserializeInput(data, input)) {
                            ClientInputPacket packet;
                            packet.connectionId = connId;
                            packet.input = input;
                            packet.receiveTimeMs = currentTimeMs;
                            
                            bool rateLimited = false;
                            {
                                std::lock_guard<std::mutex> lock(internal_->eventMutex);
                                auto& rateInfo = internal_->rateLimits[connId];
                                
                                if (currentTimeMs - rateInfo.windowStartMs > Constants::INPUT_RATE_LIMIT_WINDOW_MS) {
                                    rateInfo.windowStartMs = currentTimeMs;
                                    rateInfo.inputCount = 0;
                                }
                                
                                rateInfo.inputCount++;
                                if (rateInfo.inputCount > Constants::MAX_INPUTS_PER_SECOND) {
                                    rateLimited = true;
                                }
                            }
                            
                            if (!rateLimited) {
                                pendingInputs_.push_back(packet);
                                
                                if (onInput_) {
                                    onInput_(packet);
                                }
                            }
                        }
                    }
                    break;
                }
                
                case PacketType::Ping: {
                    std::vector<uint8_t> pongData;
                    pongData.reserve(msg->m_cbSize);
                    pongData.push_back(static_cast<uint8_t>(PacketType::Ping));
                    if (msg->m_cbSize > 1) {
                        const uint8_t* payload = static_cast<const uint8_t*>(msg->m_pData) + 1;
                        pongData.insert(pongData.end(), payload, payload + msg->m_cbSize - 1);
                    }
                    sendEvent(connId, pongData);
                    break;
                }
                
                case PacketType::Handshake: {
                    std::vector<uint8_t> response;
                    response.push_back(static_cast<uint8_t>(PacketType::Handshake));
                    response.push_back(1);
                    response.push_back(0);
                    sendEvent(connId, response);
                    break;
                }
                
                case PacketType::Disconnect: {
                    disconnect(connId, "Client requested disconnect");
                    break;
                }
                
                default:
                    break;
            }
        }
        
        msg->Release();
    }
#else
    (void)currentTimeMs;
#endif
}

void NetworkManager::sendSnapshot(ConnectionID connectionId, std::span<const uint8_t> data) {
    if (!initialized_ || data.empty()) return;

#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    
    auto it = internal_->connIdToGns.find(connectionId);
    if (it == internal_->connIdToGns.end()) return;
    
    HSteamNetConnection gnsConn = it->second;
    
    EResult result = SteamNetworkingSockets()->SendMessageToConnection(
        gnsConn,
        data.data(),
        static_cast<uint32_t>(data.size()),
        k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoDelay,
        nullptr
    );
    
    if (result == k_EResultOK) {
        internal_->totalBytesSent.fetch_add(data.size());
        
        auto& quality = internal_->connectionQuality[connectionId];
        quality.packetsSent++;
        quality.bytesSent += data.size();
    }
#else
    (void)connectionId;
    internal_->totalBytesSent.fetch_add(data.size());
    auto connIt = internal_->connections.find(connectionId);
    if (connIt != internal_->connections.end()) {
        connIt->second.packetsSent++;
        connIt->second.bytesSent += data.size();
    }
#endif
}

void NetworkManager::sendEvent(ConnectionID connectionId, std::span<const uint8_t> data) {
    if (!initialized_ || data.empty()) return;

#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    
    auto it = internal_->connIdToGns.find(connectionId);
    if (it == internal_->connIdToGns.end()) return;
    
    HSteamNetConnection gnsConn = it->second;
    
    EResult result = SteamNetworkingSockets()->SendMessageToConnection(
        gnsConn,
        data.data(),
        static_cast<uint32_t>(data.size()),
        k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoDelay,
        nullptr
    );
    
    if (result == k_EResultOK) {
        internal_->totalBytesSent.fetch_add(data.size());
        
        auto& quality = internal_->connectionQuality[connectionId];
        quality.packetsSent++;
        quality.bytesSent += data.size();
    }
#else
    (void)connectionId;
    internal_->totalBytesSent.fetch_add(data.size());
#endif
}

void NetworkManager::broadcastSnapshot(std::span<const uint8_t> data) {
#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    
    for (const auto& [connId, gnsConn] : internal_->connIdToGns) {
        (void)gnsConn;
        lock.~lock_guard();
        sendSnapshot(connId, data);
        new (&lock) std::lock_guard<std::mutex>(internal_->eventMutex);
    }
#else
    for (const auto& [connId, quality] : internal_->connections) {
        (void)quality;
        sendSnapshot(connId, data);
    }
#endif
}

void NetworkManager::broadcastEvent(std::span<const uint8_t> data) {
#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    
    for (const auto& [connId, gnsConn] : internal_->connIdToGns) {
        (void)gnsConn;
        lock.~lock_guard();
        sendEvent(connId, data);
        new (&lock) std::lock_guard<std::mutex>(internal_->eventMutex);
    }
#else
    for (const auto& [connId, quality] : internal_->connections) {
        (void)quality;
        sendEvent(connId, data);
    }
#endif
}

std::vector<ClientInputPacket> NetworkManager::getPendingInputs() {
    std::vector<ClientInputPacket> result;
    result.swap(pendingInputs_);
    return result;
}

void NetworkManager::disconnect(ConnectionID connectionId, const char* reason) {
    // [SECURITY_AGENT] Notify DDoS protection of connection close
    ddosProtection_.onConnectionClosed(connectionId);
    
#if GNS_AVAILABLE
    HSteamNetConnection gnsConn = k_HSteamNetConnection_Invalid;
    
    {
        std::lock_guard<std::mutex> lock(internal_->eventMutex);
        auto it = internal_->connIdToGns.find(connectionId);
        if (it != internal_->connIdToGns.end()) {
            gnsConn = it->second;
        }
    }
    
    if (gnsConn != k_HSteamNetConnection_Invalid) {
        SteamNetworkingSockets()->CloseConnection(
            gnsConn,
            0,
            reason ? reason : "Server disconnected",
            true
        );
        
        std::lock_guard<std::mutex> lock(internal_->eventMutex);
        internal_->connIdToGns.erase(connectionId);
        internal_->connectionQuality.erase(connectionId);
        internal_->rateLimits.erase(connectionId);
        
        auto it = internal_->gnsToConnId.find(gnsConn);
        if (it != internal_->gnsToConnId.end()) {
            internal_->gnsToConnId.erase(it);
        }
    }
#else
    auto it = internal_->connections.find(connectionId);
    if (it == internal_->connections.end()) return;
    
    std::cout << "[NETWORK] Disconnecting client " << connectionId 
              << (reason ? ": " : "") << (reason ? reason : "") << std::endl;
    
    internal_->connections.erase(it);
#endif
    
    if (onDisconnected_) {
        onDisconnected_(connectionId);
    }
}

bool NetworkManager::isConnected(ConnectionID connectionId) const {
#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    return internal_->connIdToGns.find(connectionId) != internal_->connIdToGns.end();
#else
    return internal_->connections.find(connectionId) != internal_->connections.end();
#endif
}

ConnectionQuality NetworkManager::getConnectionQuality(ConnectionID connectionId) const {
#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    auto it = internal_->connectionQuality.find(connectionId);
    if (it != internal_->connectionQuality.end()) {
        return it->second;
    }
#else
    auto it = internal_->connections.find(connectionId);
    if (it != internal_->connections.end()) {
        return it->second;
    }
#endif
    return ConnectionQuality{};
}

size_t NetworkManager::getConnectionCount() const {
#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    return internal_->connIdToGns.size();
#else
    return internal_->connections.size();
#endif
}

uint64_t NetworkManager::getTotalBytesSent() const {
    return internal_->totalBytesSent.load();
}

uint64_t NetworkManager::getTotalBytesReceived() const {
    return internal_->totalBytesReceived.load();
}

bool NetworkManager::isRateLimited(ConnectionID connectionId) const {
#if GNS_AVAILABLE
    std::lock_guard<std::mutex> lock(internal_->eventMutex);
    auto it = internal_->rateLimits.find(connectionId);
    if (it != internal_->rateLimits.end()) {
        return it->second.inputCount > Constants::MAX_INPUTS_PER_SECOND;
    }
#else
    (void)connectionId;
#endif
    return false;
}

// ============================================================================
// Protocol Serialization
// ============================================================================

namespace Protocol {

std::vector<uint8_t> serializeInput(const InputState& input) {
    std::vector<uint8_t> data;
    
    // Pack bit flags into single byte
    uint8_t flags = 0;
    flags |= (input.forward & 0x1) << 0;
    flags |= (input.backward & 0x1) << 1;
    flags |= (input.left & 0x1) << 2;
    flags |= (input.right & 0x1) << 3;
    flags |= (input.jump & 0x1) << 4;
    flags |= (input.attack & 0x1) << 5;
    flags |= (input.block & 0x1) << 6;
    flags |= (input.sprint & 0x1) << 7;
    data.push_back(flags);
    
    // Yaw and pitch (4 bytes each)
    const uint8_t* yawBytes = reinterpret_cast<const uint8_t*>(&input.yaw);
    const uint8_t* pitchBytes = reinterpret_cast<const uint8_t*>(&input.pitch);
    data.insert(data.end(), yawBytes, yawBytes + sizeof(float));
    data.insert(data.end(), pitchBytes, pitchBytes + sizeof(float));
    
    // Sequence number (4 bytes, little endian)
    data.push_back(static_cast<uint8_t>(input.sequence & 0xFF));
    data.push_back(static_cast<uint8_t>((input.sequence >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((input.sequence >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((input.sequence >> 24) & 0xFF));
    
    // Timestamp (4 bytes, little endian)
    data.push_back(static_cast<uint8_t>(input.timestamp_ms & 0xFF));
    data.push_back(static_cast<uint8_t>((input.timestamp_ms >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((input.timestamp_ms >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((input.timestamp_ms >> 24) & 0xFF));
    
    return data;
}

bool deserializeInput(std::span<const uint8_t> data, InputState& outInput) {
    if (data.size() < 17) {
        return false;
    }
    
    uint8_t flags = data[0];
    outInput.forward = (flags >> 0) & 0x1;
    outInput.backward = (flags >> 1) & 0x1;
    outInput.left = (flags >> 2) & 0x1;
    outInput.right = (flags >> 3) & 0x1;
    outInput.jump = (flags >> 4) & 0x1;
    outInput.attack = (flags >> 5) & 0x1;
    outInput.block = (flags >> 6) & 0x1;
    outInput.sprint = (flags >> 7) & 0x1;
    
    std::memcpy(&outInput.yaw, &data[1], sizeof(float));
    std::memcpy(&outInput.pitch, &data[5], sizeof(float));
    
    outInput.sequence = 
        static_cast<uint32_t>(data[9]) |
        (static_cast<uint32_t>(data[10]) << 8) |
        (static_cast<uint32_t>(data[11]) << 16) |
        (static_cast<uint32_t>(data[12]) << 24);
    
    outInput.timestamp_ms = 
        static_cast<uint32_t>(data[13]) |
        (static_cast<uint32_t>(data[14]) << 8) |
        (static_cast<uint32_t>(data[15]) << 16) |
        (static_cast<uint32_t>(data[16]) << 24);
    
    return true;
}

// ============================================================================
// EntityStateData Comparison Methods
// ============================================================================

bool EntityStateData::equalsPosition(const EntityStateData& other) const {
    const int32_t threshold = DeltaEncoding::SMALL_DELTA_THRESHOLD / 2;
    return std::abs(position.x - other.position.x) < threshold &&
           std::abs(position.y - other.position.y) < threshold &&
           std::abs(position.z - other.position.z) < threshold;
}

bool EntityStateData::equalsRotation(const EntityStateData& other) const {
    const float yawDiff = std::abs(rotation.yaw - other.rotation.yaw);
    const float pitchDiff = std::abs(rotation.pitch - other.rotation.pitch);
    return yawDiff < 0.035f && pitchDiff < 0.035f;
}

bool EntityStateData::equalsVelocity(const EntityStateData& other) const {
    const int32_t threshold = 100;
    return std::abs(velocity.dx - other.velocity.dx) < threshold &&
           std::abs(velocity.dy - other.velocity.dy) < threshold &&
           std::abs(velocity.dz - other.velocity.dz) < threshold;
}

// ============================================================================
// Delta Encoding Implementation
// ============================================================================

namespace DeltaEncoding {

// Helper to write a variable-length signed integer
static size_t writeVarDelta(uint8_t* buffer, size_t bufferSize, int32_t delta) {
    if (delta >= -127 && delta <= 127) {
        if (bufferSize < 1) return 0;
        buffer[0] = static_cast<int8_t>(delta);
        return 1;
    } else if (delta >= -32767 && delta <= 32767) {
        if (bufferSize < 3) return 0;
        buffer[0] = delta >= 0 ? 0x7F : 0x80;
        int16_t val = static_cast<int16_t>(delta);
        buffer[1] = static_cast<uint8_t>(val & 0xFF);
        buffer[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
        return 3;
    } else {
        if (bufferSize < 5) return 0;
        buffer[0] = 0x81;
        buffer[1] = static_cast<uint8_t>(delta & 0xFF);
        buffer[2] = static_cast<uint8_t>((delta >> 8) & 0xFF);
        buffer[3] = static_cast<uint8_t>((delta >> 16) & 0xFF);
        buffer[4] = static_cast<uint8_t>((delta >> 24) & 0xFF);
        return 5;
    }
}

// Helper to read a variable-length signed integer
static size_t readVarDelta(const uint8_t* buffer, size_t bufferSize, int32_t& outDelta) {
    if (bufferSize < 1) return 0;
    
    uint8_t marker = buffer[0];
    
    if (marker == 0x7F || marker == 0x80) {
        if (bufferSize < 3) return 0;
        int16_t val = static_cast<int16_t>(buffer[1] | (buffer[2] << 8));
        outDelta = val;
        return 3;
    } else if (marker == 0x81) {
        if (bufferSize < 5) return 0;
        outDelta = static_cast<int32_t>(buffer[1] | (buffer[2] << 8) | 
                                       (buffer[3] << 16) | (buffer[4] << 24));
        return 5;
    } else {
        outDelta = static_cast<int8_t>(marker);
        return 1;
    }
}

size_t encodePositionDelta(uint8_t* buffer, size_t bufferSize,
                           const Position& current, const Position& baseline) {
    size_t offset = 0;
    
    int32_t dx = current.x - baseline.x;
    size_t written = writeVarDelta(buffer + offset, bufferSize - offset, dx);
    if (written == 0) return 0;
    offset += written;
    
    int32_t dy = current.y - baseline.y;
    written = writeVarDelta(buffer + offset, bufferSize - offset, dy);
    if (written == 0) return 0;
    offset += written;
    
    int32_t dz = current.z - baseline.z;
    written = writeVarDelta(buffer + offset, bufferSize - offset, dz);
    if (written == 0) return 0;
    offset += written;
    
    return offset;
}

size_t decodePositionDelta(const uint8_t* buffer, size_t bufferSize,
                           Position& outPosition, const Position& baseline) {
    size_t offset = 0;
    int32_t delta;
    
    size_t read = readVarDelta(buffer + offset, bufferSize - offset, delta);
    if (read == 0) return 0;
    outPosition.x = baseline.x + delta;
    offset += read;
    
    read = readVarDelta(buffer + offset, bufferSize - offset, delta);
    if (read == 0) return 0;
    outPosition.y = baseline.y + delta;
    offset += read;
    
    read = readVarDelta(buffer + offset, bufferSize - offset, delta);
    if (read == 0) return 0;
    outPosition.z = baseline.z + delta;
    offset += read;
    
    return offset;
}

} // namespace DeltaEncoding

// ============================================================================
// Delta Compression Implementation
// ============================================================================

#pragma pack(push, 1)
struct SnapshotHeader {
    uint32_t serverTick;
    uint32_t baselineTick;
    uint16_t entityCount;
    uint16_t removedCount;
    uint32_t flags;
};
#pragma pack(pop)

static_assert(sizeof(SnapshotHeader) == 16, "SnapshotHeader must be 16 bytes");

std::vector<uint8_t> createDeltaSnapshot(
    uint32_t serverTick,
    uint32_t baselineTick,
    std::span<const EntityStateData> currentEntities,
    std::span<const EntityID> removedEntities,
    std::span<const EntityStateData> baselineEntities) {
    
    // Build lookup for baseline entities
    std::unordered_map<EntityID, const EntityStateData*> baselineMap;
    for (const auto& entity : baselineEntities) {
        baselineMap[entity.entity] = &entity;
    }
    
    // Find changed entities
    std::vector<DeltaEntityState> deltas;
    deltas.reserve(currentEntities.size());
    
    for (const auto& current : currentEntities) {
        auto it = baselineMap.find(current.entity);
        
        DeltaEntityState delta;
        delta.entity = current.entity;
        delta.changedFields = 0;
        
        if (it == baselineMap.end()) {
            // New entity - send full state
            delta.changedFields = DELTA_NEW_ENTITY;
            delta.position = current.position;
            delta.rotation = current.rotation;
            delta.velocity = current.velocity;
            delta.healthPercent = current.healthPercent;
            delta.animState = current.animState;
            delta.entityType = current.entityType;
            deltas.push_back(delta);
        } else {
            // Compare with baseline
            const EntityStateData& baseline = *it->second;
            
            if (!current.equalsPosition(baseline)) {
                delta.changedFields |= DELTA_POSITION;
                delta.position = current.position;
            }
            if (!current.equalsRotation(baseline)) {
                delta.changedFields |= DELTA_ROTATION;
                delta.rotation = current.rotation;
            }
            if (!current.equalsVelocity(baseline)) {
                delta.changedFields |= DELTA_VELOCITY;
                delta.velocity = current.velocity;
            }
            if (current.healthPercent != baseline.healthPercent) {
                delta.changedFields |= DELTA_HEALTH;
                delta.healthPercent = current.healthPercent;
            }
            if (current.animState != baseline.animState) {
                delta.changedFields |= DELTA_ANIM_STATE;
                delta.animState = current.animState;
            }
            if (current.entityType != baseline.entityType) {
                delta.changedFields |= DELTA_ENTITY_TYPE;
                delta.entityType = current.entityType;
            }
            
            if (delta.changedFields != 0) {
                deltas.push_back(delta);
            }
        }
    }
    
    // Calculate buffer size needed
    size_t estimatedSize = sizeof(SnapshotHeader);
    estimatedSize += deltas.size() * (sizeof(uint32_t) + sizeof(uint16_t) + 32);
    estimatedSize += removedEntities.size() * sizeof(uint32_t);
    
    std::vector<uint8_t> data;
    data.reserve(estimatedSize);
    
    // Write header
    SnapshotHeader header{};
    header.serverTick = serverTick;
    header.baselineTick = baselineTick;
    header.entityCount = static_cast<uint16_t>(deltas.size());
    header.removedCount = static_cast<uint16_t>(removedEntities.size());
    header.flags = 0;
    
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), headerBytes, headerBytes + sizeof(header));
    
    // Write delta entities
    for (const auto& delta : deltas) {
        // Entity ID
        uint32_t entityId = static_cast<uint32_t>(delta.entity);
        const uint8_t* idBytes = reinterpret_cast<const uint8_t*>(&entityId);
        data.insert(data.end(), idBytes, idBytes + sizeof(entityId));
        
        // Changed fields mask
        uint16_t mask = delta.changedFields;
        const uint8_t* maskBytes = reinterpret_cast<const uint8_t*>(&mask);
        data.insert(data.end(), maskBytes, maskBytes + sizeof(mask));
        
        // Position (if changed)
        if (delta.changedFields & DELTA_POSITION) {
            auto it = baselineMap.find(delta.entity);
            if (it != baselineMap.end()) {
                uint8_t buffer[16];
                size_t encoded = DeltaEncoding::encodePositionDelta(
                    buffer, sizeof(buffer), delta.position, it->second->position);
                data.insert(data.end(), buffer, buffer + encoded);
            } else {
                int16_t x = static_cast<int16_t>(delta.position.x / 16);
                int16_t y = static_cast<int16_t>(delta.position.y / 16);
                int16_t z = static_cast<int16_t>(delta.position.z / 16);
                data.push_back(static_cast<uint8_t>(x & 0xFF));
                data.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>(y & 0xFF));
                data.push_back(static_cast<uint8_t>((y >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>(z & 0xFF));
                data.push_back(static_cast<uint8_t>((z >> 8) & 0xFF));
            }
        }
        
        // Rotation (if changed)
        if (delta.changedFields & DELTA_ROTATION) {
            int8_t yaw = static_cast<int8_t>(delta.rotation.yaw * 40.584f);
            int8_t pitch = static_cast<int8_t>(delta.rotation.pitch * 40.584f);
            data.push_back(static_cast<uint8_t>(yaw));
            data.push_back(static_cast<uint8_t>(pitch));
        }
        
        // Velocity (if changed)
        if (delta.changedFields & DELTA_VELOCITY) {
            int16_t dx = static_cast<int16_t>(delta.velocity.dx / 4);
            int16_t dy = static_cast<int16_t>(delta.velocity.dy / 4);
            int16_t dz = static_cast<int16_t>(delta.velocity.dz / 4);
            data.push_back(static_cast<uint8_t>(dx & 0xFF));
            data.push_back(static_cast<uint8_t>((dx >> 8) & 0xFF));
            data.push_back(static_cast<uint8_t>(dy & 0xFF));
            data.push_back(static_cast<uint8_t>((dy >> 8) & 0xFF));
            data.push_back(static_cast<uint8_t>(dz & 0xFF));
            data.push_back(static_cast<uint8_t>((dz >> 8) & 0xFF));
        }
        
        // Health (if changed)
        if (delta.changedFields & DELTA_HEALTH) {
            data.push_back(delta.healthPercent);
        }
        
        // Animation state (if changed)
        if (delta.changedFields & DELTA_ANIM_STATE) {
            data.push_back(delta.animState);
        }
        
        // Entity type (if changed)
        if (delta.changedFields & DELTA_ENTITY_TYPE) {
            data.push_back(delta.entityType);
        }
    }
    
    // Write removed entities
    for (EntityID entity : removedEntities) {
        uint32_t entityId = static_cast<uint32_t>(entity);
        const uint8_t* idBytes = reinterpret_cast<const uint8_t*>(&entityId);
        data.insert(data.end(), idBytes, idBytes + sizeof(entityId));
    }
    
    return data;
}

bool applyDeltaSnapshot(
    std::span<const uint8_t> data,
    std::vector<EntityStateData>& outEntities,
    uint32_t& outServerTick,
    uint32_t& outBaselineTick,
    std::vector<EntityID>& outRemovedEntities) {
    
    if (data.size() < sizeof(SnapshotHeader)) {
        return false;
    }
    
    const SnapshotHeader* header = reinterpret_cast<const SnapshotHeader*>(data.data());
    outServerTick = header->serverTick;
    outBaselineTick = header->baselineTick;
    
    size_t offset = sizeof(SnapshotHeader);
    
    // Build lookup for baseline entities
    std::unordered_map<EntityID, EntityStateData> baselineMap;
    for (const auto& entity : outEntities) {
        baselineMap[entity.entity] = entity;
    }
    outEntities.clear();
    outEntities.reserve(header->entityCount);
    
    // Parse delta entities
    for (uint16_t i = 0; i < header->entityCount; ++i) {
        if (offset + sizeof(uint32_t) + sizeof(uint16_t) > data.size()) {
            return false;
        }
        
        EntityID entity = static_cast<EntityID>(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += sizeof(uint32_t);
        
        uint16_t mask = *reinterpret_cast<const uint16_t*>(data.data() + offset);
        offset += sizeof(uint16_t);
        
        EntityStateData state;
        auto it = baselineMap.find(entity);
        if (it != baselineMap.end()) {
            state = it->second;
        } else {
            state.entity = entity;
        }
        
        // Read position (if present)
        if (mask & DELTA_POSITION) {
            if (it != baselineMap.end()) {
                size_t decoded = DeltaEncoding::decodePositionDelta(
                    data.data() + offset, data.size() - offset, state.position, it->second.position);
                if (decoded == 0) return false;
                offset += decoded;
            } else {
                if (offset + 6 > data.size()) return false;
                int16_t x = static_cast<int16_t>(data[offset] | (data[offset + 1] << 8));
                int16_t y = static_cast<int16_t>(data[offset + 2] | (data[offset + 3] << 8));
                int16_t z = static_cast<int16_t>(data[offset + 4] | (data[offset + 5] << 8));
                state.position.x = x * 16;
                state.position.y = y * 16;
                state.position.z = z * 16;
                offset += 6;
            }
        }
        
        // Read rotation (if present)
        if (mask & DELTA_ROTATION) {
            if (offset + 2 > data.size()) return false;
            state.rotation.yaw = static_cast<int8_t>(data[offset]) / 40.584f;
            state.rotation.pitch = static_cast<int8_t>(data[offset + 1]) / 40.584f;
            offset += 2;
        }
        
        // Read velocity (if present)
        if (mask & DELTA_VELOCITY) {
            if (offset + 6 > data.size()) return false;
            int16_t dx = static_cast<int16_t>(data[offset] | (data[offset + 1] << 8));
            int16_t dy = static_cast<int16_t>(data[offset + 2] | (data[offset + 3] << 8));
            int16_t dz = static_cast<int16_t>(data[offset + 4] | (data[offset + 5] << 8));
            state.velocity.dx = dx * 4;
            state.velocity.dy = dy * 4;
            state.velocity.dz = dz * 4;
            offset += 6;
        }
        
        // Read health (if present)
        if (mask & DELTA_HEALTH) {
            if (offset + 1 > data.size()) return false;
            state.healthPercent = data[offset];
            offset += 1;
        }
        
        // Read animation state (if present)
        if (mask & DELTA_ANIM_STATE) {
            if (offset + 1 > data.size()) return false;
            state.animState = data[offset];
            offset += 1;
        }
        
        // Read entity type (if present)
        if (mask & DELTA_ENTITY_TYPE) {
            if (offset + 1 > data.size()) return false;
            state.entityType = data[offset];
            offset += 1;
        }
        
        outEntities.push_back(state);
    }
    
    // Parse removed entities
    outRemovedEntities.clear();
    outRemovedEntities.reserve(header->removedCount);
    for (uint16_t i = 0; i < header->removedCount; ++i) {
        if (offset + sizeof(uint32_t) > data.size()) {
            return false;
        }
        EntityID entity = static_cast<EntityID>(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        outRemovedEntities.push_back(entity);
        offset += sizeof(uint32_t);
    }
    
    return true;
}

} // namespace Protocol

// [SECURITY_AGENT] Check if connection should be accepted
bool NetworkManager::shouldAcceptConnection(const std::string& ipAddress) {
    return ddosProtection_.shouldAcceptConnection(ipAddress);
}

// [SECURITY_AGENT] Process packet with DDoS protection
bool NetworkManager::processPacket(ConnectionID connectionId, 
                                   const std::string& ipAddress,
                                   uint32_t packetSize,
                                   uint32_t currentTimeMs) {
    return ddosProtection_.processPacket(connectionId, ipAddress, packetSize, currentTimeMs);
}

} // namespace DarkAges
