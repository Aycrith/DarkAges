# DarkAges Stress Test & Integration Testing Tools (WP-6-5)

Python-based testing framework for the DarkAges MMO server, including bot swarms for load testing, integration test harness, and network chaos testing.

## Overview

This toolkit provides comprehensive testing capabilities for the DarkAges multiplayer synchronization system:

- **Integration Test Harness** (`integration_harness.py`) - Structured integration testing framework (WP-6-5)
- **Bot Swarm** (`bot_swarm.py`) - Simulates 10-1000+ players with realistic movement patterns
- **Multi-Player Test** (`test_multiplayer.py`) - Automated validation of server synchronization
- **Network Chaos** (`network_chaos.py`) - Network condition simulation for resilience testing
- **Latency Simulator** (`latency_simulator.py`) - Legacy network condition tool

## Quick Start

```bash
# Terminal 1: Start infrastructure (Redis + ScyllaDB)
cd infra
docker-compose up -d redis scylla

# Terminal 2: Start the DarkAges server
./build/darkages_server

# Terminal 3: Run integration tests
cd tools/stress-test
pip install -r requirements.txt

# Check service health
python integration_harness.py --health

# Run basic connectivity test
python integration_harness.py --test basic_connectivity

# Run 10-player integration test
python integration_harness.py --test 10_player_session

# Run full integration test suite
python integration_harness.py --all
```

## Integration Test Harness (WP-6-5)

The `integration_harness.py` is the main testing framework for DarkAges server integration testing.

### Available Tests

| Test Name | Description | Stage |
|-----------|-------------|-------|
| `service_health` | Check Redis, ScyllaDB, and server health | Pre-flight |
| `basic_connectivity` | Single bot connects and receives snapshot | Week 1 |
| `10_player_session` | 10 bots connect, move, disconnect cleanly | Week 4 |
| `redis_integration` | Session data persists in Redis | Week 2 |
| `scylla_integration` | Profile data persists in ScyllaDB | Week 3 |
| `disconnect_reconnect` | Clean disconnect/reconnect handling | Week 4 |
| `bandwidth_compliance` | Bandwidth within budget (<20KB/s down, <2KB/s up) | Week 4+ |
| `stress_50_connections` | 50 concurrent connection test | Week 6 |

### Usage Examples

```bash
# Run all integration tests
python integration_harness.py --all

# Run specific test
python integration_harness.py --test basic_connectivity
python integration_harness.py --test 10_player_session
python integration_harness.py --test redis_integration

# Check service health only
python integration_harness.py --health

# List available tests
python integration_harness.py --list

# Save results to JSON file
python integration_harness.py --all --output results.json

# Connect to remote server
python integration_harness.py --all --server 192.168.1.100 --port 7777
```

### Stress Testing

```bash
# Run stress test with 100 bots for 60 seconds
python integration_harness.py --stress 100 --duration 60

# Run stress test with custom parameters
python integration_harness.py --stress 500 --duration 300 --server prod.example.com
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | One or more tests failed |
| 2 | Critical failure (services unavailable) |

## Bot Swarm

Simulates multiple players connecting to the server with realistic movement patterns.

### Basic Usage

```bash
# Run 10 bots for 60 seconds (default)
python bot_swarm.py

# Run 50 bots for 120 seconds
python bot_swarm.py --bots 50 --duration 120

# Connect to remote server
python bot_swarm.py --host 192.168.1.100 --port 7777

# Use specific movement pattern
python bot_swarm.py --bots 20 --pattern circle
```

### Movement Patterns

- `random` - Random wandering with momentum (most realistic)
- `circle` - Move in continuous circles
- `linear` - Walk back and forth
- `stationary` - No movement (baseline testing)

## Network Chaos Testing

Simulates network problems to test server resilience.

### Prerequisites

- **Linux**: `tc` (traffic control) - install with `sudo apt-get install iproute2`
- **macOS**: Limited support via `pfctl`
- **Windows**: Requires WSL or external tool like [Clumsy](https://jagt.github.io/clumsy/)

### Usage

```bash
# Add 100ms latency with 20ms jitter (realistic WAN)
sudo python network_chaos.py --latency 100 --jitter 20

# Poor mobile connection simulation
sudo python network_chaos.py --latency 200 --jitter 50 --loss 3

# Use predefined profile
sudo python network_chaos.py --profile mobile-3g

# Automated chaos test for 5 minutes
sudo python network_chaos.py --chaos-test 300

# Reset to normal (IMPORTANT!)
sudo python network_chaos.py --reset
```

### Network Profiles

| Profile | Latency | Jitter | Loss | Use Case |
|---------|---------|--------|------|----------|
| lan | 1ms | 0ms | 0% | Local testing baseline |
| wan-good | 50ms | 5ms | 0% | Fiber/cable connection |
| wan-average | 100ms | 20ms | 0.5% | Typical internet |
| mobile-4g | 80ms | 15ms | 0.3% | 4G mobile |
| mobile-3g | 200ms | 50ms | 2% | 3G mobile |
| poor | 300ms | 100ms | 5% | Edge cases |
| satellite | 600ms | 50ms | 1% | Satellite internet |

## Docker Compose Testing

Run full integration tests with Docker Compose:

```bash
# Start all services (Redis, ScyllaDB, Server)
cd infra
docker-compose -f docker-compose.test.yml up -d

