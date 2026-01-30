# DarkAges Performance Profiling (Phase 5A)

This document describes the Perfetto-based performance profiling system integrated into the DarkAges MMO server.

## Overview

The profiling system provides detailed performance tracing of server ticks, enabling identification of bottlenecks in physics, networking, and game logic. It outputs traces compatible with Chrome's Perfetto UI.

## Architecture

### Components

1. **PerfettoProfiler** (`src/server/profiling/PerfettoProfiler.hpp/cpp`)
   - Singleton profiler for recording trace events
   - Outputs Chrome-compatible JSON traces
   - Zero-cost when disabled via compile-time flags

2. **PerformanceMonitor** (`src/server/profiling/PerformanceMonitor.hpp/cpp`)
   - Background thread for real-time performance monitoring
   - Tracks P95/P99 latencies, violation counts, FPS
   - Generates periodic performance reports

3. **ZoneServer Integration**
   - Instrumented tick loop with scoped events
   - Counter tracking for entities, tick times, player counts
   - Automatic profiler lifecycle management

## Usage

### Enabling Profiling

Profiling is enabled by default. To disable, set the CMake option:

```bash
cmake .. -DENABLE_PROFILING=OFF
```

### Running with Profiling

When profiling is enabled, trace files are automatically generated:
- `zone_{zoneId}_trace.json` - Per-zone trace file

### Viewing Traces

1. Open Chrome and navigate to `chrome://tracing` OR
2. Visit https://ui.perfetto.dev/ for the modern Perfetto UI
3. Load the generated JSON trace file

### Trace Categories

| Category | Color | Description |
|----------|-------|-------------|
| NETWORK | Green | Network I/O operations |
| PHYSICS | Yellow | Physics simulation and movement |
| GAME_LOGIC | Red | Combat, spells, abilities |
| REPLICATION | Blue | Snapshot generation and entity replication |
| DATABASE | Purple | Redis/ScyllaDB operations |
| MIGRATION | Orange | Zone handoffs and entity migration |
| TOTAL | Cyan | Full tick duration |

## Code Instrumentation

### Basic Scoped Events

```cpp
#include "profiling/PerfettoProfiler.hpp"

void MySystem::update() {
    TRACE_EVENT("MySystem::update", DarkAges::Profiling::TraceCategory::GAME_LOGIC);
    
    // Your code here...
}
```

### Manual Begin/End

```cpp
TRACE_BEGIN("HeavyOperation", TraceCategory::PHYSICS);
// ... heavy work ...
TRACE_END("HeavyOperation", TraceCategory::PHYSICS);
```

### Instant Markers

```cpp
TRACE_INSTANT("CriticalEvent", TraceCategory::GAME_LOGIC);
```

### Counters

```cpp
TRACE_COUNTER("entity_count", static_cast<int64_t>(registry_.alive()));
TRACE_COUNTER("tick_time_us", tickTimeUs);
```

## Performance Budgets

The system monitors against these budgets:

| Metric | Budget | Action on Violation |
|--------|--------|---------------------|
| Tick Time | 16.666ms (60Hz) | Warning logged |
| Consecutive Violations | 10 ticks | QoS degradation activated |
| Severe Overrun | 20ms | Automatic snapshot rate reduction |

## Output Format

Traces are output in Chrome Trace Event Format:

```json
{
  "displayTimeUnit": "ms",
  "traceEvents": [
    {"name": "ZoneServer::tick", "cat": "total", "ph": "B", "ts": 1234567, "pid": 1, "tid": 7},
    {"name": "updateNetwork", "cat": "network", "ph": "B", "ts": 1234568, "pid": 1, "tid": 1},
    {"name": "updateNetwork", "cat": "network", "ph": "E", "ts": 1234570, "pid": 1, "tid": 1},
    ...
    {"name": "ZoneServer::tick", "cat": "total", "ph": "E", "ts": 1234583, "pid": 1, "tid": 7}
  ]
}
```

## Performance Impact

- **Disabled**: Zero overhead (macros expand to nothing)
- **Enabled, inactive**: Minimal (< 0.1% overhead)
- **Enabled, active**: ~1-2% overhead depending on event density

## Best Practices

1. **Use scoped events (TRACE_EVENT)** for automatic RAII-based cleanup
2. **Keep event names short** to minimize memory usage
3. **Avoid tracing in hot inner loops** - trace at system boundaries
4. **Use categories appropriately** for filtering in the UI
5. **Reset counters periodically** for long-running servers

## Troubleshooting

### Large Trace Files
- Events are buffered in memory and flushed on shutdown
- For long sessions, consider periodic flushing or reduced instrumentation

### Missing Events
- Verify `ENABLE_PROFILING` is defined at compile time
- Check that `PerfettoProfiler::initialize()` was called

### Performance Impact Too High
- Reduce instrumentation density in hot paths
- Use conditional tracing: `if (shouldTrace) { TRACE_EVENT(...) }`

## Future Enhancements

- [ ] Native Perfetto SDK integration for lower overhead
- [ ] Real-time streaming to remote collector
- [ ] Automated performance regression detection
- [ ] Integration with Grafana/Prometheus for metrics
- [ ] Flame graph generation for call stacks
