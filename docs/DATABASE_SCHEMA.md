# DarkAges MMO - Database Schema Specification

**Version:** 1.0  
**Scope:** Redis Hot-State & ScyllaDB Persistence  
**Work Packages:** WP-6-2 (Redis), WP-6-3 (ScyllaDB)

---

## 1. Redis Schema (Hot State)

Redis stores ephemeral, fast-access data with TTL (Time To Live). All keys should have expiration to prevent unbounded growth.

### 1.1 Key Naming Conventions

```
{namespace}:{identifier}:{attribute}
```

| Namespace | Purpose | TTL |
|-----------|---------|-----|
| `session` | Player session data | 1 hour |
| `player` | Player-specific state | 2 hours |
| `zone` | Zone-level aggregations | 30 minutes |
| `temp` | Temporary computation data | 5 minutes |

### 1.2 Session Keys

**Key Pattern:** `session:{entity_id}`

**Data Type:** String (JSON)

**Value Format:**
```json
{
  "entity_id": 12345,
  "client_id": "player_abc123",
  "zone_id": 42,
  "login_time": 1704067200000,
  "last_activity": 1704067260000,
  "position": {"x": 1234.5, "y": 0.0, "z": 6789.0},
  "health": 85,
  "max_health": 100,
  "team_id": 1,
  "status": "active"
}
```

**Redis Commands:**
```bash
# Set session with 1-hour TTL
SET session:12345 '{"entity_id":12345,...}' EX 3600

# Get session
GET session:12345

# Update last activity
JSON.SET session:12345 $.last_activity 1704067260000

# Delete on logout
DEL session:12345
```

### 1.3 Zone Membership

**Key Pattern:** `zone:{zone_id}:players`

**Data Type:** Set

**Purpose:** Track which players are in each zone for fast lookups

**Redis Commands:**
```bash
# Add player to zone
SADD zone:42:players 12345

# Remove player from zone
SREM zone:42:players 12345

# Get all players in zone
SMEMBERS zone:42:players

# Count players in zone
SCARD zone:42:players

# Check if player is in zone
SISMEMBER zone:42:players 12345
```

### 1.4 Player Position Cache

**Key Pattern:** `player:{entity_id}:pos`

**Data Type:** String (comma-separated values)

**Value Format:** `x,y,z,timestamp`

**Purpose:** Fast position lookups for spatial queries

**Redis Commands:**
```bash
# Set position
SET player:12345:pos "1234.5,0.0,6789.0,1704067260000"

# Get position
GET player:12345:pos

# Set with 2-minute TTL (positions stale quickly)
SET player:12345:pos "1234.5,0.0,6789.0,1704067260000" EX 120
```

### 1.5 Player Zone Assignment

**Key Pattern:** `player:{entity_id}:zone`

**Data Type:** String

**Purpose:** Track which zone a player is currently in

**Redis Commands:**
```bash
# Set zone
SET player:12345:zone 42

# Get zone
GET player:12345:zone

# Delete on zone transfer
DEL player:12345:zone
```

### 1.6 Cross-Zone Pub/Sub Channels

**Channel Pattern:** `channel:zone:{zone_id}`

**Purpose:** Inter-zone communication for entity migration

**Message Format:**
```json
{
  "message_type": "entity_entering|entity_leaving|broadcast|handoff_complete",
  "source_zone": 42,
  "target_zone": 43,
  "timestamp": 1704067260000,
  "payload": {
    // Message-specific data
  }
}
```

**Redis Commands:**
```bash
# Subscribe to zone channel
SUBSCRIBE channel:zone:42

# Publish message to zone
PUBLISH channel:zone:42 '{"message_type":"entity_entering",...}'

# Unsubscribe
UNSUBSCRIBE channel:zone:42
```

### 1.7 Connection Pool Tracking

**Key Pattern:** `internal:conn:{connection_id}`

**Data Type:** Hash

**Purpose:** Track connection metadata

