// [SECURITY_AGENT] Rate limiting system implementation
// Per-IP connection limiting, per-player input rate limiting, DDoS flood detection

#include "security/RateLimiter.hpp"

namespace DarkAges {
namespace Security {

// ============================================================================
// RateLimiter Implementation
// ============================================================================

RateLimiter::RateLimiter(const Config& config)
    : config_(config) {}

RateLimiter::RateLimiter()
    : config_(Config{}) {}

// -----------------------------------------------------------------------
// Connection limiting (per-IP)
// -----------------------------------------------------------------------

bool RateLimiter::checkConnectionLimit(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ipConnections_.find(ipAddress);
    if (it == ipConnections_.end()) {
        return true;  // No connections yet from this IP
    }

    return it->second.activeConnections < config_.maxConnectionsPerIP;
}

void RateLimiter::recordConnection(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& entry = ipConnections_[ipAddress];
    entry.activeConnections++;
    entry.lastActivityMs = getCurrentTimeMs();
}

void RateLimiter::recordDisconnection(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ipConnections_.find(ipAddress);
    if (it == ipConnections_.end()) {
        return;
    }

    if (it->second.activeConnections > 0) {
        it->second.activeConnections--;
    }
    it->second.lastActivityMs = getCurrentTimeMs();

    // Remove entry if no active connections remain
    if (it->second.activeConnections == 0) {
        ipConnections_.erase(it);
    }
}

std::uint32_t RateLimiter::getConnectionCount(const std::string& ipAddress) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ipConnections_.find(ipAddress);
    if (it == ipConnections_.end()) {
        return 0;
    }
    return it->second.activeConnections;
}

// -----------------------------------------------------------------------
// Input rate limiting (per-player, sliding window)
// -----------------------------------------------------------------------

bool RateLimiter::checkInputRate(std::uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = playerInputs_.find(playerId);
    if (it == playerInputs_.end()) {
        return true;  // No inputs yet from this player
    }

    std::uint64_t now = getCurrentTimeMs();

    // Clean expired entries from the window
    auto& timestamps = it->second.timestamps;
    std::uint64_t cutoff = now - config_.inputWindowDurationMs;

    while (!timestamps.empty() && timestamps.front() < cutoff) {
        timestamps.pop_front();
    }

    return timestamps.size() < config_.maxInputPerSecond;
}

void RateLimiter::recordInput(std::uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::uint64_t now = getCurrentTimeMs();

    auto& entry = playerInputs_[playerId];
    entry.lastActivityMs = now;

    // Clean expired entries before recording
    std::uint64_t cutoff = now - config_.inputWindowDurationMs;
    while (!entry.timestamps.empty() && entry.timestamps.front() < cutoff) {
        entry.timestamps.pop_front();
    }

    entry.timestamps.push_back(now);
}

std::uint32_t RateLimiter::getInputCount(std::uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = playerInputs_.find(playerId);
    if (it == playerInputs_.end()) {
        return 0;
    }

    std::uint64_t now = getCurrentTimeMs();
    std::uint64_t cutoff = now - config_.inputWindowDurationMs;

    auto& timestamps = it->second.timestamps;
    std::uint32_t count = 0;
    for (auto ts : timestamps) {
        if (ts >= cutoff) {
            count++;
        }
    }
    return count;
}

// -----------------------------------------------------------------------
// DDoS connection flood detection (per-IP)
// -----------------------------------------------------------------------

bool RateLimiter::checkDDoSThreshold(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ipConnectionAttempts_.find(ipAddress);
    if (it == ipConnectionAttempts_.end()) {
        return false;  // No attempts from this IP, not a flood
    }

    std::uint64_t now = getCurrentTimeMs();
    auto& timestamps = it->second.timestamps;
    std::uint64_t cutoff = now - config_.connectionWindowDurationMs;

    // Clean expired entries
    while (!timestamps.empty() && timestamps.front() < cutoff) {
        timestamps.pop_front();
    }

    // Threshold exceeded = potential DDoS
    return timestamps.size() >= config_.maxConnectionsPerSecondPerIP;
}

void RateLimiter::recordConnectionAttempt(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::uint64_t now = getCurrentTimeMs();

    auto& entry = ipConnectionAttempts_[ipAddress];
    entry.lastActivityMs = now;

    // Clean expired entries before recording
    std::uint64_t cutoff = now - config_.connectionWindowDurationMs;
    while (!entry.timestamps.empty() && entry.timestamps.front() < cutoff) {
        entry.timestamps.pop_front();
    }

    entry.timestamps.push_back(now);
}

// -----------------------------------------------------------------------
// Maintenance
// -----------------------------------------------------------------------

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    ipConnections_.clear();
    ipConnectionAttempts_.clear();
    playerInputs_.clear();
}

void RateLimiter::cleanupStale(std::uint64_t maxAgeMs) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::uint64_t now = getCurrentTimeMs();
    std::uint64_t cutoff = now - maxAgeMs;

    // Cleanup stale IP connections
    for (auto it = ipConnections_.begin(); it != ipConnections_.end(); ) {
        if (it->second.activeConnections == 0 ||
            it->second.lastActivityMs < cutoff) {
            it = ipConnections_.erase(it);
        } else {
            ++it;
        }
    }

    // Cleanup stale IP connection attempts
    for (auto it = ipConnectionAttempts_.begin(); it != ipConnectionAttempts_.end(); ) {
        if (it->second.lastActivityMs < cutoff) {
            it = ipConnectionAttempts_.erase(it);
        } else {
            ++it;
        }
    }

    // Cleanup stale player inputs
    for (auto it = playerInputs_.begin(); it != playerInputs_.end(); ) {
        if (it->second.lastActivityMs < cutoff) {
            it = playerInputs_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t RateLimiter::getTrackedIPCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ipConnections_.size();
}

std::size_t RateLimiter::getTrackedPlayerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playerInputs_.size();
}

// -----------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------

std::uint64_t RateLimiter::getCurrentTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

std::size_t RateLimiter::cleanWindow(std::deque<std::uint64_t>& timestamps,
                                      std::uint64_t windowDurationMs) const {
    std::uint64_t now = getCurrentTimeMs();
    std::uint64_t cutoff = now - windowDurationMs;

    while (!timestamps.empty() && timestamps.front() < cutoff) {
        timestamps.pop_front();
    }

    return timestamps.size();
}

} // namespace Security
} // namespace DarkAges
