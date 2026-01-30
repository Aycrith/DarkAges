#pragma once

#include "ecs/CoreTypes.hpp"
#include "security/AntiCheatConfig.hpp"
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <string>
#include <atomic>
#include <cstddef>
#include <cstdint>

// [SECURITY_AGENT] Comprehensive anti-cheat system for DarkAges MMO
// Implements server authority, statistical analysis, and graduated response

namespace DarkAges {
namespace Security {

// Forward declarations
class AntiCheatSystem;

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

// [SECURITY_AGENT] Main anti-cheat system
class AntiCheatSystem {
public:
    AntiCheatSystem();
    ~AntiCheatSystem();
    
    // Non-copyable
    AntiCheatSystem(const AntiCheatSystem&) = delete;
    AntiCheatSystem& operator=(const AntiCheatSystem&) = delete;
    
    // Initialize the system
    [[nodiscard]] bool initialize();
    
    // Shutdown and cleanup
    void shutdown();
    
    // ========================================================================
    // VALIDATION METHODS
    // ========================================================================
    
    // [SECURITY_AGENT] Validate player movement
    // Returns detection result with corrected position if needed
    [[nodiscard]] CheatDetectionResult validateMovement(EntityID entity,
        const Position& oldPos, const Position& newPos,
        uint32_t dtMs, const InputState& input,
        Registry& registry);
    
    // [SECURITY_AGENT] Validate combat action
    [[nodiscard]] CheatDetectionResult validateCombat(EntityID attacker, 
        EntityID target, int16_t claimedDamage, 
        const Position& claimedHitPos, const Position& attackerPos,
        Registry& registry);
    
    // [SECURITY_AGENT] Validate input packet
    [[nodiscard]] CheatDetectionResult validateInput(EntityID entity,
        const InputState& input, uint32_t currentTimeMs,
        Registry& registry);
    
    // [SECURITY_AGENT] Check for packet flooding/rate limiting
    [[nodiscard]] CheatDetectionResult checkRateLimit(EntityID entity, 
        uint32_t currentTimeMs, Registry& registry);
    
    // [SECURITY_AGENT] Validate position is within world bounds
    [[nodiscard]] CheatDetectionResult validatePositionBounds(
        const Position& pos) const;
    
    // ========================================================================
    // BEHAVIOR TRACKING
    // ========================================================================
    
    // Update behavior profile with detection result
    void updateBehaviorProfile(uint64_t playerId, 
        const CheatDetectionResult& detection);
    
    // Update with clean movement (improves trust)
    void recordCleanMovement(uint64_t playerId);
    
    // Get behavior profile (creates if not exists)
    [[nodiscard]] BehaviorProfile* getProfile(uint64_t playerId);
    [[nodiscard]] const BehaviorProfile* getProfile(uint64_t playerId) const;
    
    // Remove profile (on disconnect)
    void removeProfile(uint64_t playerId);
    
    // ========================================================================
    // ACTIONS
    // ========================================================================
    
    // Set callback functions
    void setOnCheatDetected(CheatCallback cb) { onCheatDetected_ = std::move(cb); }
    void setOnPlayerBanned(BanCallback cb) { onPlayerBanned_ = std::move(cb); }
    void setOnPlayerKicked(KickCallback cb) { onPlayerKicked_ = std::move(cb); }
    
    // Manual enforcement
    void banPlayer(uint64_t playerId, const char* reason, 
        uint32_t durationMinutes);
    void kickPlayer(uint64_t playerId, const char* reason);
    void flagPlayerForReview(uint64_t playerId, const char* reason);
    
    // ========================================================================
    // STATISTICS
    // ========================================================================
    
    [[nodiscard]] uint32_t getTotalDetections() const { return totalDetections_; }
    [[nodiscard]] uint32_t getPlayersKicked() const { return playersKicked_; }
    [[nodiscard]] uint32_t getPlayersBanned() const { return playersBanned_; }
    [[nodiscard]] uint32_t getActiveProfileCount() const;
    
    // Get detection counts by type
    [[nodiscard]] uint32_t getDetectionCount(CheatType type) const;
    
    // Reset statistics
    void resetStatistics();
    
    // ========================================================================
    // CONFIGURATION
    // ========================================================================
    
    // Update configuration at runtime
    void setConfig(const AntiCheatConfig& config) { config_ = config; }
    [[nodiscard]] const AntiCheatConfig& getConfig() const { return config_; }

private:
    // ========================================================================
    // DETECTION METHODS
    // ========================================================================
    
    [[nodiscard]] CheatDetectionResult detectSpeedHack(EntityID entity,
        const Position& oldPos, const Position& newPos,
        uint32_t dtMs, const InputState& input, Registry& registry);
    
    [[nodiscard]] CheatDetectionResult detectTeleport(EntityID entity,
        const Position& oldPos, const Position& newPos,
        Registry& registry);
    