# Run integration tests
docker-compose -f docker-compose.test.yml --profile test run --rm integration-test

# Run stress test
docker-compose -f docker-compose.test.yml --profile stress run --rm stress-test

# Run chaos test (requires privileged mode)
docker-compose -f docker-compose.test.yml --profile chaos run --rm chaos-test

# View logs
docker-compose -f docker-compose.test.yml logs -f server

# Tear down
docker-compose -f docker-compose.test.yml down -v
```

## CI/CD Integration

### GitHub Actions

The project includes a GitHub Actions workflow for automated integration testing:

```yaml
- name: Run Integration Tests
  run: |
    cd tools/stress-test
    pip install -r requirements.txt
    python integration_harness.py --all
```

See `.github/workflows/integration-test.yml` for the complete configuration.

## Protocol Details

The bots use a simplified binary protocol that matches the FlatBuffers schema:

### Client Input Packet (20 bytes)

```
[offset: type]    description
[0: u8]           packet type (1 = ClientInput)
[1: u32]          sequence number
[5: u32]          timestamp (ms)
[9: u8]           input flags (bitfield)
[10: i16]         yaw (quantized: value / 10000)
[12: i16]         pitch (quantized: value / 10000)
[14: u32]         target entity (0 = none)
```

### Input Flags (bit field)

| Bit | Flag | Description |
|-----|------|-------------|
| 0 | INPUT_FORWARD | Move forward |
| 1 | INPUT_BACKWARD | Move backward |
| 2 | INPUT_LEFT | Strafe left |
| 3 | INPUT_RIGHT | Strafe right |
| 4 | INPUT_JUMP | Jump |
| 5 | INPUT_ATTACK | Attack |
| 6 | INPUT_BLOCK | Block |
| 7 | INPUT_SPRINT | Sprint |

### Connection Request (13 bytes)

```
[0: u8]           packet type (3 = ConnectionRequest)
[1: u32]          protocol version
[5: u64]          player ID
```

### Server Snapshot (variable)

```
[0: u8]           packet type (2 = Snapshot)
[1: u32]          server tick
[5: u32]          baseline tick (for delta compression)
[9: u32]          server time
[13: u32]         last processed input (for reconciliation)
[17: ...]         entity states (variable)
```

## Test Stages (from Research Analysis)

```
Week 1: GameNetworkingSockets + FlatBuffers (no database)
  - Test: basic_connectivity
  - Exit: UDP handshake working

Week 2: Add Redis hot-state
  - Test: redis_integration
  - Exit: Session persistence working

Week 3: Add ScyllaDB persistence
  - Test: scylla_integration
  - Exit: Database persistence working

Week 4-5: Full integration
  - Test: 10_player_session, disconnect_reconnect
  - Exit: Client connects, moves, disconnects cleanly

Week 6: Stress testing
  - Test: stress_50_connections, stress_test with N bots
  - Exit: No crashes, <16ms tick time
```

## Performance Benchmarks

Expected performance on reference hardware (Linux, local server):

| Bots | CPU Usage | Memory | Bandwidth (up) | Bandwidth (down) |
|------|-----------|--------|----------------|------------------|
| 10 | <5% | <50 MB | ~10 KB/s | ~40 KB/s |
| 50 | <15% | <200 MB | ~50 KB/s | ~200 KB/s |
| 100 | <30% | <400 MB | ~100 KB/s | ~400 KB/s |
| 200 | <60% | <800 MB | ~200 KB/s | ~800 KB/s |

*Note: Actual performance depends on server hardware and network conditions.*

## Troubleshooting

### "No bots could connect"

- Verify server is running: `netstat -an | grep 7777`
- Check firewall rules allow UDP port 7777
- Verify correct host/port arguments
- Run `--health` check to verify services

### "Redis/Scylla connection failed"

- Verify services are running: `docker-compose ps`
- Check Redis: `redis-cli ping`
- Check Scylla: `cqlsh localhost 9042`
- Install dependencies: `pip install redis cassandra-driver`

### "tc command not found" (Linux)

```bash
# Install iproute2 package
sudo apt-get install iproute2    # Debian/Ubuntu
sudo yum install iproute          # RHEL/CentOS
```

### High bandwidth usage

- Check snapshot rate on server (should be 20Hz)
- Verify delta compression is enabled
- Check for excessive entity updates

### High correction rate

- Verify client prediction matches server physics
- Check for clock drift between client and server
- Ensure input processing is deterministic

## References

- Protocol: `src/shared/proto/game_protocol.fbs`
- Constants: `src/server/include/Constants.hpp`
- Architecture: `ImplementationRoadmapGuide.md`
- WP-6-5 Specification: `PHASES_6_7_8_ROADMAP.md`
