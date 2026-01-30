# DarkAges MMO - Testing & Validation Plan

**Document Version**: 1.0  
**Date**: 2026-01-29  
**Status**: Implementation Complete, Testing Pending  

---

## Executive Summary

The DarkAges MMO server implementation (Phases 0-5) is **code-complete but untested**. This document establishes the testing strategy, validation criteria, and next-phase roadmap to achieve production readiness.

### Current State Assessment

| Phase | Code Status | Test Status | Risk Level |
|-------|-------------|-------------|------------|
| Phase 0: Foundation | Complete | Unit tests only | Medium |
| Phase 1: Networking | Complete | Interface stubs only | **High** |
| Phase 2: Multi-Player | Complete | Untested | **High** |
| Phase 3: Combat | Complete | Untested | **High** |
| Phase 4: Sharding | Complete | Untested | **High** |
| Phase 5: Optimization | Complete | Untested | Medium |

**Critical Gap**: No integration testing has been performed. The system has not been compiled as a whole.

---

## Phase 6: Build System & Compilation (Week 17)

### Goal
Achieve clean compilation across all platforms with zero warnings.

### Deliverables

#### 6A: CMake Build System Fixes
```cmake
# Current issues to resolve:
- [ ] Fix EnTT include paths
- [ ] Link GameNetworkingSockets correctly
- [ ] Add FlatBuffers code generation step
- [ ] Platform-specific compiler flags (MSVC/GCC/Clang)
- [ ] AddressSanitizer option for debug builds
```

#### 6B: Cross-Platform Compilation
| Platform | Target | Status |
|----------|--------|--------|
| Windows (MSVC 2022) | x64 | Not tested |
| Linux (GCC 11+) | x64 | Not tested |
| Linux (Clang 14+) | x64 | Not tested |
| Docker (Alpine) | x64 | Not tested |

#### 6C: CI/CD Pipeline
```yaml
# .github/workflows/build.yml
- [ ] Build on Windows (MSVC)
- [ ] Build on Ubuntu (GCC, Clang)
- [ ] Run unit tests
- [ ] Static analysis (clang-tidy)
- [ ] Generate build artifacts
```

### Success Criteria
- [ ] Zero compiler warnings at `-Wall -Wextra -Werror`
- [ ] All 70 source files compile successfully
- [ ] Docker image builds without errors
- [ ] CI passes on PR

---

## Phase 7: Unit Testing (Week 18-19)

### Goal
Achieve >80% code coverage for all core systems.

### Test Matrix

#### 7A: Core ECS Tests
```cpp
TEST_CASE("SpatialHash basic operations", "[spatial]") {
    SpatialHash hash(10.0f);  // 10m cells
    
    SECTION("Insert and query") {
        hash.insert(1, 5.0f, 5.0f);
        auto nearby = hash.query(5.0f, 5.0f, 1.0f);
        REQUIRE(nearby.size() == 1);
        REQUIRE(nearby[0] == 1);
    }
    
    SECTION("Update position") {
        hash.insert(1, 5.0f, 5.0f);
        hash.update(1, 5.0f, 5.0f, 15.0f, 15.0f);
        auto oldNearby = hash.query(5.0f, 5.0f, 1.0f);
        auto newNearby = hash.query(15.0f, 15.0f, 1.0f);
        REQUIRE(oldNearby.empty());
        REQUIRE(newNearby.size() == 1);
    }
    
    SECTION("Remove entity") {
        hash.insert(1, 5.0f, 5.0f);
        hash.remove(1);
        auto nearby = hash.query(5.0f, 5.0f, 1.0f);
        REQUIRE(nearby.empty());
    }
}
```

#### 7B: Movement System Tests
| Test Case | Input | Expected | Status |
|-----------|-------|----------|--------|
| Basic movement | WASD forward | Position updates | Pending |
| Speed limit | Sprint input | Capped at max speed | Pending |
| Collision stop | Wall ahead | Position clamped | Pending |
| Anti-cheat validation | Teleport 100m | Rejected, kicked | Pending |

