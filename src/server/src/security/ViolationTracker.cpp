#include "security/ViolationTracker.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace DarkAges {
namespace Security {

// ========================================================================
// ViolationTracker Implementation
// ========================================================================

ViolationTracker::ViolationTracker() = default;

void ViolationTracker::updateBehaviorProfile(uint64_t playerId,
    const CheatDetectionResult& detection) {

    std::lock_guard<std::mutex> lock(profileMutex_);

    auto& profile = profiles_[playerId];
    if (profile.playerId == 0) {
        profile.playerId = playerId;
        profile.accountCreationTime = detection.timestamp;
    }

    profile.recordViolation(detection);
}

void ViolationTracker::recordCleanMovement(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(profileMutex_);

    auto it = profiles_.find(playerId);
    if (it != profiles_.end()) {
        it->second.recordCleanMovement();
    }
}

BehaviorProfile* ViolationTracker::getProfile(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(profileMutex_);

    auto it = profiles_.find(playerId);
    if (it != profiles_.end()) {
        return &it->second;
    }

    // Create new profile
    auto& profile = profiles_[playerId];
    profile.playerId = playerId;
    profile.trustScore = config_.initialTrustScore;
    profile.accountCreationTime = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() / 1000000);
    return &profile;
}

const BehaviorProfile* ViolationTracker::getProfile(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(profileMutex_);

    auto it = profiles_.find(playerId);
    if (it != profiles_.end()) {
        return &it->second;
    }
    return nullptr;
}

void ViolationTracker::removeProfile(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(profileMutex_);
    profiles_.erase(playerId);
}

void ViolationTracker::reportViolation(uint64_t playerId,
    const CheatDetectionResult& result) {

    std::cerr << "[ANTICHEAT] Player " << playerId
              << " - " << result.description
              << " [" << cheatTypeToString(result.type) << "]"
              << " (confidence: " << static_cast<int>(result.confidence * 100) << "%)"
              << " [" << severityToString(result.severity) << "]\n";

    if (onCheatDetected_) {
        onCheatDetected_(playerId, result);
    }

    // Handle severity-based actions
    handleSeverityAction(playerId, result);
}

void ViolationTracker::handleSeverityAction(uint64_t playerId,
    const CheatDetectionResult& result) {

    switch (result.severity) {
        case ViolationSeverity::CRITICAL:
            kickPlayer(playerId, result.description);
            break;
        case ViolationSeverity::BAN:
            banPlayer(playerId, result.description, config_.defaultBanDurationMinutes);
            break;
        default:
            // INFO, WARNING, SUSPICIOUS - just log
            break;
    }
}

void ViolationTracker::banPlayer(uint64_t playerId, const char* reason,
    uint32_t durationMinutes) {

    std::cerr << "[ANTICHEAT] Banning player " << playerId
              << " for " << durationMinutes << " minutes: " << reason << "\n";

    ++playersBanned_;

    if (onPlayerBanned_) {
        onPlayerBanned_(playerId, reason, durationMinutes);
    }
}

void ViolationTracker::kickPlayer(uint64_t playerId, const char* reason) {
    std::cerr << "[ANTICHEAT] Kicking player " << playerId
              << ": " << reason << "\n";

    ++playersKicked_;

    if (onPlayerKicked_) {
        onPlayerKicked_(playerId, reason);
    }
}

void ViolationTracker::flagPlayerForReview(uint64_t playerId, const char* reason) {
    std::cerr << "[ANTICHEAT] Flagging player " << playerId
              << " for review: " << reason << "\n";

    // Would add to review queue for admin inspection
}

void ViolationTracker::incrementDetections(CheatType type) {
    ++totalDetections_;

    std::lock_guard<std::mutex> lock(statsMutex_);
    detectionCounts_[type]++;
}

uint32_t ViolationTracker::getActiveProfileCount() const {
    std::lock_guard<std::mutex> lock(profileMutex_);
    return static_cast<uint32_t>(profiles_.size());
}

uint32_t ViolationTracker::getDetectionCount(CheatType type) const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto it = detectionCounts_.find(type);
    if (it != detectionCounts_.end()) {
        return it->second;
    }
    return 0;
}

void ViolationTracker::resetStatistics() {
    totalDetections_ = 0;
    playersKicked_ = 0;
    playersBanned_ = 0;

    std::lock_guard<std::mutex> lock(statsMutex_);
    detectionCounts_.clear();
}

ViolationSeverity ViolationTracker::determineSeverity(CheatType type,
    float confidence, uint32_t recentViolationCount) const {
    (void)type;
    (void)confidence;
    (void)recentViolationCount;
    // Placeholder - severity is currently determined inline in detect methods
    return ViolationSeverity::INFO;
}

} // namespace Security
} // namespace DarkAges
