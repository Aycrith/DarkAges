// Stub implementation for PerfettoProfiler
#include "profiling/PerfettoProfiler.hpp"

namespace DarkAges::Profiling {

PerfettoProfiler& PerfettoProfiler::instance() {
    static PerfettoProfiler inst;
    return inst;
}

bool PerfettoProfiler::initialize(const std::string&) { return true; }
void PerfettoProfiler::shutdown() {}
void PerfettoProfiler::beginEvent(const char*, TraceCategory) {}
void PerfettoProfiler::endEvent(const char*, TraceCategory) {}
void PerfettoProfiler::instantEvent(const char*, TraceCategory) {}
void PerfettoProfiler::counterEvent(const char*, int64_t) {}
ScopedTraceEvent PerfettoProfiler::scopedEvent(const char* name, TraceCategory category) { return ScopedTraceEvent(name, category); }

// PerformanceCounter methods
void PerformanceCounter::record(uint64_t) {}
double PerformanceCounter::averageTimeUs() const { return 0.0; }
void PerformanceCounter::reset() {}

// ScopedTraceEvent methods
ScopedTraceEvent::ScopedTraceEvent(const char* name, TraceCategory category) 
    : name_(name), category_(category) {}
ScopedTraceEvent::~ScopedTraceEvent() {}

} // namespace DarkAges::Profiling
