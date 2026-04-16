#pragma once
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <string>
#include <cstddef>
#include <cstdint>

namespace DarkAges {
namespace Security {

// [SECURITY_AGENT] Input validation
class InputValidator {
public:
    // Validate position coordinates
    static bool isValidPosition(const Position& pos);
    
    // Validate rotation values
    static bool isValidRotation(const Rotation& rot);
    
    // Validate input sequence
    static bool isValidInputSequence(uint32_t sequence, uint32_t lastSequence);
    
    // Sanitize string input
    static std::string sanitizeString(const std::string& input, size_t maxLength);
    
    // Validate packet size
    static bool isValidPacketSize(size_t size);
    
    // Validate protocol version
    static bool isValidProtocolVersion(uint32_t version);

private:
    static constexpr size_t MAX_PACKET_SIZE = 1400;
    static constexpr size_t MAX_STRING_LENGTH = 256;
};

} // namespace Security
} // namespace DarkAges
