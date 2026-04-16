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

} // namespace Security
} // namespace DarkAges