    [[nodiscard]] CheatDetectionResult detectFlyHack(EntityID entity,
        const Position& pos, const Velocity& vel,
        const InputState& input, Registry& registry);
    
    [[nodiscard]] CheatDetectionResult detectNoClip(EntityID entity,
        const Position& oldPos, const Position& newPos,
        Registry& registry);
    
    [[nodiscard]] CheatDetectionResult detectInputManipulation(EntityID entity,
        const InputState& input, Registry& registry);
    
    [[nodiscard]] CheatDetectionResult detectDamageHack(EntityID attacker,
        EntityID target, int16_t claimedDamage, Registry& registry);
    
    [[nodiscard]] CheatDetectionResult detectHitboxExtension(EntityID attacker,
        const Position& claimedHitPos, const Position& attackerPos,
        Registry& registry);
    
    // ========================================================================
    // HELPER METHODS
    // ========================================================================
    
    [[nodiscard]] float calculateMaxAllowedSpeed(const InputState& input) const;
    [[nodiscard]] float calculateDistance(const Position& a, const Position& b) const;
    [[nodiscard]] float calculateSpeed(const Position& from, const Position& to, 
        uint32_t dtMs) const;
    [[nodiscard]] bool isPositionValid(const Position& pos) const;
    [[nodiscard]] bool isWithinWorldBounds(const Position& pos) const;
    [[nodiscard]] bool wouldCollideWithWorld(const Position& pos) const;
    [[nodiscard]] bool isGrounded(EntityID entity, const Position& pos, 
        Registry& registry) const;
    
    void applyPositionCorrection(EntityID entity, const Position& correctedPos,
        Registry& registry);
    
    void reportViolation(uint64_t playerId, const CheatDetectionResult& result);
    void handleSeverityAction(uint64_t playerId, const CheatDetectionResult& result);
    
    [[nodiscard]] ViolationSeverity determineSeverity(CheatType type, 
        float confidence, uint32_t recentViolationCount) const;

private:
    // Configuration
    AntiCheatConfig config_;
    
    // Behavior profiles
    mutable std::mutex profileMutex_;
    std::unordered_map<uint64_t, BehaviorProfile> profiles_;
    
    // Callbacks
    CheatCallback onCheatDetected_;
    BanCallback onPlayerBanned_;
    KickCallback onPlayerKicked_;
    
    // Statistics
    std::atomic<uint32_t> totalDetections_{0};
    std::atomic<uint32_t> playersKicked_{0};
    std::atomic<uint32_t> playersBanned_{0};
    std::unordered_map<CheatType, uint32_t> detectionCounts_;
    mutable std::mutex statsMutex_;
    
    // Initialization state
    bool initialized_{false};
};

// [SECURITY_AGENT] Server authority enforcer
// Ensures client state matches server state, applies corrections
class ServerAuthority {
public:
    // [SECURITY_AGENT] Force client position correction
    // Sent when server detects client is out of sync
    static void sendPositionCorrection(ConnectionID conn,
        const Position& serverPos, const Velocity& serverVel,
        uint32_t lastProcessedInput);
    
    // [SECURITY_AGENT] Force full client state sync
    // Used for major desyncs or after teleport
    static void sendStateSync(ConnectionID conn, EntityID entity,
        Registry& registry);
    
    // [SECURITY_AGENT] Send server authority correction packet
    static void sendAuthorityCorrection(ConnectionID conn,
        const Position& serverPos, const Position& clientClaimedPos,
        const char* reason);
    
    // [SECURITY_AGENT] Validate client claim against server truth
    // Returns true if within tolerance, false if correction needed
    [[nodiscard]] static bool validateClientClaim(const Position& serverPos,
        const Position& claimedPos, const Velocity& serverVel,
        const Velocity& claimedVel, float tolerance);
    
    // [SECURITY_AGENT] Calculate required correction magnitude
    [[nodiscard]] static float calculateCorrectionMagnitude(
        const Position& serverPos, const Position& clientPos);
    
    // [SECURITY_AGENT] Check if position needs correction
    [[nodiscard]] static bool needsCorrection(const Position& serverPos,
        const Position& clientPos, float tolerance);
};

// [SECURITY_AGENT] Statistical anomaly detector
// Uses statistical analysis to detect subtle cheats
class StatisticalAnalyzer {
public:
    // [SECURITY_AGENT] Analyze aim pattern for aimbot detection
    [[nodiscard]] static bool analyzeAimPattern(EntityID entity,
        const std::vector<Rotation>& aimHistory);
    
    // [SECURITY_AGENT] Calculate movement consistency score
    // Low scores indicate unnatural movement patterns
    [[nodiscard]] static float calculateMovementConsistency(
        const std::vector<Position>& positionHistory);
    
    // [SECURITY_AGENT] Detect impossible reaction times
    [[nodiscard]] static bool detectImpossibleReactions(
        const std::vector<uint32_t>& reactionTimeSamples);
};

} // namespace Security
} // namespace DarkAges
