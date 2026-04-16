#include "security/TrafficAnalyzer.hpp"
#include <algorithm>
#include <mutex>
#include <string>
#include <cstdint>

namespace DarkAges {
namespace Security {

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
