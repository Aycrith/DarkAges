# DarkAges Stress Test Tools

Python-based bot swarm for stress testing the DarkAges MMO server. Simulates multiple concurrent players connecting to the server.

## Overview

This toolkit provides automated testing capabilities for the DarkAges multiplayer synchronization system:

- **Bot Swarm**: Simulates 10+ players with realistic movement patterns
- **Multi-Player Test**: Automated validation of server synchronization
- **Latency Simulator**: Test behavior under poor network conditions

## Quick Start

```bash
# Terminal 1: Start the DarkAges server
./darkages-server

# Terminal 2: Run the multiplayer test
cd tools/stress-test
python test_multiplayer.py
```

## Files

| File | Description |
|------|-------------|
| `bot_swarm.py` | Main stress testing tool - simulates multiple game clients |
| `test_multiplayer.py` | Automated integration test with pass/fail validation |
| `latency_simulator.py` | Network condition simulation (Linux/macOS) |
| `requirements.txt` | Python dependencies (standard library only) |
| `README.md` | This file |

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

### Example Output

```
============================================================
DARKAGES BOT SWARM - Starting Test
============================================================
Target server: 127.0.0.1:7777
Bot count: 10
Duration: 60s
Input rate: 60Hz
------------------------------------------------------------

Connecting 10 bots...
Connected: 10/10

Running test for 60 seconds...

Disconnecting bots...

============================================================
BOT SWARM TEST SUMMARY
============================================================
Bots Connected: 10
Test Duration: 60.1s

Packet Statistics:
  Total packets sent: 36,120
  Total packets received: 12,150
  Packets sent/sec: 601
  Packets received/sec: 202

Bandwidth Usage:
  Total sent: 722.66 KB
  Total received: 2.45 MB
  Upload rate: 96.36 Kbps
  Download rate: 326.59 Kbps

Per-Bot Averages:
  Snapshots received: 1,215.0
  Server corrections: 12.3
  Avg upload: 9.64 Kbps/bot
  Avg download: 32.66 Kbps/bot

Budget Compliance:
  Upload budget (2KB/s): PASS (1204 bytes/s)
  Download budget (20KB/s): PASS (4083 bytes/s)
  Snapshot rate: 20.2/sec (expected 20/sec, ratio 1.01)
============================================================
```

## Multi-Player Test

Automated integration test that validates multiplayer synchronization.

### Usage

```bash
# Standard 10-player test for 30 seconds
python test_multiplayer.py

# Quick smoke test
python test_multiplayer.py --bots 5 --duration 10

# Stress test with 100 players
python test_multiplayer.py --bots 100 --duration 120
```

### Test Coverage

The test validates:

1. **Connection Rate** - ≥90% of bots must connect
2. **Snapshot Reception** - ≥80% of expected snapshots received
3. **Bandwidth Upload** - Within 2KB/s budget per bot
4. **Bandwidth Download** - Within 20KB/s budget per bot
5. **Packet Integrity** - All bots send and receive packets
6. **Prediction Accuracy** - <5% of inputs result in corrections
7. **Input Acknowledgment** - Server acknowledges processed inputs

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | One or more tests failed |
| 2 | Could not connect to server |

### Example Output

```
======================================================================
DARKAGES MULTI-PLAYER SYNCHRONIZATION TEST
======================================================================
Configuration:
  Server: 127.0.0.1:7777
  Bots: 10
  Duration: 30s
  Expected snapshot rate: 20Hz
======================================================================

============================================================
DARKAGES BOT SWARM - Starting Test
...

======================================================================
TEST RESULTS
======================================================================
  [PASS] Connection Rate          - 10/10 bots connected (100.0%)
  [PASS] Snapshot Reception       - 10/10 bots received >= 480 snapshots (avg rate: 20.1/sec)
  [PASS] Bandwidth Upload         - Upload: 1156 bytes/s (limit: 2048)
  [PASS] Bandwidth Download       - Download: 3842 bytes/s (limit: 20480)
  [PASS] Packet Integrity         - Total sent: 18012, received: 6045
  [PASS] Prediction Accuracy      - 45 corrections for 18012 inputs (0.25%)
  [PASS] Input Acknowledgment     - 10/10 bots received acks (avg ack ratio: 98.5%)
----------------------------------------------------------------------
RESULT: ALL TESTS PASSED (7/7)
======================================================================
```

## Latency Simulator

Simulates poor network conditions for testing game behavior under adverse conditions.

### Requirements

- **Linux**: `tc` (traffic control) - usually pre-installed
- **macOS**: `pfctl` and `dnctl` - limited support
- **Windows**: Requires external tool like [Clumsy](https://jagt.github.io/clumsy/)

### Linux Usage

```bash
# Add 100ms latency with 20ms jitter (realistic WAN)
sudo python latency_simulator.py --latency 100 --jitter 20

# Simulate poor mobile connection
sudo python latency_simulator.py --latency 200 --jitter 50 --loss 3

# Simulate very bad connection (stress testing)
sudo python latency_simulator.py --latency 300 --loss 10

# Add packet duplication and reordering
sudo python latency_simulator.py --latency 100 --duplicate 1 --reorder 0.5

# Reset to normal
sudo python latency_simulator.py --reset

# Show current rules
sudo python latency_simulator.py --show
```

### Network Profiles

| Profile | Latency | Jitter | Loss | Use Case |
|---------|---------|--------|------|----------|
| LAN | 1ms | 0ms | 0% | Local testing baseline |
| Good WAN | 50ms | 5ms | 0% | Fiber/cable connection |
| Average | 100ms | 20ms | 0.5% | Typical internet |
| Poor | 200ms | 50ms | 2% | Mobile/congested |
| Very Poor | 300ms | 100ms | 5% | Edge cases |
| Satellite | 600ms | 50ms | 1% | Satellite internet |

### Example: Test with Simulated Latency

```bash
# Terminal 1: Add 150ms latency
sudo python latency_simulator.py --latency 150 --jitter 30

# Terminal 2: Run multiplayer test
python test_multiplayer.py --duration 60

# Reset network when done
sudo python latency_simulator.py --reset
```

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

## CI/CD Integration

The test can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions workflow
- name: Run Multiplayer Test
  run: |
    cd tools/stress-test
    python test_multiplayer.py --bots 10 --duration 30
  timeout-minutes: 2
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

### "High bandwidth usage"

- Check snapshot rate on server (should be 20Hz)
- Verify delta compression is enabled
- Check for excessive entity updates

### "High correction rate"

- Verify client prediction matches server physics
- Check for clock drift between client and server
- Ensure input processing is deterministic

### Linux: "tc command not found"

```bash
# Install iproute2 package
sudo apt-get install iproute2    # Debian/Ubuntu
sudo yum install iproute          # RHEL/CentOS
```

## References

- Protocol: `src/shared/proto/game_protocol.fbs`
- Constants: `src/server/include/Constants.hpp`
- Architecture: `ImplementationRoadmapGuide.md`
