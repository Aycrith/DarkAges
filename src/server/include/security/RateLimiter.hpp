#pragma once
#include "security/DDoSProtection.hpp"
#include <cstdint>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <string>

namespace DarkAges {
namespace Security {

// [SECURITY_AGENT] Sliding window rate limiter
template<typename Key>
class SlidingWindowRateLimiter {
public:
    struct Config {
        uint32_t maxRequests = 60;      // Max requests in window
        uint32_t windowMs = 60000;      // Window size in milliseconds
    };

public:
    explicit SlidingWindowRateLimiter(const Config& config = Config{})
        : config_(config) {}
    
    // Check if request allowed
    bool allow(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        auto& window = windows_[key];
        
        // Remove old entries
        cleanup(window, now);
        
        // Check limit
        if (window.requests.size() >= config_.maxRequests) {
            return false;
        }
        
        // Add request
        window.requests.push_back(now);
        return true;
    }
    
    // Get remaining requests in current window
    uint32_t getRemaining(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = windows_.find(key);
        if (it == windows_.end()) {
            return config_.maxRequests;
        }
        
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        // Make a copy for cleanup
        auto window = it->second;
        cleanup(window, now);
        
        if (config_.maxRequests > window.requests.size()) {
            return static_cast<uint32_t>(config_.maxRequests - window.requests.size());
        }
        return 0;
    }
    
    // Reset window for key
    void reset(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        windows_.erase(key);
    }
    
    // Cleanup all expired entries
    void cleanupAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        for (auto it = windows_.begin(); it != windows_.end();) {
            cleanup(it->second, now);
            
            if (it->second.requests.empty()) {
                it = windows_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Window {
        std::deque<uint64_t> requests;
    };
    
    void cleanup(Window& window, uint64_t now) const {
        uint64_t cutoff = now - config_.windowMs;
        
        while (!window.requests.empty() && window.requests.front() < cutoff) {
            window.requests.pop_front();
        }
    }
    
    Config config_;
    mutable std::unordered_map<Key, Window> windows_;
    mutable std::mutex mutex_;
};

// Convenience typedefs
using IPSlidingWindowLimiter = SlidingWindowRateLimiter<std::string>;
using ConnectionSlidingWindowLimiter = SlidingWindowRateLimiter<uint32_t>;

// [SECURITY_AGENT] Adaptive rate limiter that adjusts based on server load
class AdaptiveRateLimiter {
public:
    struct Config {
        uint32_t normalRate = 100;      // Normal requests per second
        uint32_t stressedRate = 50;     // Reduced rate under load
        uint32_t criticalRate = 20;     // Minimal rate under critical load
        
        float stressThreshold = 0.7f;   // CPU/load threshold for stress mode
        float criticalThreshold = 0.9f; // CPU/load threshold for critical mode
        
        Config() = default;  // C++20 aggregate fix
    };

public:
    explicit AdaptiveRateLimiter(const Config& config)
        : config_(config) {}
    
    // Check if request allowed from IP
    bool allow(const std::string& ipAddress, float currentServerLoad);
    
    // Update server load (call periodically)
    void updateServerLoad(float load);
    
    // Get current effective rate limit
    uint32_t getCurrentRateLimit() const;
    
    // Check current server state
    bool isStressed() const { return currentLoad_ > config_.stressThreshold; }
    bool isCritical() const { return currentLoad_ > config_.criticalThreshold; }

private:
    Config config_;
    float currentLoad_{0.0f};
    
    TokenBucketRateLimiter<std::string> limiter_;
    mutable std::mutex mutex_;
};

// [SECURITY_AGENT] Rate limiting middleware for NetworkManager
class NetworkRateLimiter {
public:
    struct Config {
        // Connection rate limits
        TokenBucketRateLimiter<std::string>::Config connectionRate{
            10,     // maxTokens (burst)
            2       // tokensPerSecond (sustained)
        };
        
        // Packet rate limits per connection
        TokenBucketRateLimiter<uint32_t>::Config packetRate{
            120,    // maxTokens (burst - 2 seconds worth)
            60      // tokensPerSecond (sustained)
        };
        
        // Message rate limits (reliable messages)
        TokenBucketRateLimiter<uint32_t>::Config messageRate{
            30,     // maxTokens (burst)
            10      // tokensPerSecond (sustained)
        };
        
        Config() = default;  // C++20 aggregate fix
    };

public:
    explicit NetworkRateLimiter(const Config& config)
        : connectionLimiter_(config.connectionRate)
        , packetLimiter_(config.packetRate)
        , messageLimiter_(config.messageRate) {}
    
    NetworkRateLimiter() : NetworkRateLimiter(Config{}) {}
    
    // Check if connection attempt is allowed
    bool allowConnection(const std::string& ipAddress) {
        return connectionLimiter_.allow(ipAddress);
    }
    
    // Check if packet is allowed
    bool allowPacket(uint32_t connectionId) {
        return packetLimiter_.allow(connectionId);
    }
    
    // Check if reliable message is allowed
    bool allowMessage(uint32_t connectionId) {
        return messageLimiter_.allow(connectionId);
    }
    
    // Reset limits for connection
    void onConnectionClosed(uint32_t connectionId) {
        packetLimiter_.reset(connectionId);
        messageLimiter_.reset(connectionId);
    }
    
    // Get remaining tokens
    uint32_t getRemainingPacketTokens(uint32_t connectionId) const {
        return packetLimiter_.getRemainingTokens(connectionId);
    }

private:
    TokenBucketRateLimiter<std::string> connectionLimiter_;
    TokenBucketRateLimiter<uint32_t> packetLimiter_;
    TokenBucketRateLimiter<uint32_t> messageLimiter_;
};

} // namespace Security
} // namespace DarkAges
