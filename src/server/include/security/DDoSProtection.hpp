#pragma once
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>
#include <memory>
#include <deque>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace DarkAges {
namespace Security {

// Forward declarations
class DDoSProtection;

// [SECURITY_AGENT] Connection tracking for rate limiting
struct ConnectionStats {
    uint32_t connectionId{0};
    std::string ipAddress;
    uint64_t firstSeenTime{0};
    uint64_t lastPacketTime{0};
    
    // Packet rates
    uint32_t packetsInWindow{0};
    uint32_t bytesInWindow{0};
    uint32_t windowStartTime{0};
    
    // Violations
    uint32_t rateLimitViolations{0};
    uint32_t suspiciousPackets{0};
    
    // State
    bool isBlocked{false};
    uint64_t blockedUntil{0};
    bool isWhitelisted{false};
};

// [SECURITY_AGENT] DDoS protection and rate limiting
class DDoSProtection {
public:
    struct Config {
        // Connection limits
        uint32_t maxConnectionsPerIP = 5;
        uint32_t maxConnectionsPerSubnet = 20;
        uint32_t maxGlobalConnections = 1000;
        
        // Rate limits (per second)
        uint32_t maxPacketsPerSecond = 100;
        uint32_t maxBytesPerSecond = 10240;  // 10 KB/s
        uint32_t maxHandshakeAttempts = 10;
        
        // Burst allowance
        uint32_t burstSize = 10;  // Allow 10 packet burst
        
        // Blocking
        uint32_t blockDurationSeconds = 300;  // 5 minutes
        uint32_t violationThreshold = 5;  // Block after 5 violations
        
        // Global thresholds
        uint32_t globalConnectionsThreshold = 800;  // Start throttling
        uint32_t globalPacketsThreshold = 50000;    // Suspend accepts
        
        // Default constructor required for C++20 aggregate rules
        Config() = default;
    };

public:
    explicit DDoSProtection(const Config& config);
    DDoSProtection();  // Default constructor
    
    // Initialize
    bool initialize();
    
    // Check if connection should be accepted
    bool shouldAcceptConnection(const std::string& ipAddress);
    
    // Process packet and check rate limits
    // Returns true if packet should be processed, false if dropped
    bool processPacket(uint32_t connectionId, const std::string& ipAddress,
                      uint32_t packetSize, uint32_t currentTimeMs);
    
    // Mark connection as disconnected
    void onConnectionClosed(uint32_t connectionId);
    
    // Manual IP blocking
    void blockIP(const std::string& ipAddress, uint32_t durationSeconds,
                const char* reason);
    void unblockIP(const std::string& ipAddress);
    bool isIPBlocked(const std::string& ipAddress) const;
    
    // Whitelist (bypasses rate limiting)
    void whitelistIP(const std::string& ipAddress);
    void unwhitelistIP(const std::string& ipAddress);
    bool isWhitelisted(const std::string& ipAddress) const;
    
    // Get current stats
    uint32_t getActiveConnectionCount() const;
    uint32_t getBlockedIPCount() const;
    uint32_t getCurrentPacketsPerSecond() const;
    
    // Update (call every tick to decay counters)
    void update(uint32_t currentTimeMs);
    
    // Emergency mode (under attack)
    void setEmergencyMode(bool enabled);
    bool isInEmergencyMode() const { return emergencyMode_; }

private:
    Config config_;
    
    // Connection tracking
    std::unordered_map<uint32_t, ConnectionStats> connections_;
    std::unordered_map<std::string, std::vector<uint32_t>> ipToConnections_;
    mutable std::mutex connectionsMutex_;
    
    // IP blocking
    std::unordered_map<std::string, uint64_t> blockedIPs_;  // IP -> unblock time
    std::unordered_set<std::string> whitelistedIPs_;
    mutable std::mutex blockMutex_;
    