**Fields:**
- `entity_id`: Associated player entity
- `connect_time`: Connection timestamp
- `ip_address`: Client IP
- `client_version`: Protocol version

### 1.8 Rate Limiting (Sliding Window)

**Key Pattern:** `ratelimit:{entity_id}:{action}`

**Data Type:** Sorted Set (ZSET)

**Purpose:** Implement sliding window rate limiting

**Redis Commands:**
```bash
# Add event to window
ZADD ratelimit:12345:attack 1704067260000 "event_id"

# Remove old events (sliding window)
ZREMRANGEBYSCORE ratelimit:12345:attack 0 1704067200000

# Count events in window
ZCARD ratelimit:12345:attack

# Set TTL on key
EXPIRE ratelimit:12345:attack 60
```

### 1.9 Complete Redis Schema Summary

| Key Pattern | Type | TTL | Purpose |
|-------------|------|-----|---------|
| `session:{id}` | String (JSON) | 3600s | Player session |
| `zone:{id}:players` | Set | 1800s | Zone membership |
| `player:{id}:pos` | String | 120s | Position cache |
| `player:{id}:zone` | String | 7200s | Zone assignment |
| `channel:zone:{id}` | Pub/Sub | N/A | Inter-zone messaging |
| `internal:conn:{id}` | Hash | 3600s | Connection metadata |
| `ratelimit:{id}:{action}` | ZSET | 60s | Rate limiting |
| `temp:spatial:query:{id}` | String | 300s | Spatial query cache |

---

## 2. ScyllaDB Schema (Persistence)

ScyllaDB stores permanent data with time-series optimization. Uses Cassandra Query Language (CQL).

### 2.1 Keyspace Definition

```sql
-- Create keyspace with NetworkTopologyStrategy for production
CREATE KEYSPACE IF NOT EXISTS darkages
WITH replication = {
    'class': 'NetworkTopologyStrategy',
    'datacenter1': 3
};

-- For single-node development
CREATE KEYSPACE IF NOT EXISTS darkages_dev
WITH replication = {
    'class': 'SimpleStrategy',
    'replication_factor': 1
};

USE darkages;
```

### 2.2 Combat Events Table

**Purpose:** Log all combat interactions for analytics and replays

```sql
CREATE TABLE IF NOT EXISTS combat_events (
    -- Partition key: day bucket for time-series distribution
    day_bucket text,                    -- "2024-01-01"
    
    -- Clustering columns for ordering
    timestamp timestamp,
    event_id uuid,
    
    -- Event data
    attacker_id bigint,
    target_id bigint,
    damage int,
    hit_type text,                      -- 'normal', 'critical', 'blocked', 'missed'
    ability_name text,
    
    -- Context
    zone_id int,
    position_x double,
    position_y double,
    position_z double,
    
    -- Metadata
    server_instance text,
    shard_id int,
    
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy', 
                    'compaction_window_unit': 'DAYS',
                    'compaction_window_size': 1}
  AND ttl = 2592000;  -- 30 days TTL
```

**Access Patterns:**
```sql
-- Query events by time range
SELECT * FROM combat_events 
WHERE day_bucket = '2024-01-01' 
  AND timestamp > '2024-01-01T10:00:00Z' 
  AND timestamp < '2024-01-01T11:00:00Z';

-- Query events by attacker
CREATE INDEX IF NOT EXISTS idx_combat_attacker ON combat_events(attacker_id);

SELECT * FROM combat_events 
WHERE attacker_id = 12345 
  AND day_bucket = '2024-01-01';

-- Query events by target
CREATE INDEX IF NOT EXISTS idx_combat_target ON combat_events(target_id);

SELECT * FROM combat_events 
WHERE target_id = 12345 
  AND day_bucket = '2024-01-01';
```

### 2.3 Player Combat Stats (Counters)

**Purpose:** Aggregate player statistics (counters for high-write throughput)

