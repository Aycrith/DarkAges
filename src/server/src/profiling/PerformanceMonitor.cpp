#include "profiling/PerformanceMonitor.hpp"
#include "Constants.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace DarkAges {
namespace Profiling {

PerformanceMonitor::PerformanceMonitor() 
    : tickHistory_(TICK_HISTORY_SIZE, 0) {
}

PerformanceMonitor::~PerformanceMonitor() {
    stop();
}

void PerformanceMonitor::start(uint32_t reportIntervalMs) {
    if (running_.exchange(true)) {
        return;  // Already running
    }
    
    reportIntervalMs_ = reportIntervalMs;
    lastReportTime_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
    );
    
    monitorThread_ = std::thread(&PerformanceMonitor::monitorLoop, this);
    
    std::cout << "[PERFORMANCE_MONITOR] Started (report interval: " 
              << reportIntervalMs_ << " ms)" << std::endl;
}

void PerformanceMonitor::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    
    std::cout << "[PERFORMANCE_MONITOR] Stopped" << std::endl;
}

void PerformanceMonitor::recordTickTime(uint64_t tickTimeUs) {
    // Store in circular buffer
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        tickHistory_[tickIndex_] = tickTimeUs;
        tickIndex_ = (tickIndex_ + 1) % TICK_HISTORY_SIZE;
    }
    
    // Update statistics
    totalTicks_++;
    totalTickTimeUs_ += tickTimeUs;
    
    // Check for budget violation
    if (tickTimeUs > Constants::TICK_BUDGET_US) {
        violationCount_++;
    }
}

bool PerformanceMonitor::isPerformanceAcceptable() const {
    // Acceptable if less than 1% of ticks overrun
    uint64_t total = totalTicks_.load();
    if (total == 0) return true;
    
    uint64_t violations = violationCount_.load();
    return (violations * 100 / total) < 1;
}

float PerformanceMonitor::getCurrentFPS() const {
    double avgTickUs = getAverageTickTimeUs();
    if (avgTickUs <= 0.0) return 0.0f;
    return static_cast<float>(1000000.0 / avgTickUs);
}

uint64_t PerformanceMonitor::getP99TickTimeUs() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    // Copy and sort for percentile calculation
    std::vector<uint64_t> sorted = tickHistory_;
    return calculatePercentile(sorted, 0.99);
}

uint64_t PerformanceMonitor::getP95TickTimeUs() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    // Copy and sort for percentile calculation
    std::vector<uint64_t> sorted = tickHistory_;
    return calculatePercentile(sorted, 0.95);
}

double PerformanceMonitor::getAverageTickTimeUs() const {
    uint64_t total = totalTicks_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(totalTickTimeUs_.load()) / static_cast<double>(total);
}

void PerformanceMonitor::resetStats() {
    totalTicks_ = 0;
    totalTickTimeUs_ = 0;
    violationCount_ = 0;
    
    std::lock_guard<std::mutex> lock(historyMutex_);
    std::fill(tickHistory_.begin(), tickHistory_.end(), 0);
    tickIndex_ = 0;
}

void PerformanceMonitor::monitorLoop() {
    while (running_) {
        // Sleep in small increments to allow quick shutdown
        for (int i = 0; i < 10 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!running_) break;
        
        auto now = std::chrono::steady_clock::now();
        auto nowMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count()
        );
        
        // Check if it's time to generate a report
        if (nowMs - lastReportTime_ >= reportIntervalMs_) {
            generateReport();
            lastReportTime_ = nowMs;
        }
        
        // Check budget violations and log warnings
        checkBudgetViolations();
    }
}

void PerformanceMonitor::generateReport() {
    uint64_t total = totalTicks_.load();
    if (total == 0) return;
    
    double avgMs = getAverageTickTimeUs() / 1000.0;
    uint64_t p99Us = getP99TickTimeUs();
    uint64_t p95Us = getP95TickTimeUs();
    uint64_t violations = violationCount_.load();
    float fps = getCurrentFPS();
    
    std::cout << "\n[PERFORMANCE_MONITOR] Report:\n";
    std::cout << "==============================\n";
    std::cout << "  Total Ticks: " << total << "\n";
    std::cout << "  Average Tick Time: " << avgMs << " ms\n";
    std::cout << "  P95 Tick Time: " << (p95Us / 1000.0) << " ms\n";
    std::cout << "  P99 Tick Time: " << (p99Us / 1000.0) << " ms\n";
    std::cout << "  Violations (> " << (Constants::TICK_BUDGET_US / 1000.0) << " ms): " 
              << violations << " (" << (violations * 100 / total) << "%)\n";
    std::cout << "  Effective FPS: " << fps << "\n";
    std::cout << "  Acceptable: " << (isPerformanceAcceptable() ? "YES" : "NO") << "\n";
    std::cout << std::endl;
}

void PerformanceMonitor::checkBudgetViolations() {
    // Check the most recent tick times for consecutive violations
    static uint32_t consecutiveViolations = 0;
    
    uint64_t recentTick;
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        size_t idx = (tickIndex_ + TICK_HISTORY_SIZE - 1) % TICK_HISTORY_SIZE;
        recentTick = tickHistory_[idx];
    }
    
    if (recentTick > Constants::TICK_BUDGET_US) {
        consecutiveViolations++;
        
        // Log warning if we see consecutive violations
        if (consecutiveViolations == 10) {
            std::cerr << "[PERFORMANCE_MONITOR] WARNING: 10 consecutive ticks exceeded budget! "
                      << "Recent: " << (recentTick / 1000.0) << " ms" << std::endl;
        }
    } else {
        consecutiveViolations = 0;
    }
}

uint64_t PerformanceMonitor::calculatePercentile(std::vector<uint64_t>& sortedData, double percentile) const {
    if (sortedData.empty()) return 0;
    
    // Sort the data
    std::sort(sortedData.begin(), sortedData.end());
    
    // Remove zeros (unfilled slots in circular buffer)
    auto it = std::lower_bound(sortedData.begin(), sortedData.end(), 1);
    if (it == sortedData.end()) return 0;
    
    size_t validCount = sortedData.end() - it;
    if (validCount == 0) return 0;
    
    // Calculate index for percentile
    size_t index = static_cast<size_t>(percentile * (validCount - 1));
    index = std::min(index, validCount - 1);
    
    return *(it + index);
}

} // namespace Profiling
} // namespace DarkAges
