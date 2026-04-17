#pragma once

#include "ecs/CoreTypes.hpp"
#include <cstdint>
#include <array>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <atomic>

// [SECURITY_AGENT] Statistical anomaly detection system
// Implements Z-score analysis, exponential moving averages, and composite anomaly scoring

namespace DarkAges {
namespace Security {

// ============================================================================
// CONFIGURATION
// ============================================================================

// [SECURITY_AGENT] Configuration for statistical detector thresholds
struct StatisticalDetectorConfig {
    // EMA smoothing factor (0.0 - 1.0, lower = more smoothing)
    float emaAlpha = 0.1f;
    
    // Z-score threshold for anomaly detection (standard deviations)
    float zScoreThreshold = 3.0f;
    
    // Maximum velocity in units per second (server authority limit)
    float maxVelocity = 15.0f;
    
    // Maximum teleport distance per tick in units
    float maxTeleportDistance = 10.0f;
    
    // Server tick rate (Hz)
    float tickRate = 60.0f;
    
    // Maximum actions per second (60Hz limit with small burst allowance)
    float maxActionsPerSecond = 70.0f;
    
    // Inhuman accuracy threshold (percentage)
    float suspiciousAccuracyThreshold = 95.0f;
    
    // Minimum samples required before scoring is valid
    uint32_t minSamplesForConfidence = 30;
    
    // Samples needed for full confidence (1.0)
    uint32_t samplesForFullConfidence = 120;
    
    // History buffer size per player (default: 120 = 2s at 60Hz)
    uint32_t historySize = 120;
    
    // Anomaly score weight for speed violations
    float speedWeight = 25.0f;
    
    // Anomaly score weight for position violations
    float positionWeight = 35.0f;
    
    // Anomaly score weight for action rate violations
    float actionRateWeight = 20.0f;
    
    // Anomaly score weight for accuracy violations
    float accuracyWeight = 20.0f;
    
    StatisticalDetectorConfig() = default;
};

// ============================================================================
// ROLLING STATISTICS
// ============================================================================

// [SECURITY_AGENT] Per-player rolling statistics
struct PlayerStatistics {
    // Exponential moving averages
    float emaSpeed = 0.0f;
    float emaActionRate = 0.0f;
    float emaAccuracy = 0.0f;
    
    // Exponential moving variance (for Z-score calculation)
    float emaVarianceSpeed = 0.0f;
    float emaVarianceActionRate = 0.0f;
    
    // Sample count for confidence calculation
    uint32_t sampleCount = 0;
    
    // Last update timestamp
    uint32_t lastUpdateMs = 0;
    
    // Position tracking
    Position lastPosition{0, 0, 0, 0};
    
    // Action tracking
    uint32_t actionCount = 0;
    uint32_t actionWindowStartMs = 0;
    
    // Combat accuracy tracking
    uint32_t hitsLanded = 0;
    uint32_t hitsAttempted = 0;
    
    // Recent history for variance calculation
    // Ring buffer of recent speed values
    static constexpr size_t HISTORY_CAPACITY = 128;
    std::array<float, HISTORY_CAPACITY> speedHistory{};
    size_t historyIndex = 0;
    size_t historyCount = 0;
    
    // Anomaly detection state
    uint32_t speedAnomalyCount = 0;
    uint32_t positionAnomalyCount = 0;
    uint32_t actionRateAnomalyCount = 0;
    uint32_t accuracyAnomalyCount = 0;
    
    // Timestamps for anomaly decay
    uint32_t lastSpeedAnomalyMs = 0;
    uint32_t lastPositionAnomalyMs = 0;
    uint32_t lastActionRateAnomalyMs = 0;
    uint32_t lastAccuracyAnomalyMs = 0;
};

// ============================================================================
// ANOMALY SCORE
// ============================================================================

// [SECURITY_AGENT] Composite anomaly score with category breakdown
struct AnomalyScore {
    // Composite score 0-100 (0 = clean, 100 = definite cheat)
    float composite = 0.0f;
    
    // Per-category scores (0-100)
    float speedScore = 0.0f;
    float positionScore = 0.0f;
    float actionRateScore = 0.0f;
    float accuracyScore = 0.0f;
    
    // Z-scores for each category
    float speedZScore = 0.0f;
    float positionZScore = 0.0f;
    float actionRateZScore = 0.0f;
    
    // Confidence in the score (0-1, based on sample count)
    float confidence = 0.0f;
    
    // Whether any individual threshold was exceeded
    bool speedAnomaly = false;
    bool positionAnomaly = false;
    bool actionRateAnomaly = false;
    bool accuracyAnomaly = false;
    
    // Total anomaly count (weighted sum of recent anomalies)
    uint32_t totalAnomalyCount = 0;
    
    [[nodiscard]] bool hasAnomaly() const {
        return speedAnomaly || positionAnomaly || actionRateAnomaly || accuracyAnomaly;
    }
    
    [[nodiscard]] bool isReliable() const {
        return confidence >= 0.25f; // At least 30 samples
    }
    
