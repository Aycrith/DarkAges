// Stub implementation for NetworkManager when GNS is not available
#include "netcode/NetworkManager.hpp"

#include <cstring>

namespace DarkAges {

// Stub definition for internal struct
struct GNSInternal {};

NetworkManager::NetworkManager() 
    : internal_(std::make_unique<GNSInternal>())
    , ddosProtection_() {}
NetworkManager::~NetworkManager() = default;

bool NetworkManager::initialize(uint16_t) { return true; }
void NetworkManager::update(uint32_t) {}
void NetworkManager::shutdown() {}
void NetworkManager::sendSnapshot(ConnectionID, std::span<const uint8_t>) {}
void NetworkManager::sendEvent(ConnectionID, std::span<const uint8_t>) {}
void NetworkManager::broadcastSnapshot(std::span<const uint8_t>) {}
void NetworkManager::broadcastEvent(std::span<const uint8_t>) {}
void NetworkManager::sendToMultiple(const std::vector<ConnectionID>&, std::span<const uint8_t>) {}
void NetworkManager::disconnect(ConnectionID, const char*) {}
std::vector<ClientInputPacket> NetworkManager::getPendingInputs() { return {}; }
void NetworkManager::clearProcessedInputs(uint32_t) {}
ConnectionQuality NetworkManager::getConnectionQuality(ConnectionID) const { return {}; }
bool NetworkManager::isConnected(ConnectionID) const { return false; }
size_t NetworkManager::getConnectionCount() const { return 0; }
uint64_t NetworkManager::getTotalBytesSent() const { return 0; }
uint64_t NetworkManager::getTotalBytesReceived() const { return 0; }
bool NetworkManager::isRateLimited(ConnectionID) const { return false; }
bool NetworkManager::shouldAcceptConnection(const std::string&) { return true; }
bool NetworkManager::processPacket(ConnectionID, const std::string&, uint32_t, uint32_t) { return true; }

// ============================================================================
// Protocol Stub Implementation
// ============================================================================

namespace Protocol {

std::vector<uint8_t> serializeInput(const InputState& input) {
    std::vector<uint8_t> data;
    data.reserve(17);  // 1 byte flags + 2 floats + 2 uint32
    
    // Pack flags
    uint8_t flags = 0;
    flags |= (input.forward  & 0x1) << 0;
    flags |= (input.backward & 0x1) << 1;
    flags |= (input.left     & 0x1) << 2;
    flags |= (input.right    & 0x1) << 3;
    flags |= (input.jump     & 0x1) << 4;
    flags |= (input.attack   & 0x1) << 5;
    flags |= (input.block    & 0x1) << 6;
    flags |= (input.sprint   & 0x1) << 7;
    
    data.push_back(flags);
    
    // Serialize floats and uint32s
    auto appendBytes = [&data](const void* ptr, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
        data.insert(data.end(), bytes, bytes + size);
    };
    
    appendBytes(&input.yaw, sizeof(float));
    appendBytes(&input.pitch, sizeof(float));
    appendBytes(&input.sequence, sizeof(uint32_t));
    appendBytes(&input.timestamp_ms, sizeof(uint32_t));
    
    return data;
}

bool deserializeInput(std::span<const uint8_t> data, InputState& outInput) {
    constexpr size_t MIN_SIZE = 1 + sizeof(float) * 2 + sizeof(uint32_t) * 2;
    
    if (data.size() < MIN_SIZE) {
        return false;
    }
    
    size_t offset = 0;
    
    // Unpack flags
    uint8_t flags = data[offset++];
    outInput.forward  = (flags >> 0) & 0x1;
    outInput.backward = (flags >> 1) & 0x1;
    outInput.left     = (flags >> 2) & 0x1;
    outInput.right    = (flags >> 3) & 0x1;
    outInput.jump     = (flags >> 4) & 0x1;
    outInput.attack   = (flags >> 5) & 0x1;
    outInput.block    = (flags >> 6) & 0x1;
    outInput.sprint   = (flags >> 7) & 0x1;
    
    std::memcpy(&outInput.yaw, &data[offset], sizeof(float));
    offset += sizeof(float);
    std::memcpy(&outInput.pitch, &data[offset], sizeof(float));
    offset += sizeof(float);
    std::memcpy(&outInput.sequence, &data[offset], sizeof(uint32_t));
    offset += sizeof(uint32_t);
    std::memcpy(&outInput.timestamp_ms, &data[offset], sizeof(uint32_t));
    
    return true;
}

bool EntityStateData::equalsPosition(const EntityStateData& other) const {
    return position.x == other.position.x && 
           position.y == other.position.y && 
           position.z == other.position.z;
}

bool EntityStateData::equalsRotation(const EntityStateData& other) const {
    return rotation.yaw == other.rotation.yaw && 
           rotation.pitch == other.rotation.pitch;
}

bool EntityStateData::equalsVelocity(const EntityStateData& other) const {
    return velocity.dx == other.velocity.dx && 
           velocity.dy == other.velocity.dy && 
           velocity.dz == other.velocity.dz;
}

std::vector<uint8_t> createDeltaSnapshot(
    uint32_t serverTick,
    uint32_t baselineTick,
    std::span<const EntityStateData> currentEntities,
    std::span<const EntityID> removedEntities,
    std::span<const EntityStateData> baselineEntities) {
    
    (void)baselineEntities;  // Unused in stub
    
    std::vector<uint8_t> data;
    
    // Header
    data.push_back(static_cast<uint8_t>(PacketType::ServerSnapshot));
    
    auto appendUInt32 = [&data](uint32_t value) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        data.insert(data.end(), bytes, bytes + sizeof(uint32_t));
    };
    
