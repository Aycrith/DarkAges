# DarkAges MMO - Comprehensive Testing Strategy

## Executive Summary

This document outlines the complete testing strategy for the DarkAges MMO, addressing the critical requirement for **gamestate synchronization validation** between clients and servers.

**Core Principle:** *The project cannot be considered production-ready until gamestate synchronization has been validated through automated end-to-end testing with real services and clients.*

---

## Testing Architecture

### Three-Tier Testing Approach

```
┌─────────────────────────────────────────────────────────────────┐
│                    TIER 1: AUTOMATED QA                         │
│         (End-to-End with Real Services & Clients)                │
│                                                                  │
│  • AutomatedQA.py - Process orchestration                        │
│  • Real server processes spawned                                  │
│  • Real game clients launched                                     │
│  • Automated input injection                                      │
│  • Screenshot capture & vision analysis                           │
│  • Human-in-the-loop escalation                                   │
│                                                                  │
│  Usage: python AutomatedQA.py --scenario movement_sync          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                 TIER 2: GAMESTATE VALIDATION                     │
│              (Protocol & Synchronization Testing)                │
│                                                                  │
│  • TestOrchestrator.py - Network simulation                      │
│  • MovementSyncScenario - Prediction validation                  │
│  • CombatSyncScenario - Hit detection validation                 │
│  • C++ Integration Tests - Low-level validation                   │
│                                                                  │
│  Usage: python TestOrchestrator.py --all-scenarios              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   TIER 3: UNIT TESTING                          │
│              (Component-Level Validation)                        │
│                                                                  │
│  • 20 C++ test files (Catch2 framework)                          │
│  • Component isolation testing                                    │
│  • ECS system validation                                          │
│  • Network protocol testing                                       │
│                                                                  │
│  Usage: ctest or ./darkages_tests                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## Tier 1: Automated QA System (Real Execution)

### Purpose
Validate actual gamestate synchronization with real running services and clients. This is the **critical validation layer** that was missing.

### Components

#### 1. Process Orchestration (`AutomatedQA.py`)
- Spawns real zone server processes
- Launches actual Godot game clients
- Manages service lifecycle (health checks, graceful shutdown)
- Captures and analyzes all process output

#### 2. Input Injection (`InputInjector`)
- JSON-based input scripts
- Keyboard and mouse event injection
- Movement sequences (WASD)
- Combat sequences (attacks, abilities)

#### 3. Vision Analysis (`VisionAnalyzer`)
- Screenshot capture at key moments
- Visual anomaly detection
- UI element validation
- Screenshot archival for debugging

#### 4. Human-in-the-Loop (`HumanInterface`)
- **Emergency Escalation**: Critical errors trigger immediate human notification
- **Approval Gates**: Destructive actions require human approval
- **Context Requests**: AI asks for human insight when uncertain
- **Emergency Pause**: Tests can pause for investigation

### Test Scenarios

#### basic_connectivity
```bash
python AutomatedQA.py --scenario basic_connectivity --human-oversight
```
- Starts zone server
- Launches game client
- Waits for connection
- Human validates: "Did client connect successfully?"

#### movement_sync
```bash
python AutomatedQA.py --scenario movement_sync --screenshot-dir ./screenshots
```
- Tests client prediction vs server reconciliation
- Scripted movement sequence
- Screenshots captured during movement
- Validates: No teleportation, smooth movement

#### combat_sync
```bash
python AutomatedQA.py --scenario combat_sync --human-oversight --emergency-pause
```
- Two clients: attacker and target
- Scripted combat sequence
- Human validates: "Did hits register correctly?"

### Usage Modes

#### Development Mode (with human oversight)
```bash
python AutomatedQA.py \
    --scenario movement_sync \
    --human-oversight \
    --emergency-pause
```

#### CI/CD Mode (fully automated)
```bash
python AutomatedQA.py \
    --scenario regression_suite \
    --report junit.xml
