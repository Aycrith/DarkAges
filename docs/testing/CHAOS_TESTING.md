# Chaos Testing Guide

## Overview

Chaos testing (also known as Chaos Engineering) verifies system resilience by intentionally injecting failures into controlled test environments. The DarkAges MMO Chaos Framework simulates real-world failures to ensure the game server can recover gracefully.

## Why Chaos Testing?

- **Validate Recovery**: Ensure zones restart and players migrate correctly
- **Test Network Resilience**: Verify system handles partitions gracefully
- **Verify Load Handling**: Confirm servers don't crash under sudden load
- **Build Confidence**: Know your system can handle production failures

## Prerequisites

### Environment Setup

1. **Docker & Docker Compose** - For container management
2. **Running Multi-Zone Deployment** - Start with:
   ```bash
   cd infra
   docker-compose -f docker-compose.multi-zone.yml up -d
   ```
3. **Python 3.8+** with dependencies:
   ```bash
   cd tools/chaos
   pip install -r requirements.txt
   ```

### Safety Requirements

⚠️ **CRITICAL**: Only run chaos tests in:
- Local development environments
- Dedicated test/staging servers
- CI/CD pipelines with isolated infrastructure

**NEVER** run against production systems.

## Scenarios

### 1. Zone Failure

Kills a zone server container and verifies the system recovers.

**What It Tests:**
- Zone failure detection via health checks
- Player migration to adjacent zones
- Zone restart capability
- Data persistence (no player data loss)
- Recovery time within SLA (120 seconds)

**Usage:**
```bash
# Kill random zone for 30 seconds
python chaos_engine.py --scenario zone_failure --yes-i-know-what-im-doing

# Target specific zone
python chaos_engine.py --scenario zone_failure --target 1 --duration 60 --yes-i-know-what-im-doing
```

**Success Criteria:**
- All zones back online within 120 seconds
- No player data loss
- Tick rate stays above 50Hz during recovery
- Migration success rate > 95%

---

### 2. Network Partition

Disconnects zones from each other to simulate network issues.

**What It Tests:**
- Aura buffer sync failure handling
- Cross-zone messaging resilience
- Eventual consistency recovery
- Zone boundary handling during partition

**Usage:**
```bash
# Partition zones 1 and 2 for 30 seconds
python chaos_engine.py --scenario network_partition --targets 1,2 --duration 30 --yes-i-know-what-im-doing
```

**Success Criteria:**
- Zones detect partition within 5 seconds
- No cascading failures
- Clean reconnection when network restored
- State synchronization completes within 60 seconds

---

### 3. Load Spike

Rapidly spawns 50-500 bots to simulate sudden player influx.

**What It Tests:**
- Server overload handling
- QoS degradation (reduced update rates)
- No crashes under load
- Connection acceptance rate
- Recovery after spike ends

**Usage:**
```bash
# 3x intensity (150 bots) for 60 seconds
python chaos_engine.py --scenario load_spike --intensity 3 --duration 60 --yes-i-know-what-im-doing

# Maximum load test (10x = 500 bots)
python chaos_engine.py --scenario load_spike --intensity 10 --duration 120 --yes-i-know-what-im-doing
```

