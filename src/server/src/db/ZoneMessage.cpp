// ZoneMessage serialization - always compiled (no Redis dependency)
#include "db/PubSubManager.hpp"
#include <cstring>

namespace DarkAges {

std::vector<uint8_t> ZoneMessage::serialize() const {
    std::vector<uint8_t> data;
    
    // Header: type(1) + source(4) + target(4) + timestamp(4) + sequence(4) + payload_size(4)
    size_t headerSize = 1 + 4 + 4 + 4 + 4 + 4;
    data.resize(headerSize + payload.size());
    
    size_t offset = 0;
    data[offset++] = static_cast<uint8_t>(type);
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = sourceZoneId;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = targetZoneId;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = timestamp;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = sequence;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = static_cast<uint32_t>(payload.size());
    offset += 4;
    
    std::memcpy(&data[offset], payload.data(), payload.size());
    
    return data;
}

std::optional<ZoneMessage> ZoneMessage::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 21) {  // Minimum header size
        return std::nullopt;
    }
    
    ZoneMessage msg;
    size_t offset = 0;
    
    msg.type = static_cast<ZoneMessageType>(data[offset++]);
    
    msg.sourceZoneId = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.targetZoneId = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.timestamp = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.sequence = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    uint32_t payloadSize = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    if (data.size() < offset + payloadSize) {
        return std::nullopt;
    }
    
    msg.payload.assign(data.begin() + offset, data.begin() + offset + payloadSize);
    
    return msg;
}

} // namespace DarkAges
