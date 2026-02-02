// Stub implementation for PerfettoProfiler
#include "profiling/PerfettoProfiler.hpp"

namespace DarkAges::Profiling {

PerfettoProfiler& PerfettoProfiler::instance() {
    static PerfettoProfiler inst;
    return inst;
}

bool PerfettoProfiler::initialize(const std::string&) { 
    active_ = true;
    return true; 
}

void PerfettoProfiler::shutdown() { 
    active_ = false; 
}
void PerfettoProfiler::beginEvent(const char*, TraceCategory) {}
void PerfettoProfiler::endEvent(const char*, TraceCategory) {}
void PerfettoProfiler::instantEvent(const char*, TraceCategory) {}
void PerfettoProfiler::counterEvent(const char*, int64_t) {}
ScopedTraceEvent PerfettoProfiler::scopedEvent(const char* name, TraceCategory category) { return ScopedTraceEvent(name, category); }

// Note: PerformanceCounter methods are now inline in the header

// ScopedTraceEvent methods
ScopedTraceEvent::ScopedTraceEvent(const char* name, TraceCategory category) 
    : name_(name), category_(category) {}
ScopedTraceEvent::~ScopedTraceEvent() {}

} // namespace DarkAges::Profiling
