# DarkAges MMO - Testing Infrastructure

This directory contains the comprehensive testing infrastructure for DarkAges MMO, implementing a three-tier testing strategy.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         TEST ORCHESTRATION LAYER                             │
│                          (TestRunner.py)                                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
           ┌────────────────────────┼────────────────────────┐
           │                        │                        │
           ▼                        ▼                        ▼
┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐
│   TIER 1: UNIT   │    │ TIER 2: SIMULATE │    │  TIER 3: REAL    │
│   (C++/Catch2)   │    │   (Python)       │    │  (Godot MCP)     │
└──────────────────┘    └──────────────────┘    └──────────────────┘
         │                       │                       │
         └───────────────────────┼───────────────────────┘
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     TELEMETRY & ANALYTICS LAYER                              │
│           (MetricsCollector.py, TestDashboard.py)                            │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Quick Start

### Run All Tests

```bash
# Run all three tiers
python tools/testing/TestRunner.py --tier=all

# Run with real Godot MCP
python tools/testing/TestRunner.py --tier=all --mcp
```

### Run Specific Tier

```bash
# Unit tests only
python tools/testing/TestRunner.py --tier=unit

# Simulation tests only
python tools/testing/TestRunner.py --tier=simulation

# Real execution tests
python tools/testing/TestRunner.py --tier=real --mcp
```

### Start Test Dashboard

```bash
# Web dashboard
python tools/testing/TestDashboard.py --port 8080

# Console dashboard
python tools/testing/TestDashboard.py --console
```

---

## Directory Structure

```
tools/testing/
├── README.md                      # This file
├── TestRunner.py                  # Master test orchestrator
├── TestDashboard.py               # Web/console dashboard
├── telemetry/
│   ├── __init__.py
│   └── MetricsCollector.py        # Metrics collection system
├── scenarios/
│   ├── __init__.py
│   ├── MovementTestScenarios.py   # Movement sync tests
│   ├── CombatTestScenarios.py     # Combat system tests
│   └── StressTestScenarios.py     # Load/stress tests
└── reports/                       # Test output (generated)
    └── *.json                     # Test result files
```

---

## Testing Tiers

### Tier 1: Unit Tests (C++ / Catch2)

**Purpose:** Fast, deterministic testing of isolated components

**Execution:**
```bash
# Via TestRunner
python tools/testing/TestRunner.py --tier=unit

# Direct (if server built with tests)
./build/Release/darkages_server.exe --test
```

**Scope:**
- ECS component operations
- Physics calculations
- Serialization (FlatBuffers)
- Math utilities

**Performance:**
- Target: < 1ms per test
- Coverage: > 80%

---

### Tier 2: Simulation Tests (Python)

**Purpose:** Test game logic without real processes

**Execution:**
```bash
# Via TestRunner
python tools/testing/TestRunner.py --tier=simulation

# Direct
python tools/testing/scenarios/MovementTestScenarios.py
```

**Scope:**
- Client prediction algorithms
- Server reconciliation
- Network latency effects
- Combat logic

**Scenarios:**
- `basic_movement` - Simple movement sync
- `high_latency` - Poor network conditions
- `rapid_direction_changes` - Input stress
- `packet_loss` - Recovery testing

**Performance:**
- Target: < 5s per scenario
- Deterministic: Yes (seeded)

---

### Tier 3: Real Execution (Godot MCP)

**Purpose:** Full E2E testing with actual Godot client

**Execution:**
```bash
# Via TestRunner (with MCP)
python tools/testing/TestRunner.py --tier=real --mcp

# Direct MCP test
python tools/automated-qa/godot-mcp/test_movement_sync_mcp.py
```

**Requirements:**
- Godot 4.2+ installed
- DarkAges client project built
- MCP server configured

**Scope:**
- Real network stack
- Actual rendering
- Visual validation
- Input handling

**Performance:**
- Target: < 60s per test
- Resource intensive

---

## Metrics Collection

### Basic Usage

```python
from telemetry.MetricsCollector import MetricsCollector

with MetricsCollector("my_test") as collector:
    # Record metrics
    collector.record_timing("operation.time", 15.5)
    collector.record_gauge("memory.usage", 1024.5)
    collector.increment_counter("events.processed")
    
    # Log events
    collector.log_info("test_started", {"config": "value"})
    collector.log_error("operation_failed", {"reason": "timeout"})
    
    # Get statistics
    stats = collector.get_statistics("timing.operation.time")
    print(f"Mean: {stats['mean']}, P95: {stats['p95']}")

# Report auto-saved to tools/testing/reports/
```

### Specialized Collectors

