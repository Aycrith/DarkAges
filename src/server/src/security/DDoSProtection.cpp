#include "security/DDoSProtection.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace DarkAges {
namespace Security {

// ============================================================================
// DDoSProtection Implementation
// ============================================================================

DDoSProtection::DDoSProtection(const Config& config) : config_(config) {}

DDoSProtection::DDoSProtection() : config_(Config{}) {}

bool DDoSProtection::initialize() {
    std::cout << "[DDOS] Protection initialized\n";
    std::cout << "  Max connections/IP: " << config_.maxConnectionsPerIP << "\n";
    std::cout << "  Max packets/sec: " << config_.maxPacketsPerSecond << "\n";
    std::cout << "  Block duration: " << config_.blockDurationSeconds << "s\n";
    std::cout << "  Violation threshold: " << config_.violationThreshold << "\n";
    return true;
}

bool DDoSProtection::shouldAcceptConnection(const std::string& ipAddress) {
    // Check if IP is blocked first
    {
        std::lock_guard<std::mutex> blockLock(blockMutex_);
        
        auto blockIt = blockedIPs_.find(ipAddress);
        if (blockIt != blockedIPs_.end()) {
            // Check if block expired
            uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            if (now < blockIt->second) {
                ++totalConnectionsBlocked_;
                return false;  // Still blocked
            } else {
                blockedIPs_.erase(blockIt);  // Unblock
            }
        }
        
        // Check whitelist - allow immediately
        if (whitelistedIPs_.find(ipAddress) != whitelistedIPs_.end()) {
            return true;
        }
    }
    
    // Check emergency mode
    if (emergencyMode_) {
        ++totalConnectionsBlocked_;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    // Check per-IP limit
    auto it = ipToConnections_.find(ipAddress);
    if (it != ipToConnections_.end() && 
        it->second.size() >= config_.maxConnectionsPerIP) {
        return false;
    }
    
    // Check subnet limit
    std::string subnet = getSubnet(ipAddress);
    uint32_t subnetConnections = 0;
    for (const auto& [ip, conns] : ipToConnections_) {
        if (getSubnet(ip) == subnet) {
            subnetConnections += static_cast<uint32_t>(conns.size());
        }
    }
    if (subnetConnections >= config_.maxConnectionsPerSubnet) {
        return false;
    }
    
    // Check global limit
    if (connections_.size() >= config_.maxGlobalConnections) {
        return false;
    }
    
    // Check if approaching threshold and start throttling
    if (connections_.size() >= config_.globalConnectionsThreshold) {
        // Simple throttling: reject 50% of new connections
        static uint32_t counter = 0;
        if (++counter % 2 == 0) {
            return false;
        }
    }
    
    // Accept connection - register it
    // Create a placeholder connection entry
    static uint32_t nextConnectionId = 1;
    uint32_t connectionId = nextConnectionId++;
    
    ConnectionStats stats;
    stats.connectionId = connectionId;
    stats.ipAddress = ipAddress;
    stats.firstSeenTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    connections_[connectionId] = stats;
    ipToConnections_[ipAddress].push_back(connectionId);
    
    return true;
}

bool DDoSProtection::processPacket(uint32_t connectionId, 
                                  const std::string& ipAddress,
                                  uint32_t packetSize,
                                  uint32_t currentTimeMs) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    // Get or create stats
    ConnectionStats& stats = connections_[connectionId];
    stats.connectionId = connectionId;
    if (stats.ipAddress.empty()) {
        stats.ipAddress = ipAddress;
        stats.firstSeenTime = currentTimeMs;
        
        // Add to IP mapping
        ipToConnections_[ipAddress].push_back(connectionId);
    }
    stats.lastPacketTime = currentTimeMs;
    
    // Check whitelist
    {
        std::lock_guard<std::mutex> blockLock(blockMutex_);
        if (whitelistedIPs_.find(ipAddress) != whitelistedIPs_.end()) {
            stats.isWhitelisted = true;
            return true;
        }
    }
    
    // Check if blocked
    if (stats.isBlocked && currentTimeMs < stats.blockedUntil) {
        ++totalPacketsDropped_;
        return false;
    }
    
    // Reset window if needed (1 second window)
    if (currentTimeMs - stats.windowStartTime > 1000) {
        stats.windowStartTime = currentTimeMs;
        stats.packetsInWindow = 0;
        stats.bytesInWindow = 0;
    }
    
    // Check rate limit
    if (!checkRateLimit(stats, packetSize, currentTimeMs)) {
        ++totalPacketsDropped_;
        return false;
    }
    
    return true;
}

bool DDoSProtection::checkRateLimit(ConnectionStats& stats, uint32_t packetSize,
                                   uint32_t currentTimeMs) {
    // Check packet rate (with burst allowance)
    if (stats.packetsInWindow >= config_.maxPacketsPerSecond + config_.burstSize) {
        ++stats.rateLimitViolations;
        
        if (stats.rateLimitViolations >= config_.violationThreshold) {
            // Block connection
            stats.isBlocked = true;
            stats.blockedUntil = currentTimeMs + (config_.blockDurationSeconds * 1000);
            
            // Also block the IP
            blockIP(stats.ipAddress, config_.blockDurationSeconds, 
                   "Rate limit violation threshold exceeded");
        }
        
        return false;
    }
    
    // Check byte rate
    if (stats.bytesInWindow >= config_.maxBytesPerSecond) {
        return false;
    }
    
    // Update counters
    ++stats.packetsInWindow;
    stats.bytesInWindow += packetSize;
    
    return true;
}

void DDoSProtection::onConnectionClosed(uint32_t connectionId) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    auto it = connections_.find(connectionId);
    if (it != connections_.end()) {
        const std::string& ip = it->second.ipAddress;
        
        // Remove from IP mapping
        auto ipIt = ipToConnections_.find(ip);
        if (ipIt != ipToConnections_.end()) {
            auto& conns = ipIt->second;
            conns.erase(std::remove(conns.begin(), conns.end(), connectionId), 
                       conns.end());
            
            if (conns.empty()) {
                ipToConnections_.erase(ipIt);
            }
        }
        
        connections_.erase(it);
    }
}

