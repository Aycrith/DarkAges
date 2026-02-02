# Product Requirements Document: WP-8-4 Load Testing & Capacity Validation

**Project**: DarkAges MMO  
**Work Package**: WP-8-4  
**Status**: PLANNED (Weeks 5-6)  
**Owner**: DEVOPS_AGENT  
**Priority**: HIGH  

---

## 1. Executive Summary

Execute comprehensive load testing to validate server capacity, identify performance bottlenecks, and establish production scaling limits.

### Target Capacity
- **Single Zone**: 500 concurrent players @ 60Hz
- **Multi-Zone**: 2,000 concurrent players (4 zones)
- **Tick Stability**: < 16ms at 100% capacity
- **Latency**: < 50ms (P95) for all operations

---

## 2. Load Testing Scenarios

### 2.1 Baseline Tests

| Test | Players | Duration | Purpose |
|------|---------|----------|---------|
| Idle Load | 100, 500, 1000 | 10 min | Memory baseline |
| Movement Only | 100, 500, 1000 | 10 min | Network baseline |
| Combat Only | 100, 500 | 10 min | CPU baseline |
| Mixed Activity | 100, 500, 1000 | 1 hour | Realistic load |

### 2.2 Stress Tests

| Test | Players | Pattern | Purpose |
|------|---------|---------|---------|
| Spike Test | 0→1000 | Instant | Auto-scaling trigger |
| Ramp Test | 0→1000 | +100/min | Capacity discovery |
| Sustained | 1000 | 4 hours | Stability validation |
| Burst | 1000 | Random spikes | Recovery testing |

### 2.3 Soak Tests

| Test | Duration | Players | Purpose |
|------|----------|---------|---------|
| Memory Leak | 8 hours | 500 | Memory stability |
| Connection Churn | 4 hours | Variable | Connection handling |
| Zone Migration | 2 hours | 200 | Handoff stability |

---

## 3. Performance Metrics

### 3.1 Server Metrics

| Metric | Target | Warning | Critical |
|--------|--------|---------|----------|
| Tick Time (avg) | < 10ms | 10-16ms | > 16ms |
| Tick Time (p99) | < 16ms | 16-20ms | > 20ms |
| Memory Usage | < 2GB | 2-3GB | > 3GB |
| CPU Usage | < 70% | 70-90% | > 90% |
| Active Connections | 500 | 500-600 | > 600 |

### 3.2 Network Metrics

| Metric | Target | Warning | Critical |
|--------|--------|---------|----------|
| Snapshot Latency (p95) | < 50ms | 50-100ms | > 100ms |
| Input Latency (p95) | < 50ms | 50-100ms | > 100ms |
| Bandwidth/player | < 20KB/s | 20-30KB/s | > 30KB/s |
| Packet Loss | < 0.1% | 0.1-1% | > 1% |

### 3.3 Database Metrics

| Metric | Target | Warning | Critical |
|--------|--------|---------|----------|
| Redis Latency (p99) | < 1ms | 1-5ms | > 5ms |
| ScyllaDB Write | < 10ms | 10-50ms | > 50ms |
| Connection Pool | < 80% | 80-95% | > 95% |

---

## 4. Bot Behavior Profiles

### 4.1 Bot Types

```python
class BotProfile:
    """Base behavior profile for load testing bots"""
    
    IDLE = {
        "movement": 0.0,        # No movement
        "action_frequency": 0,   # No actions
        "network_variance": 0.0  # Stable connection
    }
    
    EXPLORER = {
        "movement": 0.7,        # 70% moving
        "move_pattern": "random_walk",
        "action_frequency": 0.1, # Occasional jumps
        "network_variance": 0.1  # Slight jitter
    }
    
    COMBATANT = {
        "movement": 0.5,        # 50% moving
        "combat_frequency": 0.3, # 30% attacking
        "target_selection": "nearest",
        "network_variance": 0.05
    }
    
    STRESS = {
        "movement": 1.0,        # Constant movement
        "rapid_inputs": True,    # Max input rate
        "network_variance": 0.2  # High jitter simulation
    }
```

