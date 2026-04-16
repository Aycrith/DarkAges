#include "security/RateLimiter.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <cstdint>

namespace DarkAges {
namespace Security {

// ============================================================================
// AdaptiveRateLimiter Implementation
// ============================================================================

bool AdaptiveRateLimiter::allow(const std::string& ipAddress, float currentServerLoad) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update load
    currentLoad_ = currentServerLoad;
    
    // Adjust rate limit based on load
    uint32_t effectiveRate;
    if (currentLoad_ > config_.criticalThreshold) {
        effectiveRate = config_.criticalRate;
    } else if (currentLoad_ > config_.stressThreshold) {
        effectiveRate = config_.stressedRate;
    } else {
        effectiveRate = config_.normalRate;
    }
    
    // Note: In a real implementation, you'd update the limiter's rate
    // For now, just use the existing limiter
    return limiter_.allow(ipAddress);
}

void AdaptiveRateLimiter::updateServerLoad(float load) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentLoad_ = load;
}

uint32_t AdaptiveRateLimiter::getCurrentRateLimit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (currentLoad_ > config_.criticalThreshold) {
        return config_.criticalRate;
    } else if (currentLoad_ > config_.stressThreshold) {
        return config_.stressedRate;
    }
    return config_.normalRate;
}

// ============================================================================  
// ConnectionThrottler Implementation
// ============================================================================

ConnectionThrottler::ConnectionThrottler(const Config& config)
    : config_(config) {}

ConnectionThrottler::ConnectionThrottler()
    : config_(Config{}) {}

bool ConnectionThrottler::allowConnection(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = histories_.find(ipAddress);
    if (it == histories_.end()) {
        return true;  // No history, allow
    }
    
    const auto& history = it->second;
    
    // Check if currently blocked
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    if (history.blockedUntil > now) {
        return false;  // Still blocked
    }
    
    return history.timestamps.size() < config_.maxAttempts;
}

void ConnectionThrottler::recordAttempt(const std::string& ipAddress, bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    auto& history = histories_[ipAddress];
    history.timestamps.push_back(now);
    
    if (success) {
        ++history.successCount;
    } else {
        ++history.failureCount;
    }
    
    // Remove old entries outside window
    while (!history.timestamps.empty() && 
           (now - history.timestamps.front()) > config_.windowSeconds) {
        history.timestamps.pop_front();
    }
    
    // Check if should block
    if (history.timestamps.size() >= config_.maxAttempts) {
        history.blockedUntil = now + config_.blockDurationSeconds;
        std::cerr << "[THROTTLE] Blocking IP " << ipAddress 
                  << " for " << config_.blockDurationSeconds << "s (too many attempts)\n";
    }
}

void ConnectionThrottler::cleanup(uint32_t currentTimeMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t now = currentTimeMs / 1000;
    
    for (auto it = histories_.begin(); it != histories_.end();) {
        auto& history = it->second;
        
        // Remove old entries
        while (!history.timestamps.empty() && 
               (now - history.timestamps.front()) > config_.windowSeconds) {
            history.timestamps.pop_front();
        }
        
        // Remove empty entries
        if (history.timestamps.empty() && history.blockedUntil <= now) {
            it = histories_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Security
} // namespace DarkAges
