#pragma once

// [SECURITY_AGENT] Input validation for all network packets
// Prevents exploits, malformed data, and injection attacks

#include "ecs/CoreTypes.hpp"
#include <string>
#include <cstdint>

namespace DarkAges {
namespace Security {

// Validation result
enum class ValidationResult {
    Valid,
    Invalid_Position,
    Invalid_Rotation,
    Invalid_Speed,
    Invalid_EntityId,
    Invalid_AbilityId,
    Invalid_PlayerName,
    Invalid_ChatMessage,
    Rate_Limit_Exceeded,
    Suspicious_Pattern
};

// Forward declarations
struct ClientInputPacket;

/**
 * Validates all incoming network data
 * 
 * All validation methods:
 * - Return true if valid
 * - Return false if invalid (logs reason)
 * - Are designed to be fast (no allocations)
 * - Clamp values rather than reject when possible
 */
class PacketValidator {
public:
    // =========================================================================
    // Position Validation
    // =========================================================================
    
    /**
     * Validate world position coordinates
     * Clamps to world bounds if out of range
     */
    static bool ValidatePosition(Position& pos);
    
    /**
     * Check if position is within world bounds
     */
    [[nodiscard]] static bool IsPositionInBounds(const Position& pos);
    
    /**
     * Clamp position to world bounds
     */
    static void ClampPosition(Position& pos);
    
    // =========================================================================
    // Movement Validation
    // =========================================================================
    
    /**
     * Validate movement speed
     * Checks against maximum allowed speed + tolerance
     */
    [[nodiscard]] static bool ValidateSpeed(float speed);
    
    /**
     * Validate position change between ticks
     * Detects speed hacks and teleportation
     */
    [[nodiscard]] static bool ValidatePositionDelta(
        const Position& oldPos,
        const Position& newPos,
        uint32_t deltaTimeMs,
        float maxSpeed
    );
    
    // =========================================================================
    // Rotation Validation
    // =========================================================================
    
    /**
     * Validate rotation angles
     * Yaw: 0-360 degrees
     * Pitch: -90 to 90 degrees
     */
    static bool ValidateRotation(float& yaw, float& pitch);
    
    /**
     * Validate rotation change rate
     * Detects aimbot (instant rotation)
     */
    [[nodiscard]] static bool ValidateRotationDelta(
        float oldYaw, float oldPitch,
        float newYaw, float newPitch,
        uint32_t deltaTimeMs
    );
    
    // =========================================================================
    // Entity Validation
    // =========================================================================
    
    /**
     * Validate entity ID exists in registry
     */
    [[nodiscard]] static bool ValidateEntityId(EntityID entityId, const Registry& registry);
    
    /**
     * Validate target entity is valid for attack
     */
    [[nodiscard]] static bool ValidateAttackTarget(
        EntityID attacker,
        EntityID target,
        const Registry& registry,
        float maxRange
    );
    
    // =========================================================================
    // Ability Validation
    // =========================================================================
    
    /**
     * Validate ability ID is in valid range
     */
    [[nodiscard]] static bool ValidateAbilityId(uint32_t abilityId);
    
    /**
     * Validate ability can be used (cooldown, mana, etc.)
     */
    [[nodiscard]] static bool ValidateAbilityUse(
        EntityID entity,
        uint32_t abilityId,
        const Registry& registry
    );
    
    // =========================================================================
    // Text Validation
    // =========================================================================
    
    /**
     * Validate and sanitize player name
     * - Length limits
     * - Character whitelist
     * - Profanity filter
     */
    static bool ValidatePlayerName(std::string& name);
    
    /**
     * Validate and sanitize chat message
     * - Length limits
     * - Rate limiting
     * - Spam detection
     */
    static bool ValidateChatMessage(std::string& message);
    
    /**
     * Check for suspicious patterns in text
     * - Repeated characters
     * - Excessive caps
     * - Known exploit strings
     */
    [[nodiscard]] static bool HasSuspiciousPattern(const std::string& text);
    
    // =========================================================================
    // Packet Validation
    // =========================================================================
    
    /**
     * Validate complete client input packet
     * This is the main entry point for input validation
     */
    [[nodiscard]] static ValidationResult ValidateClientInput(
        const ClientInputPacket& input,
        EntityID entity,
        const Registry& registry,
        uint32_t serverTick
    );
    
    /**
     * Validate serialized packet data
     * Checks size limits and basic structure
     */
    [[nodiscard]] static bool ValidatePacketData(const void* data, size_t size);
    
    // =========================================================================
    // Limits & Constants
    // =========================================================================
    
    static constexpr float WORLD_MIN_X = -10000.0f;
    static constexpr float WORLD_MAX_X = 10000.0f;
    static constexpr float WORLD_MIN_Y = -1000.0f;
    static constexpr float WORLD_MAX_Y = 1000.0f;
    static constexpr float WORLD_MIN_Z = -10000.0f;
    static constexpr float WORLD_MAX_Z = 10000.0f;
    
    static constexpr float MAX_SPEED = 20.0f;           // meters/second
    static constexpr float MAX_SPEED_TOLERANCE = 1.1f;  // 10% tolerance
    static constexpr float MAX_ROTATION_RATE = 720.0f;  // degrees/second
    
    static constexpr uint32_t MAX_ABILITY_ID = 1000;
    static constexpr float MAX_ATTACK_RANGE = 50.0f;
    
    static constexpr size_t MAX_PLAYER_NAME_LENGTH = 32;
    static constexpr size_t MAX_CHAT_MESSAGE_LENGTH = 256;
    static constexpr size_t MAX_PACKET_SIZE = 4096;
    
    static constexpr size_t MIN_INPUT_SEQUENCE_DELTA = 1;
    static constexpr size_t MAX_INPUT_SEQUENCE_DELTA = 10;
};

} // namespace Security
} // namespace DarkAges