    appendUInt32(serverTick);
    appendUInt32(baselineTick);
    appendUInt32(static_cast<uint32_t>(currentEntities.size()));
    
    for (const auto& entity : currentEntities) {
        appendUInt32(static_cast<uint32_t>(entity.entity));
        
        uint16_t changedFields = 0xFFFF;  // All fields
        data.push_back(static_cast<uint8_t>(changedFields & 0xFF));
        data.push_back(static_cast<uint8_t>((changedFields >> 8) & 0xFF));
        
        appendUInt32(static_cast<uint32_t>(entity.position.x));
        appendUInt32(static_cast<uint32_t>(entity.position.y));
        appendUInt32(static_cast<uint32_t>(entity.position.z));
        appendUInt32(static_cast<uint32_t>(entity.velocity.dx));
        appendUInt32(static_cast<uint32_t>(entity.velocity.dy));
        appendUInt32(static_cast<uint32_t>(entity.velocity.dz));
        
        auto appendFloat = [&data](float value) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
            data.insert(data.end(), bytes, bytes + sizeof(float));
        };
        appendFloat(entity.rotation.yaw);
        appendFloat(entity.rotation.pitch);
        
        data.push_back(entity.healthPercent);
        data.push_back(entity.animState);
        data.push_back(entity.entityType);
    }
    
    appendUInt32(static_cast<uint32_t>(removedEntities.size()));
    for (const auto& entity : removedEntities) {
        appendUInt32(static_cast<uint32_t>(entity));
    }
    
    return data;
}

bool applyDeltaSnapshot(
    std::span<const uint8_t> data,
    std::vector<EntityStateData>& outEntities,
    uint32_t& outServerTick,
    uint32_t& outBaselineTick,
    std::vector<EntityID>& outRemovedEntities) {
    
    if (data.size() < 13) {
        return false;
    }
    
    size_t offset = 1;  // Skip packet type
    
    auto readUInt32 = [&data, &offset]() -> uint32_t {
        uint32_t value;
        std::memcpy(&value, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);
        return value;
    };
    
    outServerTick = readUInt32();
    outBaselineTick = readUInt32();
    
    uint32_t entityCount = readUInt32();
    
    outEntities.clear();
    outEntities.reserve(entityCount);
    
    for (uint32_t i = 0; i < entityCount; ++i) {
        if (offset + 4 > data.size()) return false;
        
        EntityStateData entity;
        entity.entity = static_cast<EntityID>(readUInt32());
        
        if (offset + 2 > data.size()) return false;
        offset += 2;  // Skip changed fields
        
        if (offset + 12 > data.size()) return false;
        entity.position.x = static_cast<int32_t>(readUInt32());
        entity.position.y = static_cast<int32_t>(readUInt32());
        entity.position.z = static_cast<int32_t>(readUInt32());
        
        if (offset + 12 > data.size()) return false;
        entity.velocity.dx = static_cast<int32_t>(readUInt32());
        entity.velocity.dy = static_cast<int32_t>(readUInt32());
        entity.velocity.dz = static_cast<int32_t>(readUInt32());
        
        if (offset + 8 > data.size()) return false;
        std::memcpy(&entity.rotation.yaw, &data[offset], sizeof(float));
        offset += sizeof(float);
        std::memcpy(&entity.rotation.pitch, &data[offset], sizeof(float));
        offset += sizeof(float);
        
        if (offset + 3 > data.size()) return false;
        entity.healthPercent = data[offset++];
        entity.animState = data[offset++];
        entity.entityType = data[offset++];
        
        outEntities.push_back(entity);
    }
    
    if (offset + 4 > data.size()) return false;
    uint32_t removedCount = readUInt32();
    
    outRemovedEntities.clear();
    outRemovedEntities.reserve(removedCount);
    
    for (uint32_t i = 0; i < removedCount; ++i) {
        if (offset + 4 > data.size()) return false;
        outRemovedEntities.push_back(static_cast<EntityID>(readUInt32()));
    }
    
    return true;
}

} // namespace Protocol

} // namespace DarkAges
