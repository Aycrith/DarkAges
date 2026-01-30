# Delta Compression for Snapshots

## Overview

This document describes the delta compression implementation for DarkAges MMO server snapshots. Delta compression significantly reduces bandwidth by only sending entity state changes since a baseline snapshot, rather than the full state of all entities every frame.

## Goals

- **Bandwidth Target**: < 20 KB/s per player
- **Baseline History**: Store 1 second (60 frames) of snapshot history
- **Graceful Degradation**: Fall back to full snapshots when client lacks baseline
- **Entity Lifecycle**: Handle entity creation and destruction correctly

## Architecture

### Components

1. **Protocol::createDeltaSnapshot()** - Creates compressed snapshot
2. **Protocol::applyDeltaSnapshot()** - Decompresses snapshot on client
3. **ZoneServer snapshot history** - Maintains 60-frame rolling buffer
4. **Client snapshot state tracking** - Per-client acknowledged baseline tracking

### Data Flow

```
Server Tick N:
  1. Capture current entity states
  2. Store in snapshot history (tick N)
  3. For each connected client:
     a. Find client's acknowledged baseline in history
     b. If found: create delta against baseline
     c. If not found: create full snapshot (baseline = 0)
     d. Send snapshot to client

Client:
  1. Receive snapshot with baseline_tick
  2. If baseline_tick == 0: replace all entity state
  3. If baseline_tick matches local state:
     a. Apply delta to local entities
     b. Remove destroyed entities
  4. Send acknowledgment with received tick
```

## Binary Format

### Snapshot Header (16 bytes)

```
Offset  Size  Field
0       4     server_tick      - Current server simulation tick
4       4     baseline_tick    - Baseline tick (0 = full snapshot)
8       2     entity_count     - Number of delta entities
10      2     removed_count    - Number of removed entities
12      4     flags            - Reserved for future use
```

### Entity Delta Entry (variable size)

```
Offset  Size  Field
0       4     entity_id        - Entity identifier
4       2     changed_fields   - Bit mask of included fields
6+      var   field_data       - Only changed fields included
```

### Changed Fields Bit Mask

| Bit | Constant | Field |
|-----|----------|-------|
| 0   | DELTA_POSITION | Position (X, Y, Z) |
| 1   | DELTA_ROTATION | Rotation (yaw, pitch) |
| 2   | DELTA_VELOCITY | Velocity (dx, dy, dz) |
| 3   | DELTA_HEALTH | Health percent |
| 4   | DELTA_ANIM_STATE | Animation state |
| 5   | DELTA_ENTITY_TYPE | Entity type |
| all | DELTA_NEW_ENTITY | All fields (for new entities) |

### Position Delta Encoding

Position deltas use variable-length encoding based on magnitude:

- **Small delta** (-127 to 127): 1 byte per component (3 bytes total)
  - Marker: None (direct value)
  
- **Medium delta** (-32767 to 32767): 3 bytes per component (9 bytes total)
  - Marker: 0x7F (positive) or 0x80 (negative)
  - Followed by int16 value

- **Large delta** (outside int16 range): 5 bytes per component (15 bytes total)
  - Marker: 0x81
  - Followed by int32 value

### Field Encodings

| Field | Size | Encoding |
|-------|------|----------|
| Position (full) | 6 bytes | int16 per axis (quantized) |
| Position (delta) | 3-15 bytes | Variable-length per axis |
| Rotation | 2 bytes | int8 per angle (~2 degree precision) |
| Velocity | 6 bytes | int16 per component |
| Health | 1 byte | uint8 (0-100) |
| Anim State | 1 byte | uint8 enum |
| Entity Type | 1 byte | uint8 (0=player, 1=projectile, etc.) |

## Bandwidth Analysis

### Typical Scenario: 50 Players, 20Hz Updates

**Full Snapshot (no delta):**
- Per entity: 4 (ID) + 12 (pos) + 6 (vel) + 2 (rot) + 1 (health) + 1 (anim) = 26 bytes
- 50 entities: 50 × 26 = 1300 bytes
- 20Hz: 1300 × 20 = 26,000 bytes/s = **26 KB/s**

**Delta Snapshot (50% of entities moving):**
- Header: 16 bytes
- Changed entities (25): 25 × (4 + 2 + avg 6 pos + 2 rot) = 350 bytes
- Static entities: 0 bytes (not included)
- 20Hz: (16 + 350) × 20 = 7,320 bytes/s = **7.3 KB/s**

**Compression Ratio: ~72% reduction**

### Worst Case: All Entities Moving Rapidly

- All 50 entities with position and velocity changes
- Per entity: 4 + 2 + 9 (pos delta) + 2 (rot) + 6 (vel) = 23 bytes
- Total: 16 + 50 × 23 = 1166 bytes
- Still below full snapshot size due to delta position encoding

## Implementation Details

### ZoneServer Integration

```cpp
// Snapshot history buffer
std::deque<SnapshotHistory> snapshotHistory_;
static constexpr size_t MAX_SNAPSHOT_HISTORY = 60;

// Per-client tracking
std::unordered_map<ConnectionID, ClientSnapshotState> clientSnapshotState_;

// Cleanup in updateReplication()
- Build current entity state
- Add to history
- Remove entries older than MAX_SNAPSHOT_HISTORY
- For each client: find baseline and create delta
```

### Delta Detection

```cpp
// Position comparison with tolerance (~6cm)
bool equalsPosition(const EntityStateData& other) {
    const int32_t threshold = 63;  // ~6cm in fixed-point
    return abs(x - other.x) < threshold &&
           abs(y - other.y) < threshold &&
           abs(z - other.z) < threshold;
}

// Rotation comparison (~2 degrees)
bool equalsRotation(const EntityStateData& other) {
    return abs(yaw - other.yaw) < 0.035f &&
           abs(pitch - other.pitch) < 0.035f;
}
```

## Edge Cases

### Client Missing Baseline

If a client's acknowledged tick is not in our history (e.g., due to packet loss or late join):
- Send full snapshot with `baseline_tick = 0`
- Client replaces all entity state
- Future deltas will use new baseline

### Entity Creation

New entities not in baseline are sent with `changed_fields = 0xFFFF`:
- Includes all fields at full precision
- Client creates new entity entry

### Entity Destruction

Destroyed entities are tracked in `removedEntities`:
- Sent as array of entity IDs in snapshot
- Client removes entity from local state

### History Overflow

Old snapshots are automatically purged:
- Maximum 60 frames (1 second) retained
- Prevents unbounded memory growth
- Clients with stale baselines get full snapshots

## Testing

The implementation includes comprehensive tests:

- `TestDeltaCompression.cpp`:
  - Full snapshot encoding/decoding
  - Delta against baseline
  - Position variable-length encoding
  - Bandwidth reduction validation
  - Edge cases (large entity count, corrupt data)
  - Benchmarks for performance

Run tests with:
```bash
cd build
ctest -R delta --verbose
```

## Future Improvements

1. **Interest Management**: Only include entities within AOI radius
2. **LOD for Distant Entities**: Reduce update rate and precision for far entities
3. **Predictive Compression**: Use client prediction to further reduce delta size
4. **Huffman Coding**: Add entropy coding for frequently occurring values

## References

- [FlatBuffers Schema](../shared/proto/game_protocol.fbs) - Protocol definition
- [Source: NetworkManager.cpp](../../src/server/src/netcode/NetworkManager.cpp) - Implementation
- [Source: ZoneServer.cpp](../../src/server/src/zones/ZoneServer.cpp) - Integration