    [[nodiscard]] bool isHighConfidence() const {
        return confidence >= 0.75f;
    }
};

// ============================================================================
// STATISTICAL DETECTOR
// ============================================================================

// [SECURITY_AGENT] Statistical anomaly detection system
// Thread-safe public API for integration with AntiCheatSystem
class StatisticalDetector {
public:
    explicit StatisticalDetector(const StatisticalDetectorConfig& config = StatisticalDetectorConfig{});
    ~StatisticalDetector() = default;
    
    // Non-copyable
    StatisticalDetector(const StatisticalDetector&) = delete;
    StatisticalDetector& operator=(const StatisticalDetector&) = delete;
    
    // ========================================================================
    // CORE API
    // ========================================================================
    
    // [SECURITY_AGENT] Feed player movement/action data
    // Call this every server tick with player state
    void updateStats(uint64_t playerId,
                     const Position& position,
                     const Velocity& velocity,
                     bool performedAction,
                     uint32_t timestampMs);
    
    // [SECURITY_AGENT] Record a combat hit attempt
    // hitLanded: true if the attack connected
    void recordHitAttempt(uint64_t playerId, bool hitLanded, uint32_t timestampMs);
    
    // [SECURITY_AGENT] Compute composite anomaly score for player
    [[nodiscard]] AnomalyScore getAnomalyScore(uint64_t playerId) const;
    
    // [SECURITY_AGENT] Get confidence level (0-1)
    // Returns how much data we have for reliable scoring
    [[nodiscard]] float getConfidence(uint64_t playerId) const;
    
    // [SECURITY_AGENT] Check if player data is being tracked
    [[nodiscard]] bool isTracking(uint64_t playerId) const;
    
    // ========================================================================
    // INDIVIDUAL DETECTORS
    // ========================================================================
    
    // [SECURITY_AGENT] Z-score based speed anomaly detection
    // Returns Z-score for current velocity (positive = above normal)
    [[nodiscard]] float calculateSpeedZScore(uint64_t playerId, float currentSpeed) const;
    
    // [SECURITY_AGENT] Position anomaly detection (impossible teleport)
    // Returns distance moved since last update
    [[nodiscard]] float calculatePositionDelta(uint64_t playerId, const Position& newPos) const;
    
    // [SECURITY_AGENT] Action rate anomaly detection
    // Returns current actions/second
    [[nodiscard]] float getCurrentActionRate(uint64_t playerId) const;
    
    // [SECURITY_AGENT] Accuracy anomaly detection
    // Returns current accuracy percentage (0-100)
    [[nodiscard]] float getCurrentAccuracy(uint64_t playerId) const;
    
    // ========================================================================
    // MANAGEMENT
    // ========================================================================
    
    // [SECURITY_AGENT] Remove player tracking (on disconnect)
    void removePlayer(uint64_t playerId);
    
    // [SECURITY_AGENT] Clear all player data
    void clear();
    
    // [SECURITY_AGENT] Get number of tracked players
    [[nodiscard]] size_t getTrackedPlayerCount() const;
    
    // ========================================================================
    // CONFIGURATION
    // ========================================================================
    
    void setConfig(const StatisticalDetectorConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }
    
    [[nodiscard]] StatisticalDetectorConfig getConfig() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

private:
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
    // Get or create player statistics
    [[nodiscard]] PlayerStatistics* getStats(uint64_t playerId);
    [[nodiscard]] const PlayerStatistics* getStats(uint64_t playerId) const;
    
    // Update EMA value
    static float updateEMA(float current, float newValue, float alpha);
    
    // Update EMA variance
    static float updateEMAVariance(float currentVariance, float currentEMA, 
                                   float newValue, float alpha);
    
    // Calculate Z-score from EMA statistics
    static float calculateZScore(float value, float ema, float emaVariance);
    
    // Calculate composite anomaly score from individual scores
    static float calculateCompositeScore(float speedScore, float positionScore,
                                         float actionRateScore, float accuracyScore,
                                         float speedWeight, float positionWeight,
                                         float actionRateWeight, float accuracyWeight);
    
    // Convert Z-score to anomaly score (0-100)
    static float zScoreToAnomalyScore(float zScore, float threshold);
    
    // Calculate confidence from sample count
    [[nodiscard]] float calculateConfidence(uint32_t sampleCount) const;
    
    // Update anomaly counters with decay
    void updateAnomalyCounters(PlayerStatistics& stats, uint32_t timestampMs,
                               bool speedAnomaly, bool positionAnomaly,
                               bool actionRateAnomaly, bool accuracyAnomaly);
    
    // Calculate weighted anomaly count
    [[nodiscard]] uint32_t calculateTotalAnomalies(const PlayerStatistics& stats) const;

private:
    mutable std::mutex mutex_;
    StatisticalDetectorConfig config_;
    std::unordered_map<uint64_t, PlayerStatistics> playerStats_;
};

} // namespace Security
} // namespace DarkAges
