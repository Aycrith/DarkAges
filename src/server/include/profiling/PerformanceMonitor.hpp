#pragma once
#include "profiling/PerfettoProfiler.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <cstdint>
#include <cstddef>

namespace DarkAges {
namespace Profiling {

// [PERFORMANCE_AGENT] Real-time performance monitoring
// Runs in a background thread to track and report performance metrics
class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor();
    
    // Start monitoring in background thread
    void start(uint32_t reportIntervalMs = 60000);  // 1 minute default
    
    // Stop monitoring
    void stop();
    
    // Record a tick time sample
    void recordTickTime(uint64_t tickTimeUs);
    
    // Check if performance is within budget
    bool isPerformanceAcceptable() const;
    
    // Get current FPS (simulation rate)
    float getCurrentFPS() const;
    
    // Get 99th percentile tick time
    uint64_t getP99TickTimeUs() const;
    
    // Get 95th percentile tick time
    uint64_t getP95TickTimeUs() const;
    
    // Get average tick time
    double getAverageTickTimeUs() const;
    
    // Get current tick rate (actual measured)
    uint32_t getMeasuredTickRate() const { return measuredTickRate_.load(); }
    
    // Get violation count (ticks exceeding budget)
    uint64_t getViolationCount() const { return violationCount_.load(); }
    
    // Reset statistics
    void resetStats();

private:
    std::atomic<bool> running_{false};
    std::thread monitorThread_;
    uint32_t reportIntervalMs_{60000};
    
    // Circular buffer for tick times (last 1000 ticks)
    static constexpr size_t TICK_HISTORY_SIZE = 1000;
    std::vector<uint64_t> tickHistory_;
    size_t tickIndex_{0};
    mutable std::mutex historyMutex_;
    
    // Statistics
    std::atomic<uint64_t> totalTicks_{0};
    std::atomic<uint64_t> totalTickTimeUs_{0};
    std::atomic<uint64_t> violationCount_{0};
    std::atomic<uint32_t> measuredTickRate_{0};
    std::atomic<uint64_t> lastReportTime_{0};
    
    void monitorLoop();
    void generateReport();
    void checkBudgetViolations();
    uint64_t calculatePercentile(std::vector<uint64_t>& sortedData, double percentile) const;
};

} // namespace Profiling
} // namespace DarkAges
