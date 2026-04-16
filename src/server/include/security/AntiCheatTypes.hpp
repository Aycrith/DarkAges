#pragma once

#include "ecs/CoreTypes.hpp"
#include <vector>
#include <string>
#include <functional>
#include <cstdint>

// [SECURITY_AGENT] Shared types for the anti-cheat system
// Extracted to break circular dependency between AntiCheat.hpp and subsystem headers

namespace DarkAges {
namespace Security {

// [SECURITY_AGENT] Cheat detection types
enum class CheatType : uint8_t {
    NONE = 0,
    SPEED_HACK = 1,           // Moving faster than allowed
    TELEPORT = 2,             // Instant large position change
    FLY_HACK = 3,             // Invalid Y position/velocity
    NO_CLIP = 4,              // Walking through walls
    INPUT_MANIPULATION = 5,   // Invalid input sequences
    PACKET_FLOODING = 6,      // Too many packets
    POSITION_SPOOFING = 7,    // Impossible position values
    STAT_MANIPULATION = 8,    // Health/damage hacking
    SUSPICIOUS_AIM = 9,       // Inhuman aiming patterns
    DAMAGE_HACK = 10,         // Dealing impossible damage
    COOLDOWN_VIOLATION = 11,  // Actions faster than allowed
    HITBOX_EXTENSION = 12     // Extended reach attacks
};

// [SECURITY_AGENT] Convert CheatType to string
inline const char* cheatTypeToString(CheatType type) {
    switch (type) {
        case CheatType::NONE: return "NONE";
        case CheatType::SPEED_HACK: return "SPEED_HACK";
        case CheatType::TELEPORT: return "TELEPORT";
        case CheatType::FLY_HACK: return "FLY_HACK";
        case CheatType::NO_CLIP: return "NO_CLIP";
        case CheatType::INPUT_MANIPULATION: return "INPUT_MANIPULATION";
        case CheatType::PACKET_FLOODING: return "PACKET_FLOODING";
        case CheatType::POSITION_SPOOFING: return "POSITION_SPOOFING";
        case CheatType::STAT_MANIPULATION: return "STAT_MANIPULATION";
        case CheatType::SUSPICIOUS_AIM: return "SUSPICIOUS_AIM";
        case CheatType::DAMAGE_HACK: return "DAMAGE_HACK";
        case CheatType::COOLDOWN_VIOLATION: return "COOLDOWN_VIOLATION";
        case CheatType::HITBOX_EXTENSION: return "HITBOX_EXTENSION";
        default: return "UNKNOWN";
    }
}

// [SECURITY_AGENT] Violation severity levels
enum class ViolationSeverity : uint8_t {
    INFO = 0,       // Log only
    WARNING = 1,    // Log + notify admins
    SUSPICIOUS = 2, // Log + flag for review
    CRITICAL = 3,   // Log + kick player
    BAN = 4         // Log + ban player
};

// [SECURITY_AGENT] Convert ViolationSeverity to string
inline const char* severityToString(ViolationSeverity severity) {
    switch (severity) {
        case ViolationSeverity::INFO: return "INFO";
        case ViolationSeverity::WARNING: return "WARNING";
        case ViolationSeverity::SUSPICIOUS: return "SUSPICIOUS";
        case ViolationSeverity::CRITICAL: return "CRITICAL";
        case ViolationSeverity::BAN: return "BAN";
        default: return "UNKNOWN";
    }
}

// [SECURITY_AGENT] Cheat detection result
struct CheatDetectionResult {
    bool detected{false};
    CheatType type{CheatType::NONE};
    ViolationSeverity severity{ViolationSeverity::INFO};
    const char* description{nullptr};
    Position correctedPosition{0, 0, 0, 0};
    float confidence{0.0f};  // 0.0 - 1.0

    // Additional context
    float actualValue{0.0f};      // The detected value
    float expectedValue{0.0f};    // The allowed value
    uint32_t timestamp{0};        // Detection timestamp
};

// [SECURITY_AGENT] Individual violation record
struct ViolationRecord {
    CheatType type{CheatType::NONE};
    uint32_t timestamp{0};
    float confidence{0.0f};
    std::string details;
};

// [SECURITY_AGENT] Player behavior profile
// Tracks statistical patterns for anomaly detection
struct BehaviorProfile {
    uint64_t playerId{0};
    uint32_t entityId{0};  // entt::entity as uint32

    // Movement statistics
    float averageSpeed{0.0f};
    float maxRecordedSpeed{0.0f};
    uint32_t totalMovements{0};
    uint32_t teleportDetections{0};
    uint32_t speedViolations{0};
    uint32_t flyViolations{0};

    // Combat statistics
    uint32_t hitsLanded{0};
    uint32_t hitsMissed{0};
    float averageAccuracy{0.0f};
    uint32_t suspiciousAimEvents{0};
    int32_t totalDamageDealt{0};

    // Network statistics
    float averagePacketInterval{16.0f};  // ms
    uint32_t packetFloods{0};
    uint32_t totalPackets{0};
    uint32_t lastPacketTime{0};

    // Trust system
    uint8_t trustScore{50};
    uint32_t consecutiveCleanTicks{0};
    uint32_t accountCreationTime{0};  // For new player leniency

    // Violation history
    static constexpr size_t MAX_VIOLATIONS = 20;
    std::vector<ViolationRecord> violationHistory;

    // Methods
    void recordViolation(const CheatDetectionResult& detection);
    void recordCleanMovement();
    void updateTrustScore(int8_t delta);

    [[nodiscard]] bool isTrusted() const { return trustScore >= 70; }
    [[nodiscard]] bool isSuspicious() const { return trustScore < 30; }
    [[nodiscard]] bool isNewPlayer(uint32_t currentTimeMs) const;
    [[nodiscard]] uint32_t getRecentViolationCount(uint32_t windowMs, uint32_t currentTimeMs) const;
};

// [SECURITY_AGENT] Callback types for cheat handling
using CheatCallback = std::function<void(uint64_t playerId,
    const CheatDetectionResult& result)>;
using BanCallback = std::function<void(uint64_t playerId,
    const char* reason, uint32_t durationMinutes)>;
using KickCallback = std::function<void(uint64_t playerId,
    const char* reason)>;

} // namespace Security
} // namespace DarkAges