```sql
CREATE TABLE IF NOT EXISTS player_stats (
    player_id bigint PRIMARY KEY,
    
    -- Counter columns (increment only)
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
    
    -- Last updated timestamp (not a counter)
    last_updated timestamp
);

-- Update stats (counters use UPDATE, not INSERT)
UPDATE player_stats 
SET total_kills = total_kills + 1,
    total_damage_dealt = total_damage_dealt + 150,
    last_updated = toTimestamp(now())
WHERE player_id = 12345;
```

### 2.4 Player Session History

**Purpose:** Record completed play sessions

```sql
CREATE TABLE IF NOT EXISTS player_sessions (
    player_id bigint,
    session_start timestamp,
    session_end timestamp,
    
    -- Session stats
    zone_id int,
    kills int,
    deaths int,
    damage_dealt bigint,
    damage_taken bigint,
    playtime_minutes int,
    
    -- Metadata
    client_version text,
    server_instance text,
    session_id uuid,
    
    PRIMARY KEY ((player_id), session_start)
) WITH CLUSTERING ORDER BY (session_start DESC)
  AND compaction = {'class': 'LeveledCompactionStrategy'}
  AND ttl = 7776000;  -- 90 days
```

### 2.5 Leaderboard Tables

**Purpose:** Pre-aggregated leaderboards for fast queries

```sql
-- Daily leaderboards
CREATE TABLE IF NOT EXISTS leaderboard_daily (
    category text,          -- 'kills', 'damage', 'wins', 'playtime'
    day text,               -- '2024-01-01'
    player_id bigint,
    player_name text,
    score bigint,
    
    PRIMARY KEY ((category, day), score, player_id)
) WITH CLUSTERING ORDER BY (score DESC, player_id ASC)
  AND ttl = 7776000;  -- 90 days

-- All-time leaderboards (materialized view approach)
CREATE TABLE IF NOT EXISTS leaderboard_alltime (
    category text PRIMARY KEY,
    entries list<frozen<tuple<bigint, text, bigint>>>  -- [(player_id, name, score), ...]
);
```

**Access Patterns:**
```sql
-- Get top 100 for day
SELECT * FROM leaderboard_daily 
WHERE category = 'kills' AND day = '2024-01-01' 
LIMIT 100;

-- Get all-time leaderboard
SELECT * FROM leaderboard_alltime WHERE category = 'kills';
```

### 2.6 Player Profiles

**Purpose:** Persistent player data

```sql
CREATE TABLE IF NOT EXISTS player_profiles (
    player_id bigint PRIMARY KEY,
    
    -- Account info
    username text,
    email text,
    created_at timestamp,
    last_login timestamp,
    
    -- Progression
    level int,
    experience bigint,
    total_playtime_minutes bigint,
    
    -- Preferences
    preferred_class tinyint,
    settings blob,          -- JSON blob for settings
    
    -- Status
    is_banned boolean,
    ban_reason text,
    ban_expires timestamp,
    
    -- Metadata
    client_versions set<text>,  -- All versions used
    last_known_ip inet
);

-- Indexes for lookups
CREATE INDEX IF NOT EXISTS idx_player_username ON player_profiles(username);
CREATE INDEX IF NOT EXISTS idx_player_email ON player_profiles(email);
```

### 2.7 Zone Performance Metrics

**Purpose:** Store zone server performance metrics for analysis

```sql
CREATE TABLE IF NOT EXISTS zone_metrics (
    zone_id int,
    hour_bucket timestamp,  -- Truncated to hour
    
    -- Aggregates
    avg_player_count int,
    max_player_count int,
    min_player_count int,
    
    avg_tick_time_ms float,
    max_tick_time_ms float,
    p99_tick_time_ms float,
    
    total_packets_sent bigint,
    total_packets_received bigint,
    total_bytes_sent bigint,
    total_bytes_received bigint,
    
    server_instance text,
    
    PRIMARY KEY ((zone_id), hour_bucket)
) WITH CLUSTERING ORDER BY (hour_bucket DESC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy',
                    'compaction_window_unit': 'HOURS',
                    'compaction_window_size': 1}
  AND ttl = 604800;  -- 7 days
```

