// [SECURITY_AGENT] Input validation implementation
// Prevents exploits, injection attacks, and cheating

#include "security/PacketValidator.hpp"
#include "ecs/CoreTypes.hpp"
#include "ecs/Components.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>

namespace DarkAges {
namespace Security {

// ============================================================================
// Position Validation
// ============================================================================

bool PacketValidator::ValidatePosition(Position& pos) {
    // Check for NaN or Infinity
    if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z) ||
        std::isinf(pos.x) || std::isinf(pos.y) || std::isinf(pos.z)) {
        std::cerr << "[SECURITY] Invalid position: NaN or Infinity detected\n";
        pos = Position{0, 0, 0}; // Reset to origin
        return false;
    }
    
    // Clamp to world bounds
    ClampPosition(pos);
    return true;
}

bool PacketValidator::IsPositionInBounds(const Position& pos) {
    return pos.x >= WORLD_MIN_X && pos.x <= WORLD_MAX_X &&
           pos.y >= WORLD_MIN_Y && pos.y <= WORLD_MAX_Y &&
           pos.z >= WORLD_MIN_Z && pos.z <= WORLD_MAX_Z;
}

void PacketValidator::ClampPosition(Position& pos) {
    pos.x = std::max(WORLD_MIN_X, std::min(WORLD_MAX_X, pos.x));
    pos.y = std::max(WORLD_MIN_Y, std::min(WORLD_MAX_Y, pos.y));
    pos.z = std::max(WORLD_MIN_Z, std::min(WORLD_MAX_Z, pos.z));
}

// ============================================================================
// Movement Validation
// ============================================================================

bool PacketValidator::ValidateSpeed(float speed) {
    // Check for NaN/Inf
    if (std::isnan(speed) || std::isinf(speed)) {
        return false;
    }
    
    // Allow small tolerance for floating point errors
    float maxAllowed = MAX_SPEED * MAX_SPEED_TOLERANCE;
    if (speed > maxAllowed) {
        std::cerr << "[SECURITY] Speed violation: " << speed << " > " << maxAllowed << "\n";
        return false;
    }
    
    return true;
}

