# ScyllaDB Persistence Layer Implementation (WP-6-3)

## Overview

This document describes the implementation of the ScyllaDB persistence layer for the DarkAges MMO server, completed as Work Package WP-6-3.

## Implementation Summary

### Files Created/Modified

1. **`src/server/src/db/ScyllaManager.cpp`** (NEW - 706 lines)
   - Full implementation using cassandra-cpp-driver v2.17.1
   - Async I/O operations with callback-based completion
   - Prepared statements for high-performance queries
   - Batch write support for high-throughput scenarios

2. **`src/server/tests/TestScyllaManager.cpp`** (NEW - 514 lines)
   - Comprehensive unit tests for all ScyllaManager functionality
   - Performance tests for 100k writes/sec target
   - Latency tests for p99 < 10ms requirement
   - Analytics query tests

3. **`src/server/include/db/ScyllaManager.hpp`** (MODIFIED)
   - Added `<atomic>` include for atomic operations tracking
   - Existing interface unchanged for backward compatibility

4. **`CMakeLists.txt`** (MODIFIED)
   - Added `TestScyllaManager.cpp` to test sources list

### Schema Implemented

The implementation creates the following CQL schema on initialization:

```sql
-- Keyspace
CREATE KEYSPACE IF NOT EXISTS darkages
WITH replication = {'class': 'SimpleStrategy', 'replication_factor': 1};

-- Combat Events (time-series with 30-day TTL)
CREATE TABLE IF NOT EXISTS darkages.combat_events (
    day_bucket text,
    timestamp timestamp,
    event_id uuid,
    attacker_id bigint,
    target_id bigint,
    damage int,
    hit_type text,
    event_type text,
    zone_id int,
    position_x double,
    position_y double,
    position_z double,
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy'}
  AND default_time_to_live = 2592000;

-- Secondary indexes for player queries
CREATE INDEX IF NOT EXISTS idx_combat_attacker ON combat_events(attacker_id);
CREATE INDEX IF NOT EXISTS idx_combat_target ON combat_events(target_id);

-- Player Stats (counters for high-write throughput)
CREATE TABLE IF NOT EXISTS darkages.player_stats (
    player_id bigint PRIMARY KEY,
    total_kills counter,
    total_deaths counter,
    total_assists counter,
    total_damage_dealt counter,
    total_damage_taken counter,
    total_damage_blocked counter,
    total_healing_done counter,
    total_playtime_minutes counter,
    total_matches counter,
    total_wins counter,
    last_updated timestamp
);

-- Player Sessions (90-day TTL)
CREATE TABLE IF NOT EXISTS darkages.player_sessions (
    player_id bigint,
    session_start timestamp,
    session_end timestamp,
    zone_id int,
    kills int,
    deaths int,
    damage_dealt bigint,
    damage_taken bigint,
    playtime_minutes int,
    PRIMARY KEY ((player_id), session_start)
) WITH CLUSTERING ORDER BY (session_start DESC)
  AND default_time_to_live = 7776000;

-- Daily Leaderboards (90-day TTL)
CREATE TABLE IF NOT EXISTS darkages.leaderboard_daily (
    category text,
    day text,
    player_id bigint,
    player_name text,
    score bigint,
    PRIMARY KEY ((category, day), score, player_id)
) WITH CLUSTERING ORDER BY (score DESC, player_id ASC)
  AND default_time_to_live = 7776000;
```

### Key Features

#### 1. Async I/O Architecture
- All database operations are non-blocking
- Callback-based completion notification
- Internal pending operation tracking
- Graceful shutdown with pending operation wait

#### 2. Prepared Statements
The following prepared statements are created for optimal performance:
- `insertCombatEvent` - Fast combat event logging
- `updatePlayerStats` - Counter increment operations
- `insertPlayerSession` - Session persistence
- `queryCombatHistoryByAttacker` - Player combat history
- `queryPlayerStats` - Retrieve player statistics
- `queryKillFeed` - Zone kill events

#### 3. Batch Write Support
- Unlogged batches for maximum throughput
- Configurable batch sizes
- Automatic batch timeout handling (5 seconds)

#### 4. Connection Configuration
```cpp
cass_cluster_set_num_threads_io(cluster_, 4);
cass_cluster_set_queue_size_io(cluster_, 10000);
cass_cluster_set_core_connections_per_host(cluster_, 2);
cass_cluster_set_max_connections_per_host(cluster_, 8);
cass_cluster_set_consistency(cluster_, CASS_CONSISTENCY_LOCAL_QUORUM);
```

### API Usage Examples