### 2.8 Audit Log

**Purpose:** Security and compliance logging

```sql
CREATE TABLE IF NOT EXISTS audit_log (
    day_bucket text,
    timestamp timestamp,
    event_id uuid,
    
    event_type text,        -- 'login', 'logout', 'kick', 'ban', 'suspicious'
    player_id bigint,
    player_ip inet,
    
    details blob,           -- JSON blob
    severity text,          -- 'info', 'warning', 'critical'
    
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy',
                    'compaction_window_unit': 'DAYS',
                    'compaction_window_size': 1}
  AND ttl = 15552000;  -- 180 days (6 months)
```

---

## 3. Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           CLIENT                                        │
│                         (Godot 4.x)                                     │
└────────────────────────────────┬────────────────────────────────────────┘
                                 │ UDP (60Hz input, 20Hz snapshot)
                                 ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         ZONE SERVER                                     │
│                         (C++/EnTT)                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │   Network    │  │   Physics    │  │   Combat     │  │    Zones     │ │
│  │   (GNS)      │  │   (Spatial)  │  │   (Lag Comp) │  │  (Sharding)  │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘ │
└─────────┼─────────────────┼─────────────────┼─────────────────┼─────────┘
          │                 │                 │                 │
          │                 │                 │                 │
          ▼                 │                 │                 │
┌──────────────────────┐   │                 │                 │
│       REDIS          │   │                 │                 │
│    (Hot State)       │◄──┘                 │                 │
│  - Session cache     │                     │                 │
│  - Position cache    │                     │                 │
│  - Zone membership   │                     │                 │
│  - Pub/sub           │                     │                 │
└──────────────────────┘                     │                 │
                                             │                 │
                                             ▼                 │
                              ┌──────────────────────┐        │
                              │      SCYLLADB        │        │
                              │    (Persistence)     │◄───────┘
                              │  - Combat events     │
                              │  - Player stats      │
                              │  - Session history   │
                              │  - Leaderboards      │
                              └──────────────────────┘
```

---

## 4. Performance Considerations

### 4.1 Redis Performance Targets

| Operation | Target Latency | Max Throughput |
|-----------|----------------|----------------|
| GET session | < 1ms | 50k ops/sec |
| SET session | < 1ms | 30k ops/sec |
| Pub/Sub publish | < 1ms | 100k msg/sec |
| SMEMBERS zone | < 5ms | 10k ops/sec |

### 4.2 ScyllaDB Performance Targets

| Operation | Target Latency | Max Throughput |
|-----------|----------------|----------------|
| INSERT combat event | < 10ms (p99) | 100k writes/sec |
| UPDATE counters | < 5ms (p99) | 200k writes/sec |
| SELECT by partition | < 50ms | 10k reads/sec |
| SELECT with index | < 100ms | 5k reads/sec |

### 4.3 Write Buffering

**ScyllaDB Async Writes:**
- Use async C++ driver
- Batch writes when possible (max 1000 per batch)
- Use prepared statements for repeated queries
- Set appropriate consistency levels (LOCAL_QUORUM for writes)

**Redis Pipeline:**
- Use pipelines for multiple operations
- Batch zone membership updates
- Use Lua scripts for atomic operations

---

## 5. Migration & Schema Evolution

### 5.1 Adding New Columns

ScyllaDB supports adding columns:
```sql
ALTER TABLE combat_events ADD ability_name text;
```

### 5.2 Creating New Tables

Always use `IF NOT EXISTS`:
```sql
CREATE TABLE IF NOT EXISTS new_table (
    -- columns
) WITH -- options
```

### 5.3 Deprecating Fields

1. Stop writing to field
2. Mark as deprecated in documentation
3. Remove after 30 days (TTL will clean up)

---

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-30 | AI Coordinator | Initial schema specification |

### Related Documents
- `docs/API_CONTRACTS.md` - Interface contracts
- `WP_IMPLEMENTATION_GUIDE.md` - Implementation details