#### 7C: Network Protocol Tests
```cpp
TEST_CASE("Delta compression correctness", "[network]") {
    // Given two entity states
    EntityStateData baseline = createEntity(1, 1000, 0, 1000, 0, 0, 0);
    EntityStateData current = createEntity(1, 1050, 0, 1050, 100, 0, 100);
    
    // When delta is computed
    auto delta = createDeltaSnapshot(100, 99, {current}, {}, {baseline});
    
    // Then reconstruction should match
    std::vector<EntityStateData> reconstructed;
    REQUIRE(applyDeltaSnapshot(delta, reconstructed));
    REQUIRE(reconstructed[0].position.x == current.position.x);
}
```

#### 7D: Combat System Tests
| Scenario | Setup | Expected Result | Status |
|----------|-------|-----------------|--------|
| Valid hit | Attacker facing target at 1m | Hit registered | Pending |
| Out of range | Target at 10m | Miss | Pending |
| Wrong angle | Target behind attacker | Miss | Pending |
| Lag compensation | 100ms latency | Hit validated against history | Pending |

#### 7E: Migration Tests
```cpp
TEST_CASE("Entity migration state machine", "[migration]") {
    MigrationManager manager;
    EntityID entity = 42;
    
    REQUIRE(manager.getState(entity) == MigrationState::NONE);
    
    manager.initiate(entity, 1, 2);
    REQUIRE(manager.getState(entity) == MigrationState::PREPARING);
    
    // Simulate acknowledgment from target zone
    manager.onTargetAck(entity);
    REQUIRE(manager.getState(entity) == MigrationState::TRANSFERRING);
    
    // Simulate completion
    manager.onTransferComplete(entity);
    REQUIRE(manager.getState(entity) == MigrationState::COMPLETED);
}
```

### Success Criteria
- [ ] >80% line coverage for core systems
- [ ] All existing tests pass
- [ ] New tests added for Phase 4-5 features
- [ ] CI runs tests on every commit

---

## Phase 8: Integration Testing (Week 20-21)

### Goal
Validate end-to-end functionality with multiple components.

### 8A: Single-Player Integration Test
```bash
# Test scenario: One player connects and moves
python tools/integration/single_player_test.py \
    --server localhost:7777 \
    --duration 60 \
    --verify-prediction \
    --verify-reconciliation
```

**Validation Points**:
- [ ] Client connects successfully
- [ ] Client can send inputs
- [ ] Server processes movement
- [ ] Server sends corrections
- [ ] Client reconciles correctly
- [ ] No desync after 60 seconds

### 8B: Multi-Player Integration Test
```bash
# Test scenario: 10 players in same area
python tools/integration/multiplayer_sync_test.py \
    --server localhost:7777 \
    --players 10 \
    --duration 120 \
    --verify-visibility \
    --verify-interpolation
```

**Validation Points**:
- [ ] All players connect
- [ ] Players see each other
- [ ] Position updates received
- [ ] Interpolation smooth (no jitter)
- [ ] AOI updates correct

### 8C: Combat Integration Test
```bash
# Test scenario: 2 players combat
python tools/integration/combat_test.py \
    --server localhost:7777 \
    --attacker bot1 \
    --target bot2 \
    --latency 100 \
    --verify-lag-compensation
```

**Validation Points**:
- [ ] Attack input processed
- [ ] Hit detection accurate
- [ ] Lag compensation working
- [ ] Combat events logged
- [ ] Client feedback received

### 8D: Cross-Zone Integration Test
```bash
# Test scenario: Player moves between zones
python tools/integration/zone_migration_test.py \
    --source-zone localhost:7777 \
    --target-zone localhost:7779 \
    --migration-point 0,0 \
    --verify-seamless
```

**Validation Points**:
- [ ] Migration triggered at boundary
- [ ] State transferred correctly
- [ ] Target zone accepts migration
- [ ] Player appears in new zone
- [ ] No position/velocity loss
- [ ] Total time < 5 seconds

---