#### Initialize Connection
```cpp
ScyllaManager scylla;
if (!scylla.initialize("localhost", 9042)) {
    // Handle connection failure
}
```

#### Log Combat Event
```cpp
CombatEvent event{};
event.timestamp = getCurrentTimestamp();
event.attackerId = playerId;
event.targetId = targetId;
event.damageAmount = 500;
event.eventType = "damage";
event.zoneId = currentZone;
event.position = playerPosition;

scylla.logCombatEvent(event, [](bool success) {
    if (!success) {
        // Handle write failure
    }
});
```

#### Batch Log Events
```cpp
std::vector<CombatEvent> events = generateCombatEvents();

scylla.logCombatEventsBatch(events, [](bool success) {
    // Batch complete callback
});
```

#### Update Player Stats
```cpp
PlayerCombatStats stats{};
stats.playerId = playerId;
stats.kills = 5;
stats.deaths = 2;
stats.damageDealt = 5000;
stats.damageTaken = 2000;

scylla.updatePlayerStats(stats, [](bool success) {
    // Update complete
});
```

#### Query Player Stats
```cpp
scylla.getPlayerStats(playerId, sessionDate, 
    [](bool success, const PlayerCombatStats& stats) {
        if (success) {
            // Use stats data
        }
    });
```

#### Get Kill Feed
```cpp
scylla.getKillFeed(zoneId, 10, 
    [](bool success, const std::vector<CombatEvent>& events) {
        if (success) {
            for (const auto& event : events) {
                // Display kill event
            }
        }
    });
```

### Performance Targets

| Metric | Target | Implementation |
|--------|--------|----------------|
| Write Throughput | 100k writes/sec | Unlogged batches, async I/O, 4 I/O threads |
| Write Latency p99 | < 10ms | Prepared statements, connection pooling |
| Read Latency | < 50ms | Partition-based queries, secondary indexes |
| Counter Updates | 200k/sec | Native counter columns, batching |

### Testing

Run the ScyllaDB tests:
```bash
# Build tests
cmake --build build --target darkages_tests

# Run all database tests
./build/darkages_tests "[database]"

# Run performance tests
./build/darkages_tests "[performance]"

# Run Scylla-specific tests
./build/darkages_tests "[scylla]"
```

### Docker Setup for Testing

```yaml
# infra/docker-compose.yml
scylla:
  image: scylladb/scylla:5.4
  ports:
    - "9042:9042"
  command: --smp 1 --memory 1G
```

Start ScyllaDB:
```bash
docker-compose -f infra/docker-compose.yml up scylla
```

### Metrics

The ScyllaManager tracks the following metrics:
- `writesQueued_` - Total writes submitted
- `writesCompleted_` - Successful writes
- `writesFailed_` - Failed writes
- `pendingOperations` - Currently in-flight operations

Access metrics via:
```cpp
uint64_t queued = scylla.getWritesQueued();
uint64_t completed = scylla.getWritesCompleted();
uint64_t failed = scylla.getWritesFailed();
```

### Known Limitations

1. **ALLOW FILTERING**: Some queries use `ALLOW FILTERING` which can impact performance at scale. Materialized views should be considered for production.

2. **Index Creation**: Secondary indexes on `attacker_id` and `target_id` are created but may have performance implications for high write throughput.

3. **Single Day Queries**: Combat event queries are optimized for single-day lookups. Cross-day queries require multiple round trips.

4. **No Built-in Retry**: Failed writes do not automatically retry. Applications should handle failures via callbacks.

### Future Enhancements

1. Materialized views for common query patterns
2. Automatic retry with exponential backoff
3. Circuit breaker pattern for degraded ScyllaDB connectivity
4. Metrics export to Prometheus/Grafana
5. Query result caching for frequently accessed player stats

## Work Package Completion Checklist

- [x] Replace `ScyllaManager_stub.cpp` with cassandra-cpp-driver implementation
- [x] Implement combat event logging schema
- [x] Implement player stats aggregation (counters)
- [x] Implement async write batching for performance
- [x] Unit tests for all functionality
- [x] Performance tests (100k writes/sec target)
- [x] Latency tests (p99 < 10ms target)
- [x] CMakeLists.txt updated with test file

## References

- [DATABASE_SCHEMA.md](DATABASE_SCHEMA.md) - Complete schema specification
- [API_CONTRACTS.md](API_CONTRACTS.md) - Database layer contracts
- [WP_IMPLEMENTATION_GUIDE.md](../WP_IMPLEMENTATION_GUIDE.md) - WP-6-3 section
- [DataStax C++ Driver Docs](https://docs.datastax.com/en/developer/cpp-driver/latest/) - cassandra-cpp-driver documentation
