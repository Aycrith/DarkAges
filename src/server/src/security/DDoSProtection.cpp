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

// ============================================================================
// CircuitBreaker Implementation
// ============================================================================

CircuitBreaker::CircuitBreaker(const std::string& name, const Config& config)
    : name_(name), config_(config) {}

CircuitBreaker::CircuitBreaker(const std::string& name)
    : name_(name), config_(Config{}) {}

bool CircuitBreaker::allowRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (state_) {
        case State::CLOSED:
            return true;
            
        case State::OPEN: {
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            if (now - lastFailureTime_ > config_.timeoutMs) {
                // Try half-open
                state_ = State::HALF_OPEN;
                halfOpenCalls_ = 0;
                successCount_ = 0;
                std::cout << "[CIRCUIT] " << name_ << " entering HALF_OPEN\n";
                return true;
            }
            return false;
        }
        
        case State::HALF_OPEN:
            if (halfOpenCalls_ < config_.halfOpenMaxCalls) {
                ++halfOpenCalls_;
                return true;
            }
            return false;
    }
    
    return false;
}

void CircuitBreaker::recordSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (state_) {
        case State::CLOSED:
            failureCount_ = 0;
            break;
            
        case State::HALF_OPEN:
            ++successCount_;
            if (successCount_ >= config_.successThreshold) {
                state_ = State::CLOSED;
                failureCount_ = 0;
                std::cout << "[CIRCUIT] " << name_ << " CLOSED (recovered)\n";
            }
            break;
            
        case State::OPEN:
            break;
    }
}

void CircuitBreaker::recordFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    lastFailureTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    switch (state_) {
        case State::CLOSED:
            ++failureCount_;
            if (failureCount_ >= config_.failureThreshold) {
                state_ = State::OPEN;
                std::cerr << "[CIRCUIT] " << name_ << " OPEN (too many failures)\n";
            }
            break;
            
        case State::HALF_OPEN:
            state_ = State::OPEN;
            std::cerr << "[CIRCUIT] " << name_ << " OPEN (half-open failed)\n";
            break;
            
        case State::OPEN:
            break;
    }
}

const char* CircuitBreaker::getStateString() const {
    switch (state_) {
        case State::CLOSED: return "CLOSED";
        case State::OPEN: return "OPEN";
        case State::HALF_OPEN: return "HALF_OPEN";
    }
    return "UNKNOWN";
}

void CircuitBreaker::forceState(State state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    failureCount_ = 0;
    successCount_ = 0;
    halfOpenCalls_ = 0;
}

// ============================================================================
// InputValidator Implementation
// ============================================================================

bool InputValidator::isValidPosition(const Position& pos) {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    return x >= Constants::WORLD_MIN_X && x <= Constants::WORLD_MAX_X &&
           y >= Constants::WORLD_MIN_Y && y <= Constants::WORLD_MAX_Y &&
           z >= Constants::WORLD_MIN_Z && z <= Constants::WORLD_MAX_Z;
}

bool InputValidator::isValidRotation(const Rotation& rot) {
    return rot.yaw >= -6.28318f && rot.yaw <= 6.28318f &&  // ±2π
           rot.pitch >= -1.57079f && rot.pitch <= 1.57079f;  // ±π/2
}

bool InputValidator::isValidInputSequence(uint32_t sequence, uint32_t lastSequence) {
    // Allow for some packet reordering (within 100 packets)
    // Also handle wraparound
    if (sequence > lastSequence) {
        return (sequence - lastSequence) < 1000;  // Normal progression
    } else if (sequence < lastSequence) {
        // Possible wraparound or very old packet
        return (lastSequence - sequence) > 0xFFFFFFFF - 1000;  // Wraparound
    }
    return false;  // Duplicate
}

std::string InputValidator::sanitizeString(const std::string& input, 
                                          size_t maxLength) {
    std::string result;
    result.reserve(std::min(input.size(), maxLength));
    
    for (char c : input) {
        // Allow printable ASCII only
        if (c >= 32 && c < 127 && result.size() < maxLength) {
            result += c;
        }
    }
    
    return result;
}

bool InputValidator::isValidPacketSize(size_t size) {
    return size > 0 && size <= MAX_PACKET_SIZE;
}