    // Global rate tracking
    uint32_t globalPacketsInWindow_{0};
    uint32_t globalBytesInWindow_{0};
    uint32_t globalWindowStart_{0};
    
    // Emergency mode
    bool emergencyMode_{false};
    uint64_t emergencyModeStart_{0};
    
    // Stats
    uint32_t totalConnectionsBlocked_{0};
    uint32_t totalPacketsDropped_{0};
    
    // Helpers
    bool checkConnectionLimit(const std::string& ipAddress);
    bool checkRateLimit(ConnectionStats& stats, uint32_t packetSize,
                       uint32_t currentTimeMs);
    std::string getSubnet(const std::string& ipAddress) const;
    void cleanupExpiredBlocks(uint32_t currentTimeMs);
};

// [SECURITY_AGENT] Circuit breaker for external services
class CircuitBreaker {
public:
    enum class State {
        CLOSED,      // Normal operation
        OPEN,        // Failing, rejecting requests
        HALF_OPEN    // Testing if service recovered
    };
    
    struct Config {
        uint32_t failureThreshold = 5;        // Open after 5 failures
        uint32_t successThreshold = 3;        // Close after 3 successes
        uint32_t timeoutMs = 30000;           // Try again after 30s
        uint32_t halfOpenMaxCalls = 3;        // Max calls in half-open
        
        Config() = default;
    };

public:
    explicit CircuitBreaker(const std::string& name, const Config& config);
    CircuitBreaker(const std::string& name);  // Default config
    
    // Call before attempting operation
    // Returns true if operation should proceed
    bool allowRequest();
    
    // Call on success
    void recordSuccess();
    
    // Call on failure
    void recordFailure();
    
    // Get current state
    State getState() const { return state_; }
    const char* getStateString() const;
    
    // Force state (for testing)
    void forceState(State state);

private:
    std::string name_;
    Config config_;
    
    State state_{State::CLOSED};
    uint32_t failureCount_{0};
    uint32_t successCount_{0};
    uint32_t halfOpenCalls_{0};
    uint64_t lastFailureTime_{0};
    
    mutable std::mutex mutex_;
};

// [SECURITY_AGENT] Input validation
class InputValidator {
public:
    // Validate position coordinates
    static bool isValidPosition(const Position& pos);
    
    // Validate rotation values
    static bool isValidRotation(const Rotation& rot);
    
    // Validate input sequence
    static bool isValidInputSequence(uint32_t sequence, uint32_t lastSequence);
    
    // Sanitize string input
    static std::string sanitizeString(const std::string& input, size_t maxLength);
    
    // Validate packet size
    static bool isValidPacketSize(size_t size);
    
    // Validate protocol version
    static bool isValidProtocolVersion(uint32_t version);

private:
    static constexpr size_t MAX_PACKET_SIZE = 1400;
    static constexpr size_t MAX_STRING_LENGTH = 256;
};

// [SECURITY_AGENT] Token bucket rate limiter
template<typename Key>
class TokenBucketRateLimiter {
public:
    struct Config {
        uint32_t maxTokens = 100;       // Maximum burst size
        uint32_t tokensPerSecond = 60; // Refill rate
    };
    
    struct Bucket {
        uint32_t tokens;
        uint64_t lastRefillTime;
    };

public:
    explicit TokenBucketRateLimiter(const Config& config = Config{})
        : config_(config) {}
    
    // Check if operation allowed, consume token if so
    bool allow(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        auto& bucket = buckets_[key];
        refill(bucket, now);
        
        if (bucket.tokens > 0) {
            --bucket.tokens;
            return true;
        }
        return false;
    }
    
    // Peek without consuming
    bool wouldAllow(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = buckets_.find(key);
        if (it == buckets_.end()) {
            return config_.maxTokens > 0;
        }
        
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        Bucket bucket = it->second;
        refill(bucket, now);
        
        return bucket.tokens > 0;
    }
    