void DDoSProtection::blockIP(const std::string& ipAddress, 
                            uint32_t durationSeconds,
                            const char* reason) {
    std::lock_guard<std::mutex> lock(blockMutex_);
    
    uint64_t unblockTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() + durationSeconds;
    
    blockedIPs_[ipAddress] = unblockTime;
    ++totalConnectionsBlocked_;
    
    std::cerr << "[DDOS] Blocking IP " << ipAddress 
              << " for " << durationSeconds << "s: " << reason << "\n";
}

void DDoSProtection::unblockIP(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(blockMutex_);
    blockedIPs_.erase(ipAddress);
}

bool DDoSProtection::isIPBlocked(const std::string& ipAddress) const {
    std::lock_guard<std::mutex> lock(blockMutex_);
    return blockedIPs_.find(ipAddress) != blockedIPs_.end();
}

void DDoSProtection::whitelistIP(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(blockMutex_);
    whitelistedIPs_.insert(ipAddress);
    std::cout << "[DDOS] Whitelisted IP: " << ipAddress << "\n";
}

void DDoSProtection::unwhitelistIP(const std::string& ipAddress) {
    std::lock_guard<std::mutex> lock(blockMutex_);
    whitelistedIPs_.erase(ipAddress);
}

bool DDoSProtection::isWhitelisted(const std::string& ipAddress) const {
    std::lock_guard<std::mutex> lock(blockMutex_);
    return whitelistedIPs_.find(ipAddress) != whitelistedIPs_.end();
}

void DDoSProtection::setEmergencyMode(bool enabled) {
    emergencyMode_ = enabled;
    
    if (enabled) {
        emergencyModeStart_ = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::cerr << "[DDOS] EMERGENCY MODE ACTIVATED - New connections suspended\n";
    } else {
        std::cerr << "[DDOS] Emergency mode deactivated\n";
    }
}

uint32_t DDoSProtection::getActiveConnectionCount() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    return static_cast<uint32_t>(connections_.size());
}

uint32_t DDoSProtection::getBlockedIPCount() const {
    std::lock_guard<std::mutex> lock(blockMutex_);
    return static_cast<uint32_t>(blockedIPs_.size());
}

uint32_t DDoSProtection::getCurrentPacketsPerSecond() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    uint32_t total = 0;
    for (const auto& [id, stats] : connections_) {
        total += stats.packetsInWindow;
    }
    return total;
}

void DDoSProtection::update(uint32_t currentTimeMs) {
    // Cleanup expired blocks every 60 seconds
    static uint32_t lastCleanup = 0;
    if (currentTimeMs - lastCleanup > 60000) {
        cleanupExpiredBlocks(currentTimeMs);
        lastCleanup = currentTimeMs;
    }
    
    // Auto-disable emergency mode after 5 minutes
    if (emergencyMode_) {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        if (now - emergencyModeStart_ > 300) {
            setEmergencyMode(false);
        }
    }
}

void DDoSProtection::cleanupExpiredBlocks(uint32_t currentTimeMs) {
    std::lock_guard<std::mutex> lock(blockMutex_);
    
    uint64_t now = currentTimeMs / 1000;
    
    for (auto it = blockedIPs_.begin(); it != blockedIPs_.end();) {
        if (now >= it->second) {
            std::cout << "[DDOS] Auto-unblocking IP: " << it->first << "\n";
            it = blockedIPs_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string DDoSProtection::getSubnet(const std::string& ipAddress) const {
    // Simple /24 subnet extraction for IPv4
    // Format: xxx.xxx.xxx.xxx -> xxx.xxx.xxx.0/24
    size_t lastDot = ipAddress.rfind('.');
    if (lastDot != std::string::npos) {
        return ipAddress.substr(0, lastDot) + ".0/24";
    }
    return ipAddress;
}

} // namespace Security
} // namespace DarkAges