bool InputValidator::isValidProtocolVersion(uint32_t version) {
    return version == Constants::PROTOCOL_VERSION;
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

// ============================================================================
// TrafficAnalyzer Implementation
// ============================================================================

TrafficAnalyzer::TrafficAnalyzer(const Config& config)
    : config_(config) {}

TrafficAnalyzer::TrafficAnalyzer()
    : config_(Config{}) {}

void TrafficAnalyzer::recordConnection(const std::string& ipAddress, 
                                       uint32_t currentTimeMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we need to start a new window
    if (currentTimeMs - currentWindow_.startTime > 1000) {
        // Archive current window
        if (currentWindow_.startTime > 0) {
            TrafficStats stats;
            stats.connectionsPerSecond = currentWindow_.connectionCount;
            stats.packetsPerSecond = currentWindow_.packetCount;
            stats.bytesPerSecond = currentWindow_.byteCount;
            stats.uniqueIPs = static_cast<uint32_t>(currentWindow_.uniqueIPs.size());
            
            history_.push_back({currentWindow_.startTime, stats});
            
            // Keep only last minute of history
            while (history_.size() > 60) {
                history_.pop_front();
            }
        }
        
        // Start new window
        currentWindow_ = TimeWindow{};
        currentWindow_.startTime = currentTimeMs;
    }
    
    ++currentWindow_.connectionCount;
    currentWindow_.uniqueIPs.insert(ipAddress);
}

void TrafficAnalyzer::recordPacket(uint32_t size, uint32_t currentTimeMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we need to start a new window
    if (currentTimeMs - currentWindow_.startTime > 1000) {
        if (currentWindow_.startTime > 0) {
            TrafficStats stats;
            stats.connectionsPerSecond = currentWindow_.connectionCount;
            stats.packetsPerSecond = currentWindow_.packetCount;
            stats.bytesPerSecond = currentWindow_.byteCount;
            stats.uniqueIPs = static_cast<uint32_t>(currentWindow_.uniqueIPs.size());
            
            history_.push_back({currentWindow_.startTime, stats});
            
            while (history_.size() > 60) {
                history_.pop_front();
            }
        }
        
        currentWindow_ = TimeWindow{};
        currentWindow_.startTime = currentTimeMs;
    }
    
    ++currentWindow_.packetCount;
    currentWindow_.byteCount += size;
}

bool TrafficAnalyzer::detectAttack(uint32_t currentTimeMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (history_.size() < config_.minBaselineSamples / 60) {
        return false;  // Not enough data
    }
    
    // Calculate baseline (average of history)
    uint64_t totalConnections = 0;
    uint64_t totalPackets = 0;
    
    for (const auto& [timestamp, stats] : history_) {
        totalConnections += stats.connectionsPerSecond;
        totalPackets += stats.packetsPerSecond;
    }
    
    uint32_t avgConnections = static_cast<uint32_t>(totalConnections / history_.size());
    uint32_t avgPackets = static_cast<uint32_t>(totalPackets / history_.size());
    
    // Check current window
    uint32_t connectionThreshold = avgConnections * config_.spikeThresholdPercent / 100;
    uint32_t packetThreshold = avgPackets * config_.spikeThresholdPercent / 100;
    
    if (connectionThreshold > 0 && currentWindow_.connectionCount > connectionThreshold) {
        return true;
    }
    
    if (packetThreshold > 0 && currentWindow_.packetCount > packetThreshold) {
        return true;
    }
    
    return false;
}

TrafficAnalyzer::TrafficStats TrafficAnalyzer::getCurrentStats(uint32_t currentTimeMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    TrafficStats stats;
    stats.connectionsPerSecond = currentWindow_.connectionCount;
    stats.packetsPerSecond = currentWindow_.packetCount;
    stats.bytesPerSecond = currentWindow_.byteCount;
    stats.uniqueIPs = static_cast<uint32_t>(currentWindow_.uniqueIPs.size());
    
    return stats;
}

void TrafficAnalyzer::update(uint32_t currentTimeMs) {
    // Archive current window if needed
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (currentTimeMs - currentWindow_.startTime > 1000) {
        if (currentWindow_.startTime > 0) {
            TrafficStats stats;
            stats.connectionsPerSecond = currentWindow_.connectionCount;
            stats.packetsPerSecond = currentWindow_.packetCount;
            stats.bytesPerSecond = currentWindow_.byteCount;
            stats.uniqueIPs = static_cast<uint32_t>(currentWindow_.uniqueIPs.size());
            
            history_.push_back({currentWindow_.startTime, stats});
            
            while (history_.size() > 60) {
                history_.pop_front();
            }
        }
        
        currentWindow_ = TimeWindow{};
        currentWindow_.startTime = currentTimeMs;
    }
}

} // namespace Security
} // namespace DarkAges