    // Get remaining tokens
    uint32_t getRemainingTokens(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = buckets_.find(key);
        if (it == buckets_.end()) {
            return config_.maxTokens;
        }
        
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        Bucket bucket = it->second;
        refill(bucket, now);
        
        return bucket.tokens;
    }
    
    // Reset bucket for key
    void reset(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        buckets_.erase(key);
    }

private:
    void refill(Bucket& bucket, uint64_t now) const {
        uint64_t elapsedMs = now - bucket.lastRefillTime;
        if (elapsedMs > 0) {
            uint32_t tokensToAdd = static_cast<uint32_t>(
                (elapsedMs * config_.tokensPerSecond) / 1000);
            bucket.tokens = std::min(config_.maxTokens, bucket.tokens + tokensToAdd);
            bucket.lastRefillTime = now;
        }
    }
    
    Config config_;
    mutable std::unordered_map<Key, Bucket> buckets_;
    mutable std::mutex mutex_;
};

// Convenience typedefs
using IPRateLimiter = TokenBucketRateLimiter<std::string>;
using ConnectionRateLimiter = TokenBucketRateLimiter<uint32_t>;

// [SECURITY_AGENT] Per-IP connection throttling
class ConnectionThrottler {
public:
    struct Config {
        uint32_t maxAttempts = 10;          // Max attempts per window
        uint32_t windowSeconds = 60;        // Time window
        uint32_t blockDurationSeconds = 300; // Block duration
        
        Config() = default;
    };

public:
    explicit ConnectionThrottler(const Config& config);
    ConnectionThrottler();  // Default config
    
    // Check if connection attempt is allowed
    bool allowConnection(const std::string& ipAddress);
    
    // Record connection attempt
    void recordAttempt(const std::string& ipAddress, bool success);
    
    // Cleanup old entries
    void cleanup(uint32_t currentTimeMs);

private:
    struct AttemptHistory {
        std::deque<uint64_t> timestamps;  // Attempt timestamps (seconds)
        uint32_t successCount{0};
        uint32_t failureCount{0};
        uint64_t blockedUntil{0};
    };
    
    Config config_;
    std::unordered_map<std::string, AttemptHistory> histories_;
    mutable std::mutex mutex_;
};

// [SECURITY_AGENT] Global traffic analyzer for attack detection
class TrafficAnalyzer {
public:
    struct Config {
        uint32_t baselineWindowMs = 60000;      // 1 minute baseline
        uint32_t spikeThresholdPercent = 300;    // 3x baseline = attack
        uint32_t minBaselineSamples = 100;       // Min samples for detection
        
        Config() = default;
    };
    
    struct TrafficStats {
        uint32_t connectionsPerSecond{0};
        uint32_t packetsPerSecond{0};
        uint32_t bytesPerSecond{0};
        uint32_t uniqueIPs{0};
    };

public:
    explicit TrafficAnalyzer(const Config& config);
    TrafficAnalyzer();  // Default config
    
    // Record connection attempt
    void recordConnection(const std::string& ipAddress, uint32_t currentTimeMs);
    
    // Record packet
    void recordPacket(uint32_t size, uint32_t currentTimeMs);
    
    // Check if traffic pattern indicates attack
    bool detectAttack(uint32_t currentTimeMs) const;
    
    // Get current stats
    TrafficStats getCurrentStats(uint32_t currentTimeMs) const;
    
    // Update (call every tick)
    void update(uint32_t currentTimeMs);

private:
    Config config_;
    
    struct TimeWindow {
        uint32_t startTime{0};
        uint32_t connectionCount{0};
        uint32_t packetCount{0};
        uint32_t byteCount{0};
        std::unordered_set<std::string> uniqueIPs;
    };
    
    TimeWindow currentWindow_;
    std::deque<std::pair<uint32_t, TrafficStats>> history_;  // (timestamp, stats)
    
    mutable std::mutex mutex_;
};

} // namespace Security
} // namespace DarkAges
