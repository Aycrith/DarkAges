#include <catch2/catch_test_macros.hpp>
#include "security/ViolationTracker.hpp"
#include "security/AntiCheatTypes.hpp"
#include "security/AntiCheatConfig.hpp"

using namespace DarkAges;
using namespace DarkAges::Security;

namespace {
CheatDetectionResult makeDetection(CheatType type, ViolationSeverity severity,
                                   float confidence = 0.9f, uint32_t ts = 1000) {
    CheatDetectionResult result;
    result.detected = true;
    result.type = type;
    result.severity = severity;
    result.confidence = confidence;
    result.timestamp = ts;
    result.description = "Test detection";
    return result;
}
}  // namespace

// ============================================================================
// Initialization
// ============================================================================

TEST_CASE("[violation-tracker] New tracker has zero statistics", "[security]") {
    ViolationTracker tracker;
    REQUIRE(tracker.getTotalDetections() == 0);
    REQUIRE(tracker.getPlayersKicked() == 0);
    REQUIRE(tracker.getPlayersBanned() == 0);
    REQUIRE(tracker.getActiveProfileCount() == 0);
}

// ============================================================================
// Profile management
// ============================================================================

TEST_CASE("[violation-tracker] getProfile creates new profile", "[security]") {
    ViolationTracker tracker;
    auto* profile = tracker.getProfile(12345);
    REQUIRE(profile != nullptr);
    REQUIRE(profile->playerId == 12345);
    REQUIRE(tracker.getActiveProfileCount() == 1);
}

TEST_CASE("[violation-tracker] getProfile returns existing profile", "[security]") {
    ViolationTracker tracker;
    auto* p1 = tracker.getProfile(12345);
    p1->trustScore = 80;
    auto* p2 = tracker.getProfile(12345);
    REQUIRE(p1 == p2);
    REQUIRE(p2->trustScore == 80);
}

TEST_CASE("[violation-tracker] const getProfile returns nullptr for unknown", "[security]") {
    const ViolationTracker tracker;
    REQUIRE(tracker.getProfile(99999) == nullptr);
}

TEST_CASE("[violation-tracker] removeProfile deletes profile", "[security]") {
    ViolationTracker tracker;
    (void)tracker.getProfile(12345); // Creates profile via side effect
    REQUIRE(tracker.getActiveProfileCount() == 1);
    tracker.removeProfile(12345);
    REQUIRE(tracker.getActiveProfileCount() == 0);
}

TEST_CASE("[violation-tracker] removeProfile of nonexistent is no-op", "[security]") {
    ViolationTracker tracker;
    tracker.removeProfile(99999);  // Should not crash
    REQUIRE(tracker.getActiveProfileCount() == 0);
}

// ============================================================================
// Behavior tracking
// ============================================================================

TEST_CASE("[violation-tracker] updateBehaviorProfile records violations", "[security]") {
    ViolationTracker tracker;
    auto detection = makeDetection(CheatType::SPEED_HACK, ViolationSeverity::WARNING);
    tracker.updateBehaviorProfile(12345, detection);

    auto* profile = tracker.getProfile(12345);
    REQUIRE(profile != nullptr);
    REQUIRE(profile->playerId == 12345);
}

TEST_CASE("[violation-tracker] recordCleanMovement improves trust", "[security]") {
    ViolationTracker tracker;
    auto* profile = tracker.getProfile(12345);
    uint8_t initialTrust = profile->trustScore;
    tracker.recordCleanMovement(12345);
    // Clean movement should improve trust
    REQUIRE(profile->trustScore >= initialTrust);
}

TEST_CASE("[violation-tracker] recordCleanMovement for nonexistent is no-op", "[security]") {
    ViolationTracker tracker;
    tracker.recordCleanMovement(99999);  // Should not crash
    REQUIRE(tracker.getActiveProfileCount() == 0);
}

// ============================================================================
// reportViolation
// ============================================================================

TEST_CASE("[violation-tracker] reportViolation triggers cheat callback", "[security]") {
    ViolationTracker tracker;
    bool callbackCalled = false;
    uint64_t callbackPlayerId = 0;

    tracker.setOnCheatDetected([&](uint64_t playerId, const CheatDetectionResult&) {
        callbackCalled = true;
        callbackPlayerId = playerId;
    });

    auto detection = makeDetection(CheatType::SPEED_HACK, ViolationSeverity::INFO);
    tracker.reportViolation(12345, detection);

    REQUIRE(callbackCalled);
    REQUIRE(callbackPlayerId == 12345);
}

TEST_CASE("[violation-tracker] reportViolation without callback still works", "[security]") {
    ViolationTracker tracker;
    auto detection = makeDetection(CheatType::TELEPORT, ViolationSeverity::WARNING);
    tracker.reportViolation(12345, detection);  // Should not crash
}

// ============================================================================
// Severity actions
// ============================================================================

TEST_CASE("[violation-tracker] CRITICAL severity kicks player", "[security]") {
    ViolationTracker tracker;
    bool kicked = false;
    uint64_t kickedPlayer = 0;

    tracker.setOnPlayerKicked([&](uint64_t playerId, const char*) {
        kicked = true;
        kickedPlayer = playerId;
    });

    auto detection = makeDetection(CheatType::FLY_HACK, ViolationSeverity::CRITICAL);
    tracker.reportViolation(12345, detection);

    REQUIRE(kicked);
    REQUIRE(kickedPlayer == 12345);
    REQUIRE(tracker.getPlayersKicked() == 1);
}

