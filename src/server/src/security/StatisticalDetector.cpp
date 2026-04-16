// [SECURITY_AGENT] Statistical anomaly detection implementation
// Z-score analysis, exponential moving averages, composite anomaly scoring

#include "security/StatisticalDetector.hpp"
#include <algorithm>
#include <numeric>

namespace DarkAges {
namespace Security {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

StatisticalDetector::StatisticalDetector(const StatisticalDetectorConfig& config)
    : config_(config) {}

// ============================================================================
// CORE API
// ============================================================================

void StatisticalDetector::updateStats(uint64_t playerId,
                                       const Position& position,
                                       const Velocity& velocity,
                                       bool performedAction,
                                       uint32_t timestampMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& stats = playerStats_[playerId];
    const float alpha = config_.emaAlpha;
    
    // Calculate current speed
    float currentSpeed = velocity.speed();
    
    // Calculate position delta from last position
    float positionDelta = 0.0f;
    if (stats.sampleCount > 0) {
        float dx = (position.x - stats.lastPosition.x) * Constants::FIXED_TO_FLOAT;
        float dy = (position.y - stats.lastPosition.y) * Constants::FIXED_TO_FLOAT;
        float dz = (position.z - stats.lastPosition.z) * Constants::FIXED_TO_FLOAT;
        positionDelta = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    
    // Calculate time delta for action rate
    uint32_t dtMs = 0;
    if (stats.lastUpdateMs > 0 && timestampMs > stats.lastUpdateMs) {
        dtMs = timestampMs - stats.lastUpdateMs;
    }
    
    // Update action tracking
    if (performedAction) {
        // Check if we need to reset the action window (1 second window)
        if (stats.actionWindowStartMs == 0 || 
            timestampMs - stats.actionWindowStartMs >= 1000) {
            stats.actionWindowStartMs = timestampMs;
            stats.actionCount = 1;
        } else {
            ++stats.actionCount;
        }
    }
    
    // Calculate current action rate (actions per second)
    float currentActionRate = 0.0f;
    if (stats.actionWindowStartMs > 0) {
        uint32_t windowDuration = timestampMs - stats.actionWindowStartMs;
        if (windowDuration > 0) {
            currentActionRate = static_cast<float>(stats.actionCount) * 1000.0f 
                              / static_cast<float>(windowDuration);
        } else {
            currentActionRate = static_cast<float>(stats.actionCount) * config_.tickRate;
        }
    }
    
    // Update EMAs
    if (stats.sampleCount == 0) {
        // First sample - initialize EMAs
        stats.emaSpeed = currentSpeed;
        stats.emaActionRate = currentActionRate;
        stats.emaVarianceSpeed = 0.0f;
        stats.emaVarianceActionRate = 0.0f;
    } else {
        // Update variance before updating EMA
        stats.emaVarianceSpeed = updateEMAVariance(
            stats.emaVarianceSpeed, stats.emaSpeed, currentSpeed, alpha);
        stats.emaVarianceActionRate = updateEMAVariance(
            stats.emaVarianceActionRate, stats.emaActionRate, currentActionRate, alpha);
        
        // Update EMAs
        stats.emaSpeed = updateEMA(stats.emaSpeed, currentSpeed, alpha);
        stats.emaActionRate = updateEMA(stats.emaActionRate, currentActionRate, alpha);
    }
    
    // Update accuracy EMA
    if (stats.hitsAttempted > 0) {
        float currentAccuracy = static_cast<float>(stats.hitsLanded) 
                              / static_cast<float>(stats.hitsAttempted) * 100.0f;
        if (stats.sampleCount == 0) {
            stats.emaAccuracy = currentAccuracy;
        } else {
            stats.emaAccuracy = updateEMA(stats.emaAccuracy, currentAccuracy, alpha);
        }
    }
    
    // Update speed history ring buffer
    stats.speedHistory[stats.historyIndex] = currentSpeed;
    stats.historyIndex = (stats.historyIndex + 1) % PlayerStatistics::HISTORY_CAPACITY;
    if (stats.historyCount < PlayerStatistics::HISTORY_CAPACITY) {
        ++stats.historyCount;
    }
    
    // Detect anomalies for this update
    bool speedAnomaly = false;
    bool positionAnomaly = false;
    bool actionRateAnomaly = false;
    bool accuracyAnomaly = false;
    
    // Speed anomaly: Z-score > threshold OR absolute velocity exceeds max
    if (stats.sampleCount > 0) {
        float speedZScore = calculateZScore(currentSpeed, stats.emaSpeed, 
                                            stats.emaVarianceSpeed);
        if (std::abs(speedZScore) > config_.zScoreThreshold || 
            currentSpeed > config_.maxVelocity) {
            speedAnomaly = true;
        }
    }
    
    // Position anomaly: impossible teleport distance per tick
    if (stats.sampleCount > 0 && dtMs > 0) {
        // Calculate max allowed distance based on tick rate
        float maxDistancePerTick = config_.maxTeleportDistance;
        if (positionDelta > maxDistancePerTick) {
            positionAnomaly = true;
        }
    }
    
    // Action rate anomaly: above 60Hz limit + tolerance
    if (currentActionRate > config_.maxActionsPerSecond) {
        actionRateAnomaly = true;
    }
    
    // Accuracy anomaly: sustained inhuman accuracy
    if (stats.hitsAttempted >= 10 && stats.emaAccuracy > config_.suspiciousAccuracyThreshold) {
        accuracyAnomaly = true;
    }
    
    // Update anomaly counters
    updateAnomalyCounters(stats, timestampMs, speedAnomaly, positionAnomaly,
                          actionRateAnomaly, accuracyAnomaly);
    
    // Update tracking state
    stats.lastPosition = position;
    stats.lastUpdateMs = timestampMs;
    ++stats.sampleCount;
}

void StatisticalDetector::recordHitAttempt(uint64_t playerId, bool hitLanded, 
                                            uint32_t timestampMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& stats = playerStats_[playerId];
    ++stats.hitsAttempted;
    if (hitLanded) {
        ++stats.hitsLanded;
    }
    
    // Update accuracy EMA with alpha
    float currentAccuracy = static_cast<float>(stats.hitsLanded) 
                          / static_cast<float>(stats.hitsAttempted) * 100.0f;
    if (stats.sampleCount == 0) {
        stats.emaAccuracy = currentAccuracy;
    } else {
        stats.emaAccuracy = updateEMA(stats.emaAccuracy, currentAccuracy, config_.emaAlpha);
    }
    
    // Check for accuracy anomaly after updating EMA
    bool accuracyAnomaly = false;
    if (stats.hitsAttempted >= 10 && stats.emaAccuracy > config_.suspiciousAccuracyThreshold) {
        accuracyAnomaly = true;
    }
    
    // Update accuracy anomaly counter
    if (accuracyAnomaly) {
        stats.accuracyAnomalyCount = std::min(stats.accuracyAnomalyCount + 1, 10u);
        stats.lastAccuracyAnomalyMs = timestampMs;
    } else if (timestampMs - stats.lastAccuracyAnomalyMs > 5000 && 
               stats.accuracyAnomalyCount > 0) {
        --stats.accuracyAnomalyCount;
    }
}

AnomalyScore StatisticalDetector::getAnomalyScore(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    AnomalyScore score;
    
    auto it = playerStats_.find(playerId);
    if (it == playerStats_.end()) {
        return score; // No data, return zeroed score
    }
    
    const auto& stats = it->second;
    const auto& cfg = config_;
    
    // Calculate confidence
    score.confidence = calculateConfidence(stats.sampleCount);
    
    if (stats.sampleCount < 2) {
        return score; // Not enough data
    }
    
    // Calculate speed Z-score and anomaly score
    score.speedZScore = calculateZScore(stats.emaSpeed, stats.emaSpeed, 
                                        stats.emaVarianceSpeed);
    // Use recent speed from history
    float recentSpeed = 0.0f;
    if (stats.historyCount > 0) {
        size_t lastIdx = (stats.historyIndex == 0) 
            ? PlayerStatistics::HISTORY_CAPACITY - 1 
            : stats.historyIndex - 1;
        recentSpeed = stats.speedHistory[lastIdx];
    }
    score.speedZScore = calculateZScore(recentSpeed, stats.emaSpeed, 
                                        stats.emaVarianceSpeed);
    score.speedScore = zScoreToAnomalyScore(score.speedZScore, cfg.zScoreThreshold);
    score.speedAnomaly = stats.speedAnomalyCount > 0;
    
    // Position anomaly score based on anomaly frequency
    score.positionScore = std::min(100.0f, 
        static_cast<float>(stats.positionAnomalyCount) * 33.3f);
    score.positionAnomaly = stats.positionAnomalyCount > 0;
    
    // Action rate Z-score and anomaly score
    score.actionRateZScore = calculateZScore(stats.emaActionRate, stats.emaActionRate,
                                             stats.emaVarianceActionRate);
    score.actionRateScore = zScoreToAnomalyScore(score.actionRateZScore, 
                                                 cfg.zScoreThreshold);
    // Also check absolute limit
    if (stats.emaActionRate > cfg.maxActionsPerSecond) {
        float excess = stats.emaActionRate - cfg.maxActionsPerSecond;
        score.actionRateScore = std::max(score.actionRateScore, 
            std::min(100.0f, excess / cfg.maxActionsPerSecond * 100.0f));
    }
    score.actionRateAnomaly = stats.actionRateAnomalyCount > 0;
    
    // Accuracy anomaly score
    if (stats.hitsAttempted >= 10) {
        if (stats.emaAccuracy > cfg.suspiciousAccuracyThreshold) {
            float excess = stats.emaAccuracy - cfg.suspiciousAccuracyThreshold;
            score.accuracyScore = std::min(100.0f, excess / (100.0f - cfg.suspiciousAccuracyThreshold) * 100.0f);
        }
    }
    score.accuracyAnomaly = stats.accuracyAnomalyCount > 0;
    
    // Calculate composite score
    score.composite = calculateCompositeScore(
        score.speedScore, score.positionScore, 
        score.actionRateScore, score.accuracyScore,
        cfg.speedWeight, cfg.positionWeight,
        cfg.actionRateWeight, cfg.accuracyWeight
    );
    
    // Total anomaly count
    score.totalAnomalyCount = calculateTotalAnomalies(stats);
    
    return score;
}

float StatisticalDetector::getConfidence(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerStats_.find(playerId);
    if (it == playerStats_.end()) {
        return 0.0f;
    }
    
    return calculateConfidence(it->second.sampleCount);
}

bool StatisticalDetector::isTracking(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playerStats_.find(playerId) != playerStats_.end();
}

// ============================================================================
// INDIVIDUAL DETECTORS
// ============================================================================

float StatisticalDetector::calculateSpeedZScore(uint64_t playerId, float currentSpeed) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerStats_.find(playerId);
    if (it == playerStats_.end() || it->second.sampleCount < 2) {
        return 0.0f;
    }
    
    const auto& stats = it->second;
    return calculateZScore(currentSpeed, stats.emaSpeed, stats.emaVarianceSpeed);
}

float StatisticalDetector::calculatePositionDelta(uint64_t playerId, 
                                                   const Position& newPos) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerStats_.find(playerId);
    if (it == playerStats_.end() || it->second.sampleCount == 0) {
        return 0.0f;
    }
    
