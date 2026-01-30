#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <cstdint>

// Perfetto integration for performance tracing
// Uses Chrome's Perfetto format for visualization
// [PERFORMANCE_AGENT] Phase 5A - Performance Profiling

namespace DarkAges {
namespace Profiling {

// Trace categories
enum class TraceCategory : uint8_t {
    NETWORK = 0,      // Network I/O
    PHYSICS = 1,      // Physics/movement
    GAME_LOGIC = 2,   // Combat, spells, etc.
    REPLICATION = 3,  // Snapshot generation
    DATABASE = 4,     // Redis/Scylla operations
    MIGRATION = 5,    // Zone migrations
    TOTAL = 6         // Full tick
};

// Scoped trace event (RAII)
class ScopedTraceEvent {
public:
    ScopedTraceEvent(const char* name, TraceCategory category);
    ~ScopedTraceEvent();
    
    // Disable copy
    ScopedTraceEvent(const ScopedTraceEvent&) = delete;
    ScopedTraceEvent& operator=(const ScopedTraceEvent&) = delete;
    
    // Enable move
    ScopedTraceEvent(ScopedTraceEvent&&) noexcept;
    ScopedTraceEvent& operator=(ScopedTraceEvent&&) noexcept;

private:
    const char* name_{nullptr};
    TraceCategory category_{TraceCategory::TOTAL};
    std::chrono::high_resolution_clock::time_point start_;
    bool active_{false};
};

// Performance counter
struct PerformanceCounter {
    std::string name;
    uint64_t count{0};
    uint64_t totalTimeUs{0};
    uint64_t maxTimeUs{0};
    uint64_t minTimeUs{~0ULL};
    
    void record(uint64_t timeUs);
    double averageTimeUs() const;
    void reset();
};

// [PERFORMANCE_AGENT] Main profiler interface
class PerfettoProfiler {
public:
    static PerfettoProfiler& instance();
    
    // Initialize profiling
    bool initialize(const std::string& outputPath = "trace.perfetto");
    
    // Shutdown and flush traces
    void shutdown();
    
    // Begin a trace event
    void beginEvent(const char* name, TraceCategory category);
    
    // End a trace event
    void endEvent(const char* name, TraceCategory category);
    
    // Record instant event (marker)
    void instantEvent(const char* name, TraceCategory category);
    
    // Update counter value
    void counterEvent(const char* name, int64_t value);
    
    // Scoped event helper
    ScopedTraceEvent scopedEvent(const char* name, TraceCategory category);
    
    // Get performance stats
    const PerformanceCounter* getCounter(const std::string& name) const;
    std::vector<std::string> getCounterNames() const;
    
    // Reset all counters
    void resetCounters();
    
    // Check if profiling is active
    bool isActive() const { return active_; }
    
    // Enable/disable profiling at runtime
    void setActive(bool active);

private:
    PerfettoProfiler() = default;
    ~PerfettoProfiler() = default;
    
    PerfettoProfiler(const PerfettoProfiler&) = delete;
    PerfettoProfiler& operator=(const PerfettoProfiler&) = delete;

private:
    bool active_{false};
    std::string outputPath_;
    
    // Internal trace buffer
    struct TraceEvent {
        std::string name;
        TraceCategory category;
        uint64_t timestampUs;
        uint64_t durationUs;  // 0 for instant events
        bool isBegin;
        bool isInstant;
        
        TraceEvent() = default;
        TraceEvent(const std::string& n, TraceCategory c, uint64_t ts, uint64_t dur, bool begin, bool instant = false)
            : name(n), category(c), timestampUs(ts), durationUs(dur), isBegin(begin), isInstant(instant) {}
    };
    
    std::vector<TraceEvent> eventBuffer_;
    mutable std::mutex bufferMutex_;
    
    // Performance counters
    std::unordered_map<std::string, PerformanceCounter> counters_;
    mutable std::mutex counterMutex_;
    
    // Category colors for Chrome tracing
    static const char* getCategoryColor(TraceCategory cat);
    
    // Write trace to file
    void flushTrace();
    void writeTraceHeader(FILE* fp);
    void writeTraceEvent(FILE* fp, const TraceEvent& event, bool isLast);
    void writeTraceFooter(FILE* fp);
};

// Macros for easy instrumentation
#define TRACE_EVENT(name, category) \
    auto _trace_##__LINE__ = DarkAges::Profiling::PerfettoProfiler::instance().scopedEvent(name, category)

#define TRACE_BEGIN(name, category) \
    DarkAges::Profiling::PerfettoProfiler::instance().beginEvent(name, category)

#define TRACE_END(name, category) \
    DarkAges::Profiling::PerfettoProfiler::instance().endEvent(name, category)

#define TRACE_INSTANT(name, category) \
    DarkAges::Profiling::PerfettoProfiler::instance().instantEvent(name, category)

#define TRACE_COUNTER(name, value) \
    DarkAges::Profiling::PerfettoProfiler::instance().counterEvent(name, value)

} // namespace Profiling
} // namespace DarkAges
