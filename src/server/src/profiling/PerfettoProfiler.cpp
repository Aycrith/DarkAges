#include "profiling/PerfettoProfiler.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace DarkAges {
namespace Profiling {

// ============================================================================
// PerformanceCounter
// ============================================================================

void PerformanceCounter::record(uint64_t timeUs) {
    count++;
    totalTimeUs += timeUs;
    maxTimeUs = std::max(maxTimeUs, timeUs);
    minTimeUs = std::min(minTimeUs, timeUs);
}

double PerformanceCounter::averageTimeUs() const {
    if (count == 0) return 0.0;
    return static_cast<double>(totalTimeUs) / static_cast<double>(count);
}

void PerformanceCounter::reset() {
    count = 0;
    totalTimeUs = 0;
    maxTimeUs = 0;
    minTimeUs = ~0ULL;
}

// ============================================================================
// ScopedTraceEvent
// ============================================================================

ScopedTraceEvent::ScopedTraceEvent(const char* name, TraceCategory category)
    : name_(name), category_(category), active_(true) {
    start_ = std::chrono::high_resolution_clock::now();
    PerfettoProfiler::instance().beginEvent(name, category);
}

ScopedTraceEvent::~ScopedTraceEvent() {
    if (active_) {
        PerfettoProfiler::instance().endEvent(name_, category_);
    }
}

ScopedTraceEvent::ScopedTraceEvent(ScopedTraceEvent&& other) noexcept
    : name_(other.name_)
    , category_(other.category_)
    , start_(other.start_)
    , active_(other.active_) {
    other.active_ = false;
}

ScopedTraceEvent& ScopedTraceEvent::operator=(ScopedTraceEvent&& other) noexcept {
    if (this != &other) {
        if (active_) {
            PerfettoProfiler::instance().endEvent(name_, category_);
        }
        name_ = other.name_;
        category_ = other.category_;
        start_ = other.start_;
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

// ============================================================================
// PerfettoProfiler
// ============================================================================

PerfettoProfiler& PerfettoProfiler::instance() {
    static PerfettoProfiler instance;
    return instance;
}

bool PerfettoProfiler::initialize(const std::string& outputPath) {
    outputPath_ = outputPath;
    active_ = true;
    
    // Reserve buffer space to minimize allocations during tracing
    eventBuffer_.reserve(100000);
    
    std::cout << "[PROFILER] Initialized, output: " << outputPath_ << std::endl;
    return true;
}

void PerfettoProfiler::shutdown() {
    if (!active_) return;
    
    flushTrace();
    active_ = false;
    
    // Print summary
    std::cout << "\n[PROFILER] Performance Summary:\n";
    std::cout << "===============================\n";
    
    std::lock_guard<std::mutex> lock(counterMutex_);
    for (const auto& [name, counter] : counters_) {
        std::cout << name << ":\n";
        std::cout << "  Count: " << counter.count << "\n";
        std::cout << "  Avg: " << counter.averageTimeUs() / 1000.0 << " ms\n";
        std::cout << "  Min: " << counter.minTimeUs / 1000.0 << " ms\n";
        std::cout << "  Max: " << counter.maxTimeUs / 1000.0 << " ms\n";
    }
    
    std::cout << "\nTrace written to: " << outputPath_ << std::endl;
}

void PerfettoProfiler::beginEvent(const char* name, TraceCategory category) {
    if (!active_) return;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    eventBuffer_.emplace_back(name, category, static_cast<uint64_t>(timestampUs), 0, true, false);
}

void PerfettoProfiler::endEvent(const char* name, TraceCategory category) {
    if (!active_) return;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    eventBuffer_.emplace_back(name, category, static_cast<uint64_t>(timestampUs), 0, false, false);
}

void PerfettoProfiler::instantEvent(const char* name, TraceCategory category) {
    if (!active_) return;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    eventBuffer_.emplace_back(name, category, static_cast<uint64_t>(timestampUs), 1, false, true);
}

void PerfettoProfiler::counterEvent(const char* name, int64_t value) {
    if (!active_) return;
    
    // Track in counters for summary statistics
    std::lock_guard<std::mutex> lock(counterMutex_);
    auto& counter = counters_[name];
    counter.name = name;
    
    // Record the value as a "time" for counter tracking
    counter.record(static_cast<uint64_t>(value));
}

ScopedTraceEvent PerfettoProfiler::scopedEvent(const char* name, TraceCategory category) {
    return ScopedTraceEvent(name, category);
}

const PerformanceCounter* PerfettoProfiler::getCounter(const std::string& name) const {
    std::lock_guard<std::mutex> lock(counterMutex_);
    auto it = counters_.find(name);
    if (it != counters_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> PerfettoProfiler::getCounterNames() const {
    std::lock_guard<std::mutex> lock(counterMutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : counters_) {
        names.push_back(name);
    }
    return names;
}

void PerfettoProfiler::resetCounters() {
    std::lock_guard<std::mutex> lock(counterMutex_);
    for (auto& [name, counter] : counters_) {
        counter.reset();
    }
}

void PerfettoProfiler::setActive(bool active) {
    active_ = active;
}

const char* PerfettoProfiler::getCategoryColor(TraceCategory cat) {
    switch (cat) {
        case TraceCategory::NETWORK: return "thread_state_running";      // Green
        case TraceCategory::PHYSICS: return "thread_state_runnable";     // Yellow
        case TraceCategory::GAME_LOGIC: return "thread_state_unknown";   // Red
        case TraceCategory::REPLICATION: return "thread_state_sleeping"; // Blue
        case TraceCategory::DATABASE: return "thread_state_blocked";     // Purple
        case TraceCategory::MIGRATION: return "heap_dump_stack_frame";   // Orange
        case TraceCategory::TOTAL: return "thread_state_io";             // Cyan
        default: return "generic_work";
    }
}

// ============================================================================
// Trace File Output (JSON format compatible with Chrome tracing / Perfetto UI)
// ============================================================================

void PerfettoProfiler::flushTrace() {
    FILE* fp = nullptr;
    errno_t err = fopen_s(&fp, outputPath_.c_str(), "w");
    if (err != 0 || !fp) {
        std::cerr << "[PROFILER] Failed to open output file: " << outputPath_ << std::endl;
        return;
    }
    
    writeTraceHeader(fp);
    
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        size_t count = eventBuffer_.size();
        for (size_t i = 0; i < count; ++i) {
            writeTraceEvent(fp, eventBuffer_[i], i == count - 1);
        }
    }
    
    writeTraceFooter(fp);
    fclose(fp);
}

void PerfettoProfiler::writeTraceHeader(FILE* fp) {
    std::fprintf(fp, "{\n");
    std::fprintf(fp, "  \"displayTimeUnit\": \"ms\",\n");
    std::fprintf(fp, "  \"systemTraceEvents\": \"SystemTraceData\",\n");
    std::fprintf(fp, "  \"traceEvents\": [\n");
}

void PerfettoProfiler::writeTraceEvent(FILE* fp, const TraceEvent& event, bool isLast) {
    const char* phase;
    if (event.isInstant) {
        phase = "i";  // Instant event
    } else if (event.isBegin) {
        phase = "B";  // Begin
    } else {
        phase = "E";  // End
    }
    
    const char* categoryStr = "unknown";
    int tid = 1;
    switch (event.category) {
        case TraceCategory::NETWORK: categoryStr = "network"; tid = 1; break;
        case TraceCategory::PHYSICS: categoryStr = "physics"; tid = 2; break;
        case TraceCategory::GAME_LOGIC: categoryStr = "game_logic"; tid = 3; break;
        case TraceCategory::REPLICATION: categoryStr = "replication"; tid = 4; break;
        case TraceCategory::DATABASE: categoryStr = "database"; tid = 5; break;
        case TraceCategory::MIGRATION: categoryStr = "migration"; tid = 6; break;
        case TraceCategory::TOTAL: categoryStr = "total"; tid = 7; break;
    }
    
    // Use scope 't' for instant events (top-level), 'g' for begin/end
    const char* scope = event.isInstant ? "\"s\": \"t\", " : "";
    
    std::fprintf(fp, "    {\"name\": \"%s\", \"cat\": \"%s\", \"ph\": \"%s\", %s\"ts\": %llu, \"pid\": 1, \"tid\": %d}",
                   event.name.c_str(), categoryStr, phase, scope,
                   static_cast<unsigned long long>(event.timestampUs),
                   tid);
    
    if (!isLast) {
        std::fprintf(fp, ",");
    }
    std::fprintf(fp, "\n");
}

void PerfettoProfiler::writeTraceFooter(FILE* fp) {
    std::fprintf(fp, "  ]\n");
    std::fprintf(fp, "}\n");
}

} // namespace Profiling
} // namespace DarkAges
