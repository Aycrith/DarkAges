#pragma once

#include "security/AntiCheatTypes.hpp"
#include "security/AntiCheatConfig.hpp"
#include "physics/SpatialHash.hpp"

// Include subsystem headers after AntiCheatTypes
#include "security/MovementValidator.hpp"
#include "security/ViolationTracker.hpp"

// [SECURITY_AGENT] Comprehensive anti-cheat system for DarkAges MMO
// Implements server authority, statistical analysis, and graduated response

namespace DarkAges {
namespace Security {

// Forward declarations
class AntiCheatSystem;

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
    void setOnCheatDetected(CheatCallback cb) { violationTracker_.setOnCheatDetected(std::move(cb)); }
    void setOnPlayerBanned(BanCallback cb) { violationTracker_.setOnPlayerBanned(std::move(cb)); }
    void setOnPlayerKicked(KickCallback cb) { violationTracker_.setOnPlayerKicked(std::move(cb)); }
    
    // Set spatial hash for collision detection
    void setSpatialHash(const SpatialHash* hash) { movementValidator_.setSpatialHash(hash); }
    
    // Manual enforcement
    void banPlayer(uint64_t playerId, const char* reason, 
        uint32_t durationMinutes);
    void kickPlayer(uint64_t playerId, const char* reason);
    void flagPlayerForReview(uint64_t playerId, const char* reason);
    
    // ========================================================================
    // STATISTICS
    // ========================================================================
    
    [[nodiscard]] uint32_t getTotalDetections() const { return violationTracker_.getTotalDetections(); }
    [[nodiscard]] uint32_t getPlayersKicked() const { return violationTracker_.getPlayersKicked(); }
    [[nodiscard]] uint32_t getPlayersBanned() const { return violationTracker_.getPlayersBanned(); }
    [[nodiscard]] uint32_t getActiveProfileCount() const { return violationTracker_.getActiveProfileCount(); }
    
    // Get detection counts by type
    [[nodiscard]] uint32_t getDetectionCount(CheatType type) const { return violationTracker_.getDetectionCount(type); }
    
    // Reset statistics
    void resetStatistics() { violationTracker_.resetStatistics(); }
    
    // ========================================================================
    // CONFIGURATION
    // ========================================================================
    
    // Update configuration at runtime
    void setConfig(const AntiCheatConfig& config) {
        config_ = config;
        movementValidator_.setConfig(config);
        violationTracker_.setConfig(config);
    }
    [[nodiscard]] const AntiCheatConfig& getConfig() const { return config_; }

private:
    // Configuration
    AntiCheatConfig config_;

    // Extracted subsystems
    MovementValidator movementValidator_;
    ViolationTracker violationTracker_;

    // Initialization state
    bool initialized_{false};
};

} // namespace Security
} // namespace DarkAges