TEST_CASE("[violation-tracker] BAN severity bans player", "[security]") {
    ViolationTracker tracker;
    bool banned = false;
    uint64_t bannedPlayer = 0;
    uint32_t banDuration = 0;

    tracker.setOnPlayerBanned([&](uint64_t playerId, const char*, uint32_t duration) {
        banned = true;
        bannedPlayer = playerId;
        banDuration = duration;
    });

    auto detection = makeDetection(CheatType::TELEPORT, ViolationSeverity::BAN);
    tracker.reportViolation(12345, detection);

    REQUIRE(banned);
    REQUIRE(bannedPlayer == 12345);
    REQUIRE(banDuration == 60);  // Default ban duration
    REQUIRE(tracker.getPlayersBanned() == 1);
}

TEST_CASE("[violation-tracker] INFO severity does not kick or ban", "[security]") {
    ViolationTracker tracker;
    bool anyAction = false;

    tracker.setOnPlayerKicked([&](uint64_t, const char*) { anyAction = true; });
    tracker.setOnPlayerBanned([&](uint64_t, const char*, uint32_t) { anyAction = true; });

    auto detection = makeDetection(CheatType::SPEED_HACK, ViolationSeverity::INFO);
    tracker.reportViolation(12345, detection);

    REQUIRE_FALSE(anyAction);
    REQUIRE(tracker.getPlayersKicked() == 0);
    REQUIRE(tracker.getPlayersBanned() == 0);
}

// ============================================================================
// Manual enforcement
// ============================================================================

TEST_CASE("[violation-tracker] banPlayer triggers callback and increments counter", "[security]") {
    ViolationTracker tracker;
    bool banned = false;

    tracker.setOnPlayerBanned([&](uint64_t playerId, const char* reason, uint32_t duration) {
        banned = true;
        REQUIRE(playerId == 12345);
        REQUIRE(std::string(reason) == "Test ban");
        REQUIRE(duration == 120);
    });

    tracker.banPlayer(12345, "Test ban", 120);
    REQUIRE(banned);
    REQUIRE(tracker.getPlayersBanned() == 1);
}

TEST_CASE("[violation-tracker] kickPlayer triggers callback and increments counter", "[security]") {
    ViolationTracker tracker;
    bool kicked = false;

    tracker.setOnPlayerKicked([&](uint64_t playerId, const char* reason) {
        kicked = true;
        REQUIRE(playerId == 12345);
        REQUIRE(std::string(reason) == "Test kick");
    });

    tracker.kickPlayer(12345, "Test kick");
    REQUIRE(kicked);
    REQUIRE(tracker.getPlayersKicked() == 1);
}

TEST_CASE("[violation-tracker] flagPlayerForReview does not crash", "[security]") {
    ViolationTracker tracker;
    tracker.flagPlayerForReview(12345, "Suspicious behavior");
    // Just verifying no crash
}

// ============================================================================
// Statistics
// ============================================================================

TEST_CASE("[violation-tracker] incrementDetections tracks by type", "[security]") {
    ViolationTracker tracker;
    tracker.incrementDetections(CheatType::SPEED_HACK);
    tracker.incrementDetections(CheatType::SPEED_HACK);
    tracker.incrementDetections(CheatType::TELEPORT);

    REQUIRE(tracker.getTotalDetections() == 3);
    REQUIRE(tracker.getDetectionCount(CheatType::SPEED_HACK) == 2);
    REQUIRE(tracker.getDetectionCount(CheatType::TELEPORT) == 1);
    REQUIRE(tracker.getDetectionCount(CheatType::FLY_HACK) == 0);
}

TEST_CASE("[violation-tracker] resetStatistics clears all counters", "[security]") {
    ViolationTracker tracker;
    tracker.incrementDetections(CheatType::SPEED_HACK);
    tracker.incrementDetections(CheatType::TELEPORT);
    tracker.banPlayer(1, "test", 60);
    tracker.kickPlayer(2, "test");

    tracker.resetStatistics();

    REQUIRE(tracker.getTotalDetections() == 0);
    REQUIRE(tracker.getPlayersKicked() == 0);
    REQUIRE(tracker.getPlayersBanned() == 0);
    REQUIRE(tracker.getDetectionCount(CheatType::SPEED_HACK) == 0);
}

// ============================================================================
// Config
// ============================================================================

TEST_CASE("[violation-tracker] setConfig applies custom config", "[security]") {
    ViolationTracker tracker;
    AntiCheatConfig config;
    config.initialTrustScore = 75;
    config.defaultBanDurationMinutes = 1440;  // 24 hours
    tracker.setConfig(config);

    auto* profile = tracker.getProfile(12345);
    REQUIRE(profile->trustScore == 75);

    // Verify ban duration uses custom config
    bool banned = false;
    uint32_t banDuration = 0;
    tracker.setOnPlayerBanned([&](uint64_t, const char*, uint32_t duration) {
        banned = true;
        banDuration = duration;
    });

    auto detection = makeDetection(CheatType::TELEPORT, ViolationSeverity::BAN);
    tracker.reportViolation(12345, detection);
    REQUIRE(banDuration == 1440);
}

// ============================================================================
// Callbacks without set
// ============================================================================

TEST_CASE("[violation-tracker] Actions work without callbacks set", "[security]") {
    ViolationTracker tracker;
    // No callbacks set - should not crash
    tracker.banPlayer(1, "reason", 60);
    tracker.kickPlayer(2, "reason");
    REQUIRE(tracker.getPlayersBanned() == 1);
    REQUIRE(tracker.getPlayersKicked() == 1);
}