```

---

## Tier 2: Gamestate Validation (Simulation)

### Purpose
Validate gamestate synchronization logic through simulation and metrics collection.

### Components

#### 1. TestOrchestrator
- Network condition simulation (latency, jitter, packet loss)
- Simulates client prediction vs server reconciliation
- Measures synchronization metrics

#### 2. MovementSyncScenario
- Simulates client-side prediction
- Simulates server validation and correction
- Measures prediction error and reconciliation time

#### 3. C++ Integration Tests
- `TestGamestateSynchronization.cpp`
- Tests movement synchronization
- Validates reconciliation
- Tests interpolation
- Validates tick stability

### Key Metrics

| Metric | Threshold | Purpose |
|--------|-----------|---------|
| Prediction Error | < 100ms (P95) | Client prediction accuracy |
| Reconciliation Time | < 200ms (P95) | Server correction efficiency |
| Interpolation Error | < 0.5m (P95) | Remote player smoothness |
| Combat Delay | < 150ms (P95) | Hit registration |
| Tick Rate | 60Hz ± 2Hz | Server stability |

---

## Tier 3: Unit Testing (Components)

### Purpose
Validate individual components in isolation.

### Test Files (20 total)

| Test File | Purpose |
|-----------|---------|
| TestMovementSystem.cpp | Physics and movement |
| TestCombatSystem.cpp | Damage and combat logic |
| TestLagCompensatedCombat.cpp | Lag compensation |
| TestSpatialHash.cpp | Spatial queries |
| TestAreaOfInterest.cpp | Entity visibility |
| TestEntityMigration.cpp | Zone handoffs |
| TestNetworkProtocol.cpp | Packet serialization |
| TestRedisIntegration.cpp | Database connectivity |
| TestScyllaManager.cpp | Persistence layer |
| TestGNSIntegration.cpp | Network layer |
| ...and 10 more | Various components |

### Execution
```bash
# Build tests
cmake --build . --target darkages_tests

# Run all tests
./darkages_tests

# Run specific test category
./darkages_tests "[gamestate]"

# Run with verbose output
./darkages_tests -s
```

---

## Human-in-the-Loop Strategy

### When Humans Are Involved

1. **Emergency Escalation**
   - Server crash (SEGFAULT, stack overflow)
   - Client crash or freeze
   - Critical error in logs
   - Screenshot shows corruption

2. **Approval Gates**
   - Destructive operations (database wipes)
   - Production environment access
   - Long-running tests (>1 hour)

3. **Context Requests**
   - Vision analysis ambiguous
   - Test results inconclusive
   - New failure pattern detected

4. **Validation Checkpoints**
   - "Did the character move smoothly?"
   - "Did combat feel responsive?"
   - "Are there visual glitches?"

### Human Interface Options

```
🚨 HUMAN ESCALATION REQUIRED

Time: 2026-01-30 16:45:23
Phase: EXECUTION
Source: client1
Level: EMERGENCY

Message: Client window frozen, no response to input

Screenshot: screenshots/screenshot_1706635523.png

Options:
  [c] Continue - Proceed with test
  [p] Pause - Pause for investigation
  [s] Skip - Skip this test
  [r] Retry - Retry current step
  [a] Abort - Abort entire test suite
  [f] Feedback - Provide insight
```

---

## Testing Workflow

### Pre-Deployment Validation Checklist

```bash
# Step 1: Project Validation
python tools/validate_project_state.py --report validation.json

# Step 2: Unit Tests
cd build && ctest --output-on-failure

# Step 3: Gamestate Simulation
python tools/test-orchestrator/TestOrchestrator.py \
    --all-scenarios \
    --output simulation_results.json

# Step 4: Automated QA (with human oversight)
python tools/automated-qa/AutomatedQA.py \
    --scenario basic_connectivity \
    --human-oversight

python tools/automated-qa/AutomatedQA.py \
    --scenario movement_sync \
    --human-oversight \
    --screenshot-dir ./movement_test

python tools/automated-qa/AutomatedQA.py \
    --scenario combat_sync \
    --human-oversight

# Step 5: Load Testing
python tools/stress-test/enhanced_bot_swarm.py \
    --bots 1000 \
    --duration 3600 \
    --output load_test_1hour.json

# Step 6: Chaos Testing
python tools/chaos/chaos_monkey.py \
    --duration 3600 \
    --namespace darkages

# Step 7: Generate Final Report
python tools/generate_test_report.py \
    --validation validation.json \
    --unit-tests unit_results.xml \
    --simulation simulation_results.json \
    --qa qa_results.json \
    --load-test load_test_1hour.json \
    --chaos chaos_results.json \
    --output final_report.html
