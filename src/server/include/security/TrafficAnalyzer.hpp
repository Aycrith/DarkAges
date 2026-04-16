#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <cstdint>

namespace DarkAges {
namespace Security {

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