## Phase 9: Stress & Performance Testing (Week 22-23)

### Goal
Validate performance under load and identify bottlenecks.

### 9A: Load Testing Matrix

| Test | Players/Bots | Duration | Target | Tool |
|------|--------------|----------|--------|------|
| Light Load | 50 | 10 min | <16ms tick | bot_swarm.py |
| Medium Load | 200 | 10 min | <16ms tick | bot_swarm.py |
| Heavy Load | 400 | 10 min | <20ms tick | bot_swarm.py |
| Spike Test | 0→400→0 | 5 min | No crashes | bot_swarm.py |
| Sustained | 400 | 1 hour | Stable memory | bot_swarm.py |

### 9B: Performance Benchmarks
```cpp
// Google Benchmark tests
BENCHMARK(SpatialHashQuery)->Range(1, 1000);
BENCHMARK(MovementSystemUpdate)->Range(1, 4000);
BENCHMARK(DeltaCompression)->Range(1, 400);
BENCHMARK(AOIQuery)->Range(1, 400);
BENCHMARK(LagCompensatorRewind)->Range(1, 120);
```

### 9C: Memory Profiling
```bash
# Valgrind memory check
valgrind --leak-check=full --show-leak-kinds=all \
    ./darkages_server --config test_config.json

# AddressSanitizer
./darkages_server_asan --config test_config.json &
python tools/stress-test/bot_swarm.py --bots 100 --duration 300
```

**Success Criteria**:
- [ ] No memory leaks detected
- [ ] No use-after-free
- [ ] Memory usage stable over 1 hour
- [ ] No heap allocations during tick

### 9D: Network Proficiency
| Metric | Target | Measurement |
|--------|--------|-------------|
| Bandwidth (down) | <20 KB/s/player | tcpdump/wireshark |
| Bandwidth (up) | <2 KB/s/player | tcpdump/wireshark |
| Snapshot rate | 20 Hz | Server metrics |
| Packet loss | <0.1% | GNS stats |
| RTT | <50ms | GNS stats |

---

## Phase 10: Security & Penetration Testing (Week 24)

### Goal
Validate security measures and identify vulnerabilities.

### 10A: Anti-Cheat Validation
```python
# security_test.py
class SecurityTests:
    def test_speed_hack(self):
        # Send movement faster than possible
        client.send_movement(speed=50.0)  # Max is 5.0
        assert client.is_kicked()
    
    def test_teleport_hack(self):
        # Jump to impossible position
        client.send_position(10000, 0, 10000)
        assert client.is_kicked()
    
    def test_packet_flood(self):
        # Send 1000 packets in 1 second
        for _ in range(1000):
            client.send_input()
        assert client.is_rate_limited()
    
    def test_invalid_hit(self):
        # Claim hit from 100m away
        client.send_attack(target_id, range=100.0)
        assert not hit_registered
```

### 10B: DDoS Simulation
```bash
# Using chaos testing framework
python tools/chaos/chaos_engine.py \
    --scenario load_spike \
    --intensity 10 \
    --duration 60
```

**Validation**:
- [ ] Legitimate players not affected
- [ ] Attacking IPs blocked
- [ ] Emergency mode activates
- [ ] Recovery successful

### 10C: Fuzz Testing
```cpp
// AFL++ fuzzing harness
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Protocol::ClientInputPacket input;
    if (Protocol::deserializeInput(data, size, input)) {
        // Process through validation
        AntiCheat::validateInput(input);
    }
    return 0;
}
```

---

## Phase 11: Chaos & Resilience Testing (Week 25)

### Goal
Validate system behavior under failure conditions.

### 11A: Chaos Scenarios

| Scenario | Frequency | Expected Behavior | Recovery Time |
|----------|-----------|-------------------|---------------|
| Zone kill | Daily test | Players migrate | <30s |
| Network partition | Weekly | Queue + retry | <60s |
| Redis failure | Weekly | Cache miss, degrade | <10s |
| ScyllaDB slow | Weekly | Async queue buildup | <30s |
| Memory pressure | Monthly | QoS degradation | <5s |