bool PacketValidator::ValidatePositionDelta(
    const Position& oldPos,
    const Position& newPos,
    uint32_t deltaTimeMs,
    float maxSpeed) {
    
    // Prevent division by zero
    if (deltaTimeMs == 0) {
        return oldPos.x == newPos.x && oldPos.y == newPos.y && oldPos.z == newPos.z;
    }
    
    // Calculate distance
    float dx = newPos.x - oldPos.x;
    float dy = newPos.y - oldPos.y;
    float dz = newPos.z - oldPos.z;
    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    // Calculate time in seconds
    float deltaTimeSec = deltaTimeMs / 1000.0f;
    
    // Calculate max allowed distance
    float maxDistance = maxSpeed * deltaTimeSec * MAX_SPEED_TOLERANCE;
    
    if (distance > maxDistance) {
        // Calculate actual speed for logging
        float actualSpeed = distance / deltaTimeSec;
        std::cerr << "[SECURITY] Position delta violation: moved " << distance 
                  << "m in " << deltaTimeSec << "s (speed=" << actualSpeed 
                  << "m/s, max=" << maxSpeed << "m/s)\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Rotation Validation
// ============================================================================

bool PacketValidator::ValidateRotation(float& yaw, float& pitch) {
    // Check for NaN/Inf
    if (std::isnan(yaw) || std::isinf(yaw) || std::isnan(pitch) || std::isinf(pitch)) {
        std::cerr << "[SECURITY] Invalid rotation: NaN or Infinity\n";
        yaw = 0.0f;
        pitch = 0.0f;
        return false;
    }
    
    // Normalize yaw to 0-360
    yaw = std::fmod(yaw, 360.0f);
    if (yaw < 0) yaw += 360.0f;
    
    // Clamp pitch to -90 to 90
    pitch = std::max(-90.0f, std::min(90.0f, pitch));
    
    return true;
}

bool PacketValidator::ValidateRotationDelta(
    float oldYaw, float oldPitch,
    float newYaw, float newPitch,
    uint32_t deltaTimeMs) {
    
    if (deltaTimeMs == 0) {
        return true; // No time passed, any rotation is valid
    }
    
    // Calculate yaw delta (handle wraparound)
    float yawDelta = std::abs(newYaw - oldYaw);
    if (yawDelta > 180.0f) {
        yawDelta = 360.0f - yawDelta; // Shorter direction
    }
    
    float pitchDelta = std::abs(newPitch - oldPitch);
    float deltaTimeSec = deltaTimeMs / 1000.0f;
    
    // Calculate rotation rates
    float yawRate = yawDelta / deltaTimeSec;
    float pitchRate = pitchDelta / deltaTimeSec;
    
    if (yawRate > MAX_ROTATION_RATE || pitchRate > MAX_ROTATION_RATE) {
        std::cerr << "[SECURITY] Rotation rate violation: yaw=" << yawRate 
                  << "deg/s, pitch=" << pitchRate << "deg/s (max=" 
                  << MAX_ROTATION_RATE << "deg/s)\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Entity Validation
// ============================================================================

bool PacketValidator::ValidateEntityId(EntityID entityId, const Registry& registry) {
    // Check if entity ID is valid
    if (entityId == 0 || entityId == EntityID(-1)) {
        return false;
    }
    
    // Check if entity exists in registry
    if (!registry.valid(entityId)) {
        std::cerr << "[SECURITY] Invalid entity ID: " << static_cast<uint32_t>(entityId) << "\n";
        return false;
    }
    
    return true;
}

bool PacketValidator::ValidateAttackTarget(
    EntityID attacker,
    EntityID target,
    const Registry& registry,
    float maxRange) {
    
    // Both entities must exist
    if (!ValidateEntityId(attacker, registry) || !ValidateEntityId(target, registry)) {
        return false;
    }
    
    // Cannot attack self
    if (attacker == target) {
        std::cerr << "[SECURITY] Self-attack attempt\n";
        return false;
    }
    
    // Check range
    const Position* attackerPos = registry.try_get<Position>(attacker);
    const Position* targetPos = registry.try_get<Position>(target);
    
    if (!attackerPos || !targetPos) {
        std::cerr << "[SECURITY] Attack target missing position component\n";
        return false;
    }
    
    float dx = targetPos->x - attackerPos->x;
    float dy = targetPos->y - attackerPos->y;
    float dz = targetPos->z - attackerPos->z;
    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    if (distance > maxRange * 1.1f) { // 10% tolerance
        std::cerr << "[SECURITY] Attack range violation: " << distance 
                  << "m > " << maxRange << "m\n";
        return false;
    }
    
    // Check if target is alive
    const Health* targetHealth = registry.try_get<Health>(target);
    if (targetHealth && targetHealth->current <= 0) {
        std::cerr << "[SECURITY] Attack on dead entity\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Ability Validation
// ============================================================================

bool PacketValidator::ValidateAbilityId(uint32_t abilityId) {
    if (abilityId == 0 || abilityId > MAX_ABILITY_ID) {
        std::cerr << "[SECURITY] Invalid ability ID: " << abilityId << "\n";
        return false;
    }
    return true;
}

bool PacketValidator::ValidateAbilityUse(
    EntityID entity,
    uint32_t abilityId,
    const Registry& registry) {
    
    if (!ValidateAbilityId(abilityId)) {
        return false;
    }
    
    if (!ValidateEntityId(entity, registry)) {
        return false;
    }
    
    // Check if entity has ability component
    const Abilities* abilities = registry.try_get<Abilities>(entity);
    if (!abilities) {
        std::cerr << "[SECURITY] Entity has no abilities component\n";
        return false;
    }
    
    // Check if ability is known to entity
    bool hasAbility = false;
    for (const auto& ability : abilities->abilities) {
        if (ability.abilityId == abilityId) {
            hasAbility = true;
            
            // Check cooldown
            if (ability.cooldownRemaining > 0) {
                std::cerr << "[SECURITY] Ability on cooldown: " << abilityId 
                          << " (" << ability.cooldownRemaining << "s remaining)\n";
                return false;
            }
            
            // Check mana/resource
            const Mana* mana = registry.try_get<Mana>(entity);
            if (mana && ability.manaCost > mana->current) {
                std::cerr << "[SECURITY] Insufficient mana for ability: " << abilityId << "\n";
                return false;
            }
            
            break;
        }
    }
    
    if (!hasAbility) {
        std::cerr << "[SECURITY] Entity doesn't have ability: " << abilityId << "\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Text Validation
// ============================================================================

// Whitelist of allowed characters for player names
static bool IsValidNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || 
           c == '_' || c == '-' || c == '.';
}

bool PacketValidator::ValidatePlayerName(std::string& name) {
    // Check length
    if (name.empty()) {
        name = "Player";
        return true; // Auto-fix
    }
    
    if (name.length() > MAX_PLAYER_NAME_LENGTH) {
        name = name.substr(0, MAX_PLAYER_NAME_LENGTH);
    }
    
    // Remove invalid characters
    name.erase(
        std::remove_if(name.begin(), name.end(),
            [](char c) { return !IsValidNameChar(c); }),
        name.end()
    );
    
    // Check for empty after sanitization
    if (name.empty()) {
        name = "Player";
    }
    
    // Check for suspicious patterns
    if (HasSuspiciousPattern(name)) {
        std::cerr << "[SECURITY] Suspicious player name detected: " << name << "\n";
        name = "Player";
        return false;
    }
    
    return true;
}

bool PacketValidator::ValidateChatMessage(std::string& message) {
    // Check length
    if (message.length() > MAX_CHAT_MESSAGE_LENGTH) {
        message = message.substr(0, MAX_CHAT_MESSAGE_LENGTH);
    }
    
    // Remove control characters
    message.erase(
        std::remove_if(message.begin(), message.end(),
            [](char c) { 
                unsigned char uc = static_cast<unsigned char>(c);
                return uc < 32 && uc != '\t' && uc != '\n'; // Keep tabs and newlines
            }),
        message.end()
    );
    
    // Trim whitespace
    auto start = message.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        message.clear();
        return true;
    }
    auto end = message.find_last_not_of(" \t\n\r");
    message = message.substr(start, end - start + 1);
    
    // Check for suspicious patterns
    if (HasSuspiciousPattern(message)) {
        std::cerr << "[SECURITY] Suspicious chat message detected\n";
        return false;
    }
    
    return true;
}

bool PacketValidator::HasSuspiciousPattern(const std::string& text) {
    // Check for excessive repetition (spam)
    int maxRepeat = 0;
    int currentRepeat = 1;
    char prevChar = '\0';
    
    for (char c : text) {
        char lower = std::tolower(static_cast<unsigned char>(c));
        if (lower == prevChar) {
            currentRepeat++;
            maxRepeat = std::max(maxRepeat, currentRepeat);
        } else {
            currentRepeat = 1;
            prevChar = lower;
        }
    }
    
    // More than 5 repeated characters is suspicious
    if (maxRepeat > 5) {
        return true;
    }
    
    // Check for excessive caps (shouting)
    int upperCount = 0;
    int lowerCount = 0;
    for (char c : text) {
        if (std::isupper(static_cast<unsigned char>(c))) upperCount++;
        else if (std::islower(static_cast<unsigned char>(c))) lowerCount++;
    }
    
    // More than 70% caps is suspicious (if more than 5 chars)
    if (text.length() > 5 && upperCount > (upperCount + lowerCount) * 0.7) {
        return true;
    }
    
    // Check for known exploit strings (case-insensitive)
    std::string lowerText;
    lowerText.reserve(text.length());
    for (char c : text) {
        lowerText += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    // SQL injection patterns
    const char* sqlPatterns[] = {
        "'", "--", ";", "/*", "*/", "xp_", "sp_", "exec(", "select ", "insert ",
        "update ", "delete ", "drop ", "union ", "script>", "javascript:",
        "onerror=", "onload=", "alert(", "eval(", "document.", "window.",
        "<iframe", "<object", "<embed", "<form", "<input", "<textarea"
    };
    
    for (const char* pattern : sqlPatterns) {
        if (lowerText.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// ============================================================================
// Packet Validation
// ============================================================================

bool PacketValidator::ValidatePacketData(const void* data, size_t size) {
    // Check for null
    if (!data) {
        std::cerr << "[SECURITY] Null packet data\n";
        return false;
    }
    
    // Check size limits
    if (size == 0) {
        std::cerr << "[SECURITY] Empty packet\n";
        return false;
    }
    
    if (size > MAX_PACKET_SIZE) {
        std::cerr << "[SECURITY] Packet too large: " << size << " > " << MAX_PACKET_SIZE << "\n";
        return false;
    }
    
    return true;
}

// Stub for ClientInputPacket - would be implemented with actual packet structure
ValidationResult PacketValidator::ValidateClientInput(
    const void* input,
    EntityID entity,
    const Registry& registry,
    uint32_t serverTick) {
    
    // This is a placeholder - actual implementation would validate:
    // - Sequence number is in valid range
    // - Timestamp is reasonable (not too far in future/past)
    // - Position delta is valid
    // - Speed is valid
    // - etc.
    
    if (!ValidateEntityId(entity, registry)) {
        return ValidationResult::Invalid_EntityId;
    }
    
    return ValidationResult::Valid;
}

} // namespace Security
} // namespace DarkAges
