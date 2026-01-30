# DarkAges MMO - Gamestate Test Orchestrator

## Overview

This is a comprehensive testing framework for validating client-server gamestate synchronization in the DarkAges MMO. It ensures that:

- **Client prediction** matches server authority
- **Entity interpolation** is smooth and accurate
- **Combat events** are synchronized correctly
- **Zone handoffs** are seamless
- **Gamestate** remains consistent under various network conditions

## Why This Matters

In an MMO, the server is the authority, but clients must predict movement for responsiveness. This creates potential for:

1. **Desynchronization** - Client and server disagree on state
2. **Rubber-banding** - Client snaps back after misprediction
3. **Hit detection issues** - Combat feels inconsistent
4. **Cheating** - Players exploit prediction gaps

This framework validates that these issues are handled correctly.

## Architecture

```
test-orchestrator/
├── TestOrchestrator.py      # Main orchestration engine
├── scenarios/               # Test scenarios
│   ├── MovementSyncScenario.py    # Client prediction test
│   ├── CombatSyncScenario.py      # Combat synchronization test
│   ├── ZoneHandoffScenario.py     # Zone transition test
│   └── MultiplayerScenario.py     # Multi-player interpolation test
├── validators/              # Gamestate validators
│   ├── PredictionValidator.py     # Client prediction accuracy
│   ├── InterpolationValidator.py  # Entity interpolation quality
│   └── CombatValidator.py         # Combat sync validation
└── reporters/               # Test result reporters
    ├── JSONReporter.py            # JSON output
    └── HTMLReporter.py            # HTML report
```

## Usage

### Basic Movement Sync Test

```bash
# Test movement synchronization with 50ms latency
python TestOrchestrator.py --scenario movement_sync --duration 60 --latency 50

# Test with network jitter and packet loss
python TestOrchestrator.py --scenario movement_sync --latency 100 --jitter 20 --packet-loss 0.5
```

### Combat Synchronization Test

```bash
# Test combat synchronization
python TestOrchestrator.py --scenario combat_sync --duration 120
```

### All Scenarios

```bash
# Run all test scenarios
python TestOrchestrator.py --all-scenarios --output test-results.json
```

### Continuous Testing

```bash
# Run continuously (for CI/CD)
python TestOrchestrator.py --all-scenarios --continuous
```

### C++ Integration Tests

```bash
# Build integration tests
cd build
cmake --build . --target TestGamestateSynchronization

# Run tests
./TestGamestateSynchronization "[gamestate]"
```

## Test Scenarios

### 1. MovementSyncScenario

Validates client prediction and server reconciliation:

1. Client predicts movement based on inputs
2. Server validates and sends corrections
3. Client reconciles with server state
4. Measures prediction error and reconciliation time

**Success Criteria:**
- P95 prediction error < 100ms
- P95 reconciliation time < 200ms
- No teleportation or rubber-banding

### 2. CombatSyncScenario

Validates combat event synchronization:

1. Client initiates attack
2. Server validates with lag compensation
3. Damage is applied
4. All clients see consistent result

**Success Criteria:**
- P95 combat delay < 150ms
- No missed hits that should connect
- No phantom hits

### 3. ZoneHandoffScenario

Validates seamless zone transitions:

1. Player approaches zone boundary
2. Aura projection shows adjacent zone
3. Player crosses boundary
4. Connection migrates to new zone server

**Success Criteria:**
- Zone handoff < 100ms
- No disconnection
- Entity state preserved

### 4. MultiplayerScenario

Validates multiplayer synchronization:

1. Multiple clients connect
2. Entity interpolation for remote players
3. Priority-based update rates
4. Bandwidth optimization

**Success Criteria:**
- Interpolation error < 0.5m
- Bandwidth < 20KB/s per player
- 10+ players visible smoothly

## Metrics

The framework collects these key metrics:

