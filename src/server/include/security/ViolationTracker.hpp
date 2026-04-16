#pragma once

#include "security/AntiCheatTypes.hpp"
#include "security/AntiCheatConfig.hpp"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>

// [SECURITY_AGENT] Violation tracking and enforcement subsystem
// Extracted from AntiCheatSystem for cohesion and testability

namespace DarkAges {
namespace Security {

// [SECURITY_AGENT] Tracks player violations, manages trust scores, and handles enforcement
// Manages behavior profiles, reporting, ban/kick actions, and detection statistics
class ViolationTracker {
public:
    ViolationTracker();

    // Set configuration
    void setConfig(const AntiCheatConfig& config) { config_ = config; }

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

    // Report a violation (logs, triggers callback, handles severity actions)
    void reportViolation(uint64_t playerId, const CheatDetectionResult& result);

    // Manual enforcement
    void banPlayer(uint64_t playerId, const char* reason,
        uint32_t durationMinutes);
    void kickPlayer(uint64_t playerId, const char* reason);
    void flagPlayerForReview(uint64_t playerId, const char* reason);

    // ========================================================================
    // STATISTICS
    // ========================================================================

    void incrementDetections(CheatType type);

    [[nodiscard]] uint32_t getTotalDetections() const { return totalDetections_; }
    [[nodiscard]] uint32_t getPlayersKicked() const { return playersKicked_; }
    [[nodiscard]] uint32_t getPlayersBanned() const { return playersBanned_; }
    [[nodiscard]] uint32_t getActiveProfileCount() const;

    // Get detection counts by type
    [[nodiscard]] uint32_t getDetectionCount(CheatType type) const;

    // Reset statistics
    void resetStatistics();

private:
    void handleSeverityAction(uint64_t playerId, const CheatDetectionResult& result);

    [[nodiscard]] ViolationSeverity determineSeverity(CheatType type,
        float confidence, uint32_t recentViolationCount) const;

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
};

} // namespace Security
} // namespace DarkAges