### 11B: Recovery Validation
```bash
# Full system restart test
docker-compose -f infra/docker-compose.prod.yml down
docker-compose -f infra/docker-compose.prod.yml up -d

# Verify all services healthy
for zone in 1 2 3 4; do
    curl -f http://localhost:$((7777 + (zone-1)*2))/health
done

# Run smoke test
python tools/integration/smoke_test.py
```

---

## Phase 12: User Acceptance Testing (Week 26)

### Goal
Validate gameplay experience with real players.

### 12A: Internal Playtest
- 20 employees, 2-hour session
- All zones active
- Combat scenarios
- Migration testing

### 12B: External Alpha
- 100 invited players
- 1-week duration
- Bug reporting system
- Telemetry collection

### 12C: Feedback Collection
| Metric | Target | Measurement |
|--------|--------|-------------|
| Connection success | >98% | Server logs |
| Disconnect rate | <2% | Server logs |
| Player satisfaction | >4.0/5.0 | Survey |
| Bug reports | <10 critical | Issue tracker |

---

## Test Infrastructure Requirements

### Hardware
| Environment | Specs | Purpose |
|-------------|-------|---------|
| CI Runner | 4 vCPU, 8GB RAM | Build + unit tests |
| Integration Test | 8 vCPU, 16GB RAM | Multi-zone testing |
| Load Test | 16 vCPU, 32GB RAM | 1000+ bot simulation |
| Chaos Test | 8 vCPU, 16GB RAM | Failure injection |

### Software
```yaml
# Required tools
testing:
  unit: ["Catch2", "GoogleTest", "Google Benchmark"]
  coverage: ["gcov", "llvm-cov", "codecov.io"]
  integration: ["Python 3.10+", "asyncio", "docker SDK"]
  security: ["AFL++", "Valgrind", "AddressSanitizer"]
  monitoring: ["Prometheus", "Grafana", "Jaeger"]
```

---

## Success Criteria Summary

### Phase 6 (Build)
- [ ] Clean compilation on all platforms
- [ ] Zero compiler warnings
- [ ] Docker images build successfully

### Phase 7 (Unit Tests)
- [ ] >80% code coverage
- [ ] All tests passing
- [ ] CI integration

### Phase 8 (Integration)
- [ ] Single-player: 60s without desync
- [ ] Multi-player: 10 players sync correctly
- [ ] Combat: Hit detection accurate
- [ ] Migration: <5s seamless transition

### Phase 9 (Performance)
- [ ] 400 players @ <16ms tick
- [ ] Zero memory leaks
- [ ] Bandwidth <20 KB/s/player
- [ ] 1-hour stability test passed

### Phase 10 (Security)
- [ ] All cheat attempts blocked
- [ ] DDoS handled gracefully
- [ ] No crashes from fuzzing

### Phase 11 (Chaos)
- [ ] Zone recovery <30s
- [ ] No data loss during failures
- [ ] Players can reconnect

### Phase 12 (UAT)
- [ ] >95% player satisfaction
- [ ] <5 critical bugs
- [ ] Ready for public beta

---

## Timeline

```
Week 17:  Phase 6 - Build System & Compilation
Week 18-19: Phase 7 - Unit Testing
Week 20-21: Phase 8 - Integration Testing
Week 22-23: Phase 9 - Stress & Performance Testing
Week 24:    Phase 10 - Security Testing
Week 25:    Phase 11 - Chaos Testing
Week 26:    Phase 12 - User Acceptance Testing
Week 27:    Bug fixes, stabilization
Week 28:    Production readiness review
```

---

## Immediate Next Actions

1. **Fix CMakeLists.txt** - Ensure all dependencies linked correctly
2. **Set up CI** - GitHub Actions for automated builds
3. **Create first integration test** - Single player movement
4. **Fix compilation errors** - Address any build failures
5. **Document API** - Generate docs from headers

---

*This document is a living plan. Update as testing reveals new requirements.*