| Metric | Description | Target |
|--------|-------------|--------|
| Prediction Error | Time delta between client and server | < 100ms (P95) |
| Reconciliation Time | Time to correct prediction errors | < 200ms (P95) |
| Interpolation Error | Position error for remote entities | < 0.5m (P95) |
| Combat Delay | Time for combat events to sync | < 150ms (P95) |
| Zone Handoff | Time to migrate between zones | < 100ms |
| Tick Stability | Server tick rate consistency | 60Hz ± 2Hz |
| Bandwidth Usage | Network bandwidth per player | < 20KB/s |

## Validation Thresholds

Tests pass when all thresholds are met:

```python
TestConfig(
    max_prediction_error_ms=100.0,      # 100ms max prediction error
    max_reconciliation_time_ms=200.0,   # 200ms to reconcile
    max_interpolation_error_m=0.5,      # 0.5m position error
    max_combat_sync_ms=150.0,           # 150ms combat delay
    max_zone_handoff_ms=100.0,          # 100ms zone handoff
    min_tick_rate=58.0,                 # 58Hz minimum tick rate
)
```

## Continuous Integration

Add to your CI/CD pipeline:

```yaml
# .github/workflows/gamestate-tests.yml
name: Gamestate Synchronization Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Build Server
        run: ./build.sh --release
      
      - name: Run Gamestate Tests
        run: |
          python tools/test-orchestrator/TestOrchestrator.py \
            --all-scenarios \
            --duration 300 \
            --output test-results.json
      
      - name: Upload Results
        uses: actions/upload-artifact@v2
        with:
          name: test-results
          path: test-results.json
```

## Interpreting Results

### Pass

```
TEST REPORT: movement_sync
Result: PASS

METRICS:
  Prediction Error:     P95=45.2ms (max=67.1ms)  ✅
  Reconciliation Time:  P95=89.3ms (max=112.5ms) ✅
  Tick Rate:            Mean=60.0Hz (min=59.8Hz) ✅
```

### Fail

```
TEST REPORT: movement_sync
Result: FAIL

METRICS:
  Prediction Error:     P95=145.8ms (max=203.2ms) ❌
  
FAILURES:
  ❌ Prediction error P95 145.8ms exceeds threshold 100.0ms
  ❌ Reconciliation time P95 245.3ms exceeds threshold 200.0ms
```

## Debugging Failed Tests

1. **High Prediction Error**
   - Check client physics vs server physics match
   - Verify input processing timing
   - Review prediction algorithm

2. **Slow Reconciliation**
   - Check network latency
   - Verify snapshot send rate
   - Review reconciliation algorithm

3. **Tick Rate Issues**
   - Profile server code
   - Check for blocking operations
   - Review ECS update order

## Extending the Framework

### Add New Scenario

1. Create `scenarios/YourScenario.py`:

```python
class YourScenario:
    def __init__(self):
        self.name = "your_scenario"
    
    async def execute(self, config, report):
        # Implement test logic
        pass
```

2. Register in `TestOrchestrator.py`:

```python
from scenarios.YourScenario import YourScenario

scenarios = {
    "your_scenario": YourScenario,
    # ...
}
```

### Add New Validator

1. Create `validators/YourValidator.py`:

```python
class YourValidator:
    def validate(self, samples: List[GamestateSample]) -> Tuple[bool, List[float]]:
        # Implement validation
        return passed, errors
```

2. Use in scenario:

```python
from validators.YourValidator import YourValidator

validator = YourValidator()
passed, errors = validator.validate(samples)
```

## Related Documentation

- [Game Networking Architecture](../../docs/architecture/networking.md)
- [Client Prediction Guide](../../docs/client/prediction.md)
- [Lag Compensation](../../docs/server/lag-compensation.md)
- [Entity Interpolation](../../docs/client/interpolation.md)

## Contributing

When adding new tests:

1. Follow existing code style
2. Add test to `TestGamestateSynchronization.cpp`
3. Update this README
4. Ensure tests pass in CI

## License

Part of DarkAges MMO - See main project license