**Success Criteria:**
- Server remains responsive during spike
- No crashes or memory leaks
- Tick rate degrades gracefully (doesn't drop to 0)
- 80%+ bot connection success rate
- Recovery within 60 seconds after disconnect

---

### 4. Cascade Failure

Kills all zones sequentially to test total system failure recovery.

**What It Tests:**
- System resilience to multiple failures
- Remaining zone overload handling
- Recovery from severely degraded state
- Orchestrator restart capability

**Usage:**
```bash
# Cascade failure over 60 seconds, then recovery
python chaos_engine.py --scenario cascade_failure --duration 60 --yes-i-know-what-im-doing
```

**Success Criteria:**
- System remains partially operational with 2+ zones down
- Full recovery within 180 seconds
- No data loss during cascade

---

### 5. Run All Scenarios

Executes all chaos scenarios sequentially with a full report.

**Usage:**
```bash
python chaos_engine.py --scenario all --yes-i-know-what-im-doing
```

**Output:**
- Individual scenario reports
- Final summary with pass/fail status
- Recovery times for each scenario

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `CHAOS_REDIS_HOST` | `localhost` | Redis server hostname |
| `CHAOS_REDIS_PORT` | `6379` | Redis server port |
| `CHAOS_NETWORK_NAME` | `darkages-network` | Docker network name |
| `CHAOS_ZONE_PREFIX` | `darkages-zone-` | Zone container name prefix |

### Command-Line Options

```
--scenario {zone_failure,network_partition,load_spike,cascade_failure,all}
                      Chaos scenario to run
--duration FLOAT      Duration of chaos in seconds (default: 30)
--target INT          Target zone ID for zone_failure
--targets STR         Comma-separated zone IDs for network_partition
--intensity INT       Load spike multiplier (default: 3)
--redis-host STR      Override Redis host
--redis-port INT      Override Redis port
--output FILE         Save JSON results to file
--yes-i-know-what-im-doing  Safety acknowledgment (required)
```

## Interpreting Results

### Event Log

Each chaos action is logged with:
```
<timestamp> | <scenario> | <target> | <action> | <result> | <duration>
```

Example:
```
12.34s | zone_failure | zone-1 | kill_container | success | 523.4ms
45.67s | zone_failure | zone-1 | restart_container | success | 2341.2ms
```

### Metrics

Before and after metrics show system impact:
```
Players:        45 → 42 (Δ-3)
Online Zones:    4 →  4 (Δ0)
Avg Tick:       16.23ms → 15.89ms
Packet Loss:     0.00% → 0.05%
Migrations:     12 → 15 (failures: 0)
```

### Recovery Time

Time from failure injection to stable state:
- **Zone Failure**: Typically 30-60 seconds
- **Network Partition**: Typically 10-30 seconds
- **Load Spike**: Typically 10-20 seconds after disconnect
- **Cascade Failure**: Up to 120 seconds

## Integration with CI/CD

### GitHub Actions Example

```yaml
name: Chaos Tests

on:
  schedule:
    - cron: '0 2 * * 0'  # Weekly on Sunday at 2 AM

jobs:
  chaos:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Start test environment
        run: |
          cd infra
          docker-compose -f docker-compose.multi-zone.yml up -d
          sleep 60  # Wait for initialization
      
      - name: Run chaos tests
        run: |
          cd tools/chaos
          pip install -r requirements.txt
          python chaos_engine.py --scenario all \
            --yes-i-know-what-im-doing \
            --output chaos-results.json
      
      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: chaos-results
          path: tools/chaos/chaos-results.json
      
      - name: Cleanup
        if: always()
        run: |
          cd infra
          docker-compose -f docker-compose.multi-zone.yml down
```

## Troubleshooting

### Redis Connection Failed

**Problem**: `WARNING: Redis connection failed`

**Solutions**:
1. Ensure Redis is running: `docker ps | grep redis`
2. Check Redis host/port: `--redis-host localhost --redis-port 6379`
3. Verify network connectivity

### Docker Connection Failed

**Problem**: `WARNING: Docker connection failed`

**Solutions**:
1. Ensure Docker daemon is running
2. Check user permissions (add to docker group)
3. On Windows: Ensure Docker Desktop is running

### Container Not Found

**Problem**: `Zone X container not found`

**Solutions**:
1. Check container names: `docker ps`
2. Verify `CHAOS_ZONE_PREFIX` matches your naming
3. Ensure multi-zone deployment is running

### Bots Fail to Connect

**Problem**: Load spike shows 0 connected bots

**Solutions**:
1. Check zone ports are exposed: 7777, 7779, 7781, 7783
2. Verify server is accepting connections
3. Check firewall rules

## Safety Features

1. **Explicit Flag Required**: `--yes-i-know-what-im-doing` must be provided
2. **Timeout Limits**: All scenarios have maximum durations
3. **Auto-Recovery**: Zones are automatically restarted after tests
4. **Graceful Degradation**: Failed scenarios don't leave system broken

## Future Enhancements

Planned additions to the chaos framework:

- [ ] Latency injection (TC/netem)
- [ ] Packet corruption simulation
- [ ] Database failure scenarios (Redis/Scylla)
- [ ] Memory pressure testing
- [ ] CPU throttling tests
- [ ] Prometheus metrics integration
- [ ] Automated SLA validation
- [ ] Chaos scheduling (random failures during load tests)

## References

- [Principles of Chaos Engineering](https://principlesofchaos.org/)
- [Netflix Chaos Monkey](https://netflix.github.io/chaosmonkey/)
- [Docker SDK for Python](https://docker-py.readthedocs.io/)
- [Redis Python Client](https://redis-py.readthedocs.io/)