    const auto& lastPos = it->second.lastPosition;
    float dx = (newPos.x - lastPos.x) * Constants::FIXED_TO_FLOAT;
    float dy = (newPos.y - lastPos.y) * Constants::FIXED_TO_FLOAT;
    float dz = (newPos.z - lastPos.z) * Constants::FIXED_TO_FLOAT;
    
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float StatisticalDetector::getCurrentActionRate(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerStats_.find(playerId);
    if (it == playerStats_.end()) {
        return 0.0f;
    }
    
    return it->second.emaActionRate;
}

float StatisticalDetector::getCurrentAccuracy(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerStats_.find(playerId);
    if (it == playerStats_.end() || it->second.hitsAttempted == 0) {
        return 0.0f;
    }
    
    return it->second.emaAccuracy;
}

// ============================================================================
// MANAGEMENT
// ============================================================================

void StatisticalDetector::removePlayer(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    playerStats_.erase(playerId);
}

void StatisticalDetector::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    playerStats_.clear();
}

size_t StatisticalDetector::getTrackedPlayerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playerStats_.size();
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

PlayerStatistics* StatisticalDetector::getStats(uint64_t playerId) {
    auto it = playerStats_.find(playerId);
    if (it != playerStats_.end()) {
        return &it->second;
    }
    return nullptr;
}

const PlayerStatistics* StatisticalDetector::getStats(uint64_t playerId) const {
    auto it = playerStats_.find(playerId);
    if (it != playerStats_.end()) {
        return &it->second;
    }
    return nullptr;
}

float StatisticalDetector::updateEMA(float current, float newValue, float alpha) {
    return alpha * newValue + (1.0f - alpha) * current;
}

float StatisticalDetector::updateEMAVariance(float currentVariance, float currentEMA,
                                              float newValue, float alpha) {
    // Welford's online algorithm adapted for EMA
    float diff = newValue - currentEMA;
    return (1.0f - alpha) * (currentVariance + alpha * diff * diff);
}

float StatisticalDetector::calculateZScore(float value, float ema, float emaVariance) {
    if (emaVariance <= 0.0f) {
        return 0.0f;
    }
    
    float stddev = std::sqrt(emaVariance);
    if (stddev <= 0.0001f) {
        return 0.0f;
    }
    
    return (value - ema) / stddev;
}

float StatisticalDetector::calculateCompositeScore(float speedScore, float positionScore,
                                                    float actionRateScore, float accuracyScore,
                                                    float speedWeight, float positionWeight,
                                                    float actionRateWeight, float accuracyWeight) {
    float totalWeight = speedWeight + positionWeight + actionRateWeight + accuracyWeight;
    if (totalWeight <= 0.0f) {
        return 0.0f;
    }
    
    float weighted = speedScore * speedWeight 
                   + positionScore * positionWeight
                   + actionRateScore * actionRateWeight
                   + accuracyScore * accuracyWeight;
    
    return std::clamp(weighted / totalWeight, 0.0f, 100.0f);
}

float StatisticalDetector::zScoreToAnomalyScore(float zScore, float threshold) {
    float absZ = std::abs(zScore);
    
    if (absZ <= threshold) {
        // Below threshold: scale 0 to 50 (normal range)
        return (absZ / threshold) * 50.0f;
    } else {
        // Above threshold: scale 50 to 100 (anomaly range)
        float excess = absZ - threshold;
        // Use sigmoid-like scaling for smoother transition
        float score = 50.0f + std::min(50.0f, excess / threshold * 50.0f);
        return score;
    }
}

float StatisticalDetector::calculateConfidence(uint32_t sampleCount) const {
    if (sampleCount < config_.minSamplesForConfidence) {
        return static_cast<float>(sampleCount) 
             / static_cast<float>(config_.minSamplesForConfidence) * 0.25f;
    }
    
    uint32_t excessSamples = sampleCount - config_.minSamplesForConfidence;
    uint32_t samplesForFull = config_.samplesForFullConfidence - config_.minSamplesForConfidence;
    
    float additionalConfidence = std::min(1.0f, 
        static_cast<float>(excessSamples) / static_cast<float>(samplesForFull));
    
    return 0.25f + additionalConfidence * 0.75f;
}

void StatisticalDetector::updateAnomalyCounters(PlayerStatistics& stats, 
                                                 uint32_t timestampMs,
                                                 bool speedAnomaly, bool positionAnomaly,
                                                 bool actionRateAnomaly, bool accuracyAnomaly) {
    // Decay old anomalies (every 5 seconds, reduce by 1)
    const uint32_t decayInterval = 5000;
    
    if (speedAnomaly) {
        stats.speedAnomalyCount = std::min(stats.speedAnomalyCount + 1, 10u);
        stats.lastSpeedAnomalyMs = timestampMs;
    } else if (timestampMs - stats.lastSpeedAnomalyMs > decayInterval && 
               stats.speedAnomalyCount > 0) {
        --stats.speedAnomalyCount;
    }
    
    if (positionAnomaly) {
        stats.positionAnomalyCount = std::min(stats.positionAnomalyCount + 1, 10u);
        stats.lastPositionAnomalyMs = timestampMs;
    } else if (timestampMs - stats.lastPositionAnomalyMs > decayInterval && 
               stats.positionAnomalyCount > 0) {
        --stats.positionAnomalyCount;
    }
    
    if (actionRateAnomaly) {
        stats.actionRateAnomalyCount = std::min(stats.actionRateAnomalyCount + 1, 10u);
        stats.lastActionRateAnomalyMs = timestampMs;
    } else if (timestampMs - stats.lastActionRateAnomalyMs > decayInterval && 
               stats.actionRateAnomalyCount > 0) {
        --stats.actionRateAnomalyCount;
    }
    
    if (accuracyAnomaly) {
        stats.accuracyAnomalyCount = std::min(stats.accuracyAnomalyCount + 1, 10u);
        stats.lastAccuracyAnomalyMs = timestampMs;
    } else if (timestampMs - stats.lastAccuracyAnomalyMs > decayInterval && 
               stats.accuracyAnomalyCount > 0) {
        --stats.accuracyAnomalyCount;
    }
}

uint32_t StatisticalDetector::calculateTotalAnomalies(const PlayerStatistics& stats) const {
    return stats.speedAnomalyCount 
         + stats.positionAnomalyCount 
         + stats.actionRateAnomalyCount
         + stats.accuracyAnomalyCount;
}

} // namespace Security
} // namespace DarkAges