```

### Success Criteria

| Test | Must Pass | Should Pass |
|------|-----------|-------------|
| Project Validation | 100% checks | 100% checks |
| Unit Tests | 100% pass | 100% pass |
| Simulation | P95 prediction < 100ms | P95 prediction < 50ms |
| Automated QA | No critical errors | Smooth human validation |
| Load Test | 1000 bots @ 60Hz | 1000 bots @ 60Hz, <50ms latency |
| Chaos Test | <30s recovery | <10s recovery |

---

## Testing Artifacts

### Generated During Testing

1. **Logs**
   - Server logs (per service)
   - Client logs (per client)
   - Orchestrator logs

2. **Screenshots**
   - Connection state
   - During movement
   - Combat sequences
   - Error states

3. **Metrics**
   - Prediction error samples
   - Reconciliation times
   - Tick rate stability
   - Bandwidth usage

4. **Reports**
   - JSON machine-readable
   - HTML human-readable
   - JUnit CI/CD compatible

### Artifact Retention

| Artifact Type | Retention | Purpose |
|---------------|-----------|---------|
| Failed test logs | 30 days | Debugging |
| Failed screenshots | 30 days | Visual analysis |
| All reports | 90 days | Trend analysis |
| Successful logs | 7 days | Storage efficiency |

---

## CI/CD Integration

### GitHub Actions Workflow

```yaml
name: Complete Test Suite

on: [push, pull_request]

jobs:
  test-matrix:
    strategy:
      matrix:
        test-tier: [unit, simulation, qa]
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Build
        run: ./build.ps1
      
      - name: Tier 1: Unit Tests
        if: matrix.test-tier == 'unit'
        run: |
          cd build
          ctest --output-on-failure
      
      - name: Tier 2: Simulation
        if: matrix.test-tier == 'simulation'
        run: |
          python tools/test-orchestrator/TestOrchestrator.py \
            --all-scenarios \
            --output simulation_results.json
      
      - name: Tier 3: Automated QA
        if: matrix.test-tier == 'qa'
        run: |
          python tools/automated-qa/AutomatedQA.py \
            --scenario regression_suite \
            --report qa_results.json
      
      - name: Upload Artifacts
        uses: actions/upload-artifact@v2
        with:
          name: test-results-${{ matrix.test-tier }}
          path: |
            *_results.json
            screenshots/
```

---

## Emergency Procedures

### Test Hangs
1. AutomatedQA detects no progress for 60 seconds
2. Captures screenshots of all clients
3. Escalates to human: "Test appears hung, screenshot attached"
4. Human decides: Continue, Kill, or Debug

### Server Crash
1. ProcessManager detects server exit
2. Captures final logs
3. Escalates to human: "Server crashed with error X"
4. Human decides: Retry, Skip, or Investigate

### Multiple Client Failures
1. If >50% clients fail, pause test
2. Collect all client logs and screenshots
3. Escalate to human with comprehensive context
4. Human decides: Abort or Continue with reduced clients

---

## Future Enhancements

### Short Term
1. **More Scenarios**: zone_transition, multi_client, stress_patterns
2. **Vision ML**: Train model to detect visual anomalies automatically
3. **Metrics Dashboard**: Real-time Grafana dashboard during tests

### Long Term
1. **Full Automation**: Reduce human oversight to true edge cases only
2. **Cloud Testing**: Run tests on cloud VMs with various network conditions
3. **Replay System**: Record and replay test sessions for debugging

---

## Summary

### What Exists Now

✅ **Tier 1: Automated QA**
- `AutomatedQA.py` - Full orchestration system
- Process spawning and management
- Input injection
- Screenshot capture
- Human escalation

✅ **Tier 2: Gamestate Validation**
- `TestOrchestrator.py` - Simulation framework
- Movement sync scenario
- Combat sync scenario
- C++ integration tests

✅ **Tier 3: Unit Testing**
- 20 C++ test files
- Catch2 framework
- Component isolation

### What Must Happen Before Deployment

🎯 **Execute Tier 1 tests with human oversight**

```bash
# This is the critical path
python tools/automated-qa/AutomatedQA.py \
    --scenario basic_connectivity \
    --human-oversight

python tools/automated-qa/AutomatedQA.py \
    --scenario movement_sync \
    --human-oversight

python tools/automated-qa/AutomatedQA.py \
    --scenario combat_sync \
    --human-oversight
```

**Without these tests passing with human validation, the project is NOT production-ready.**

---

**Document Version:** 1.0  
**Last Updated:** 2026-01-30  
**Status:** Ready for Execution