```python
from telemetry.MetricsCollector import (
    ServerMetricsCollector,
    ClientMetricsCollector,
    NetworkMetricsCollector
)

# Server metrics
with ServerMetricsCollector("server_session") as collector:
    collector.record_tick(tick_time_ms=16.5, entity_count=100)
    collector.record_network_io(1024, 2048, 10, 20)
    collector.record_player_event("joined", "player_001")

# Client metrics (via MCP)
with ClientMetricsCollector("client_session") as collector:
    collector.record_frame(frame_time_ms=16.0, fps=60)
    collector.record_prediction_error(error_ms=10.5, position_delta=0.1)
    collector.record_screenshot_analysis(similarity=0.95, path="shot.png")

# Network metrics
with NetworkMetricsCollector("network_session") as collector:
    collector.record_latency(rtt_ms=50, jitter_ms=5)
    collector.record_packet_loss(loss_percent=0.1)
    collector.record_bandwidth(15.5, 1.2)
```

---

## Test Scenarios

### Creating New Scenarios

```python
# tools/testing/scenarios/MyScenarios.py

import asyncio
from telemetry.MetricsCollector import MetricsCollector
from scenarios.MovementTestScenarios import MovementTestResult

async def scenario_my_feature() -> MovementTestResult:
    scenario_name = "my_feature"
    errors = []
    
    with MetricsCollector(scenario_name) as collector:
        start_time = time.time()
        
        # Test implementation
        collector.log_info("test_start", {})
        
        # ... your test code ...
        
        # Validate
        passed = len(errors) == 0
        
        duration_ms = (time.time() - start_time) * 1000
        
        return MovementTestResult(
            scenario_name=scenario_name,
            passed=passed,
            duration_ms=duration_ms,
            metrics=collector.generate_report(),
            errors=errors,
            screenshots=[],
            details={}
        )
```

---

## CI/CD Integration

### GitHub Actions

The repository includes `.github/workflows/testing.yml` that runs:

1. **Unit Tests** - On every push/PR
2. **Simulation Tests** - On every push/PR
3. **Real Execution Tests** - On PRs and main branch

### Local CI Simulation

```bash
# Run full CI pipeline locally
python tools/testing/TestRunner.py --tier=all --mcp

# Generate dashboard view
python tools/testing/TestDashboard.py --console
```

---

## Performance Budgets

| Metric | Target | Critical |
|--------|--------|----------|
| Server Tick | 16.67ms | 20ms |
| Client FPS | 60 | 30 |
| Input Latency | < 50ms | 100ms |
| Memory Growth | < 10MB/hr | 50MB/hr |
| Network Down | < 20KB/s | 50KB/s |

---

## Debugging Failed Tests

### View Test Reports

```bash
# List all reports
ls -la tools/testing/reports/

# View specific report
cat tools/testing/reports/test_run_*.json | python -m json.tool
```

### Enable Debug Logging

```python
# In your test
import logging
logging.basicConfig(level=logging.DEBUG)

# Or via environment
set DEBUG=true
python TestRunner.py --tier=all
```

### Screenshot Analysis

Failed real execution tests save screenshots to:
```
tools/automated-qa/screenshots/
```

---

## Best Practices

### Test Design
1. **Deterministic** - Same input = same output
2. **Isolated** - Tests don't depend on each other
3. **Fast** - Unit tests < 1ms, simulations < 5s
4. **Readable** - Clear failure messages
5. **Metrics** - Always collect performance data

### Test Data
- Use fixtures in `tools/testing/fixtures/`
- Generate synthetic data deterministically
- Don't rely on external services

### Metrics
- Always use `MetricsCollector`
- Record timing for operations
- Log significant events
- Tag metrics with context

---

## Troubleshooting

### "Godot not found"
```bash
set GODOT_PATH=C:\Dev\DarkAges\tools\godot\Godot_v4.2.2-stable_mono_win64\Godot_v4.2.2-stable_mono_win64.exe
```

### "Server binary not found"
```bash
# Build server first
./build.ps1
# Or manually
cmake -B build -S . -A x64
cmake --build build --config Release
```

### "MCP connection failed"
Ensure MCP server is configured in your AI assistant settings.

---

## Resources

- `COMPREHENSIVE_TESTING_PLAN.md` - Full testing strategy
- `MCP_INTEGRATION_COMPLETE.md` - Godot MCP documentation
- `TESTING_STRATEGY.md` - Original testing strategy

---

## Contributing

When adding new tests:
1. Follow the three-tier model
2. Add to appropriate scenario file
3. Update this README
4. Ensure CI passes
