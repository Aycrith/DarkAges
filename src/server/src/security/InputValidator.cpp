#include "security/InputValidator.hpp"
#include <algorithm>
#include <string>
#include <cstdint>

namespace DarkAges {
namespace Security {

bool InputValidator::isValidPosition(const Position& pos) {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    return x >= Constants::WORLD_MIN_X && x <= Constants::WORLD_MAX_X &&
           y >= Constants::WORLD_MIN_Y && y <= Constants::WORLD_MAX_Y &&
           z >= Constants::WORLD_MIN_Z && z <= Constants::WORLD_MAX_Z;
}

bool InputValidator::isValidRotation(const Rotation& rot) {
    return rot.yaw >= -6.28318f && rot.yaw <= 6.28318f &&  // ±2π
           rot.pitch >= -1.57079f && rot.pitch <= 1.57079f;  // ±π/2
}

bool InputValidator::isValidInputSequence(uint32_t sequence, uint32_t lastSequence) {
    // Allow for some packet reordering (within 100 packets)
    // Also handle wraparound
    if (sequence > lastSequence) {
        return (sequence - lastSequence) < 1000;  // Normal progression
    } else if (sequence < lastSequence) {
        // Possible wraparound or very old packet
        return (lastSequence - sequence) > 0xFFFFFFFF - 1000;  // Wraparound
    }
    return false;  // Duplicate
}

std::string InputValidator::sanitizeString(const std::string& input, 
                                          size_t maxLength) {
    std::string result;
    result.reserve(std::min(input.size(), maxLength));
    
    for (char c : input) {
        // Allow printable ASCII only
        if (c >= 32 && c < 127 && result.size() < maxLength) {
            result += c;
        }
    }
    
    return result;
}

bool InputValidator::isValidPacketSize(size_t size) {
    return size > 0 && size <= MAX_PACKET_SIZE;
}

bool InputValidator::isValidProtocolVersion(uint32_t version) {
    return version == Constants::PROTOCOL_VERSION;
}

} // namespace Security
} // namespace DarkAges
