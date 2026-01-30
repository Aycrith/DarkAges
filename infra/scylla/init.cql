-- DarkAges MMO - ScyllaDB Schema Initialization
-- Run this script to create keyspaces and tables

-- ============================================================================
-- Keyspace Creation
-- ============================================================================

CREATE KEYSPACE IF NOT EXISTS darkages
WITH replication = {
    'class': 'SimpleStrategy',
    'replication_factor': 1
};

USE darkages;

-- ============================================================================
-- Combat Events (Time-series data with TTL)
-- ============================================================================

CREATE TABLE IF NOT EXISTS combat_events (
    day_bucket text,
    timestamp timestamp,
    event_id uuid,
    attacker_id bigint,
    target_id bigint,
    damage int,
    hit_type text,
    ability_name text,
    zone_id int,
    position_x double,
    position_y double,
    position_z double,
    server_instance text,
    shard_id int,
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy', 
                    'compaction_window_unit': 'DAYS',
                    'compaction_window_size': 1}
  AND default_time_to_live = 2592000;  -- 30 days

-- Indexes for querying by player
CREATE INDEX IF NOT EXISTS idx_combat_attacker ON combat_events(attacker_id);
CREATE INDEX IF NOT EXISTS idx_combat_target ON combat_events(target_id);
CREATE INDEX IF NOT EXISTS idx_combat_zone ON combat_events(zone_id);

-- ============================================================================
-- Player Combat Stats (Counters for high-write throughput)
-- ============================================================================

CREATE TABLE IF NOT EXISTS player_stats (
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

-- ============================================================================
-- Player Session History
-- ============================================================================

CREATE TABLE IF NOT EXISTS player_sessions (
    player_id bigint,
    session_start timestamp,
    session_end timestamp,
    zone_id int,
    kills int,
    deaths int,
    assists int,
    damage_dealt bigint,
    damage_taken bigint,
    playtime_minutes int,
    client_version text,
    server_instance text,
    session_id uuid,
    PRIMARY KEY ((player_id), session_start)
) WITH CLUSTERING ORDER BY (session_start DESC)
  AND compaction = {'class': 'LeveledCompactionStrategy'}
  AND default_time_to_live = 7776000;  -- 90 days

-- ============================================================================
-- Player Profiles
-- ============================================================================

CREATE TABLE IF NOT EXISTS player_profiles (
    player_id bigint PRIMARY KEY,
    username text,
    email text,
    created_at timestamp,
    last_login timestamp,
    level int,
    experience bigint,
    total_playtime_minutes bigint,
    preferred_class tinyint,
    settings blob,
    is_banned boolean,
    ban_reason text,
    ban_expires timestamp,
    client_versions set<text>,
    last_known_ip inet
);

CREATE INDEX IF NOT EXISTS idx_player_username ON player_profiles(username);
CREATE INDEX IF NOT EXISTS idx_player_email ON player_profiles(email);

-- ============================================================================
-- Daily Leaderboards
-- ============================================================================

CREATE TABLE IF NOT EXISTS leaderboard_daily (
    category text,
    day text,
    player_id bigint,
    player_name text,
    score bigint,
    PRIMARY KEY ((category, day), score, player_id)
) WITH CLUSTERING ORDER BY (score DESC, player_id ASC)
  AND default_time_to_live = 7776000;  -- 90 days

-- ============================================================================
-- All-time Leaderboards
-- ============================================================================

CREATE TABLE IF NOT EXISTS leaderboard_alltime (
    category text PRIMARY KEY,
    entries list<frozen<tuple<bigint, text, bigint>>>
);

-- ============================================================================
-- Zone Performance Metrics
-- ============================================================================

CREATE TABLE IF NOT EXISTS zone_metrics (
    zone_id int,
    hour_bucket timestamp,
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
  AND default_time_to_live = 604800;  -- 7 days

-- ============================================================================
-- Audit Log
-- ============================================================================

CREATE TABLE IF NOT EXISTS audit_log (
    day_bucket text,
    timestamp timestamp,
    event_id uuid,
    event_type text,
    player_id bigint,
    player_ip inet,
    details blob,
    severity text,
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy',
                    'compaction_window_unit': 'DAYS',
                    'compaction_window_size': 1}
  AND default_time_to_live = 15552000;  -- 180 days

-- ============================================================================
-- Entity State Snapshots (for zone handoff)
-- ============================================================================

CREATE TABLE IF NOT EXISTS entity_snapshots (
    entity_id bigint,
    zone_id int,
    snapshot_time timestamp,
    position_x double,
    position_y double,
    position_z double,
    velocity_x double,
    velocity_y double,
    velocity_z double,
    health int,
    max_health int,
    team_id int,
    inventory blob,
    PRIMARY KEY ((entity_id), snapshot_time)
) WITH CLUSTERING ORDER BY (snapshot_time DESC)
  AND default_time_to_live = 3600;  -- 1 hour (temporary storage)

-- ============================================================================
-- Anti-Cheat Events
-- ============================================================================

CREATE TABLE IF NOT EXISTS anticheat_events (
    day_bucket text,
    timestamp timestamp,
    event_id uuid,
    player_id bigint,
    event_type text,
    severity text,
    details blob,
    action_taken text,
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy',
                    'compaction_window_unit': 'DAYS',
                    'compaction_window_size': 1}
  AND default_time_to_live = 7776000;  -- 90 days

CREATE INDEX IF NOT EXISTS idx_anticheat_player ON anticheat_events(player_id);

-- ============================================================================
-- System Events
-- ============================================================================

CREATE TABLE IF NOT EXISTS system_events (
    day_bucket text,
    timestamp timestamp,
    event_id uuid,
    event_type text,
    server_instance text,
    zone_id int,
    severity text,
    message text,
    details blob,
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = {'class': 'TimeWindowCompactionStrategy',
                    'compaction_window_unit': 'DAYS',
                    'compaction_window_size': 1}
  AND default_time_to_live = 2592000;  -- 30 days

-- ============================================================================
-- Verification
-- ============================================================================

DESCRIBE TABLES;