### 4.2 Player Distribution

| Profile | Percentage | Count (1000 players) |
|---------|------------|----------------------|
| Idle | 20% | 200 |
| Explorer | 50% | 500 |
| Combatant | 25% | 250 |
| Stress | 5% | 50 |

---

## 5. Test Execution Plan

### Phase 1: Single Zone Capacity (Day 1-2)
```bash
# 100 players baseline
python tools/stress-test/enhanced_bot_swarm.py \
    --bots 100 \
    --profile mixed \
    --duration 600 \
    --output results_100.json

# 500 players target
python tools/stress-test/enhanced_bot_swarm.py \
    --bots 500 \
    --profile mixed \
    --duration 3600 \
    --output results_500.json

# 1000 players stress
python tools/stress-test/enhanced_bot_swarm.py \
    --bots 1000 \
    --profile stress \
    --duration 1800 \
    --output results_1000.json
```

### Phase 2: Multi-Zone Capacity (Day 3-4)
```bash
# 4 zones, 500 players each
python tools/stress-test/multi_zone_test.py \
    --zones 4 \
    --bots-per-zone 500 \
    --duration 3600 \
    --cross-zone-traffic 0.1 \
    --output multi_zone_results.json
```

### Phase 3: Soak Testing (Day 5-6)
```bash
# 8-hour sustained test
python tools/stress-test/soak_test.py \
    --bots 500 \
    --duration 28800 \
    --memory-profile \
    --output soak_results.json
```

### Phase 4: Analysis & Reporting (Day 7)
```bash
# Generate capacity report
python tools/stress-test/generate_capacity_report.py \
    --results results_*.json \
    --output capacity_report.html
```

---

## 6. Infrastructure Requirements

### Test Harness

| Component | Spec | Count |
|-----------|------|-------|
| Bot Runner | 4 vCPU, 8GB RAM | 10 |
| Monitoring | Prometheus + Grafana | 1 |
| Log Aggregation | ELK stack | 1 |

### Target Environment

| Component | Spec | Count |
|-----------|------|-------|
| Zone Server | 8 vCPU, 16GB RAM | 4 |
| Redis | 4 vCPU, 8GB RAM | 3 (cluster) |
| ScyllaDB | 8 vCPU, 16GB RAM | 5 (cluster) |

---

## 7. Success Criteria

### Must Meet (P0)
- [ ] 500 players @ 60Hz, < 16ms tick
- [ ] < 50ms latency (P95)
- [ ] < 20KB/s per player downstream
- [ ] Zero crashes in 1-hour test
- [ ] Zero memory leaks (8-hour test)

### Should Meet (P1)
- [ ] 1000 players @ 60Hz
- [ ] < 30ms latency (P95) at 500 players
- [ ] Graceful degradation at capacity
- [ ] Auto-scaling triggers correctly

### Nice to Have (P2)
- [ ] 2000 players multi-zone
- [ ] < 20ms latency (P95)
- [ ] Self-healing under load

---

## 8. Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Tick overrun at 500 players | HIGH | Profile early, optimize hot paths |
| Memory leak detected | MEDIUM | AddressSanitizer, soak testing |
| Network bandwidth exceeded | MEDIUM | Delta compression tuning |
| Database bottleneck | MEDIUM | Connection pool tuning |

---

## 9. Deliverables

| Deliverable | Description |
|-------------|-------------|
| Capacity Report | Max players per zone, scaling factors |
| Performance Baseline | Metrics at various load levels |
| Bottleneck Analysis | Hot paths, optimization recommendations |
| Scaling Guide | How to scale horizontally/vertically |
| Cost Estimation | Cloud infrastructure costs |

---

**Last Updated**: 2026-02-01  
**Planned Start**: Week 5 of Phase 8
