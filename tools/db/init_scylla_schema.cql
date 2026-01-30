-- ============================================================================
-- DarkAges MMO - ScyllaDB Schema Initialization
-- Phase 3D: Combat Logging and Analytics
--
-- Usage:
--   cqlsh -f init_scylla_schema.cql
-- Or via Docker:
--   docker exec -i darkages-scylla cqlsh < init_scylla_schema.cql
-- ============================================================================

-- Create keyspace for DarkAges data
-- Using SimpleStrategy for single-datacenter deployments
-- For production multi-DC, use NetworkTopologyStrategy
CREATE KEYSPACE IF NOT EXISTS darkages 
WITH replication = {'class': 'SimpleStrategy', 'replication_factor': 1};

USE darkages;

-- ============================================================================
-- COMBAT EVENTS TABLE
-- ============================================================================
-- Stores all combat events: damage, heals, kills, deaths
-- High write throughput required - partition by time buckets for efficiency

CREATE TABLE IF NOT EXISTS darkages.combat_events (
    event_id uuid PRIMARY KEY,
    event_time timestamp,
    zone_id int,
    attacker_id bigint,
    target_id bigint,
    event_type text,  -- 'damage', 'heal', 'kill', 'death', 'assist'
    damage_amount int,
    is_critical boolean,
    weapon_type text,
    position_x float,
    position_y float,
    position_z float,
    server_tick bigint
);

-- Secondary index for querying events by attacker
CREATE INDEX IF NOT EXISTS idx_combat_events_attacker 
ON darkages.combat_events (attacker_id);

-- Secondary index for querying events by target
CREATE INDEX IF NOT EXISTS idx_combat_events_target 
ON darkages.combat_events (target_id);

-- Secondary index for querying events by zone
CREATE INDEX IF NOT EXISTS idx_combat_events_zone 
ON darkages.combat_events (zone_id);

-- Secondary index for event type filtering
CREATE INDEX IF NOT EXISTS idx_combat_events_type 
ON darkages.combat_events (event_type);

-- ============================================================================
-- COMBAT EVENTS BY TIME TABLE
-- ============================================================================
-- Optimized for time-range queries with partitioning
-- Use this for analytics queries, not the main combat_events table

CREATE TABLE IF NOT EXISTS darkages.combat_events_by_time (
    time_bucket text,  -- Format: YYYY-MM-DD for daily buckets
    event_time timestamp,
    event_id uuid,
    zone_id int,
    attacker_id bigint,
    target_id bigint,
    event_type text,
    damage_amount int,
    is_critical boolean,
    weapon_type text,
    position_x float,
    position_y float,
    position_z float,
    server_tick bigint,
    PRIMARY KEY (time_bucket, event_time, event_id)
) WITH CLUSTERING ORDER BY (event_time DESC, event_id ASC);

-- ============================================================================
-- PLAYER COMBAT STATS TABLE
-- ============================================================================
-- Daily aggregated stats per player
-- Used for leaderboards, player profiles, match history

CREATE TABLE IF NOT EXISTS darkages.player_combat_stats (
    player_id bigint,
    session_date date,
    kills int,
    deaths int,
    damage_dealt bigint,
    damage_taken bigint,
    assists int,
    longest_kill_streak int,
    current_kill_streak int,
    PRIMARY KEY (player_id, session_date)
) WITH CLUSTERING ORDER BY (session_date DESC);

-- ============================================================================
-- KILL FEED TABLE
-- ============================================================================
-- Optimized for kill feed queries - latest kills first
-- Used for in-game kill announcements and zone activity feeds

CREATE TABLE IF NOT EXISTS darkages.kill_feed (
    time_bucket text,  -- Format: YYYY-MM-DD
    kill_time timestamp,
    zone_id int,
    killer_id bigint,
    victim_id bigint,
    weapon_type text,
    killer_name text,
    victim_name text,
    PRIMARY KEY (time_bucket, kill_time, killer_id)
) WITH CLUSTERING ORDER BY (kill_time DESC, killer_id ASC);

-- Secondary index for zone-specific kill feeds
CREATE INDEX IF NOT EXISTS idx_kill_feed_zone 
ON darkages.kill_feed (zone_id);

-- ============================================================================
-- ZONE COMBAT STATISTICS TABLE
-- ============================================================================
-- Aggregated combat statistics per zone per hour
-- Used for zone balancing and activity monitoring

CREATE TABLE IF NOT EXISTS darkages.zone_combat_stats (
    zone_id int,
    hour_bucket timestamp,  -- Truncated to hour
    total_kills int,
    total_deaths int,
    total_damage_dealt bigint,
    unique_attackers int,
    unique_victims int,
    PRIMARY KEY (zone_id, hour_bucket)
) WITH CLUSTERING ORDER BY (hour_bucket DESC);

-- ============================================================================
-- PLAYER SESSIONS TABLE
-- ============================================================================
-- Tracks player session data for analytics
-- Links entity activity to persistent player accounts

CREATE TABLE IF NOT EXISTS darkages.player_sessions (
    player_id bigint,
    session_start timestamp,
    session_end timestamp,
    zone_id int,
    duration_seconds int,
    kills int,
    deaths int,
    damage_dealt bigint,
    damage_taken bigint,
    PRIMARY KEY (player_id, session_start)
) WITH CLUSTERING ORDER BY (session_start DESC);

-- ============================================================================
-- WEAPON USAGE STATISTICS TABLE
-- ============================================================================
-- Tracks weapon usage for game balance analytics

CREATE TABLE IF NOT EXISTS darkages.weapon_stats (
    weapon_type text,
    date date,
    times_used bigint,
    total_damage bigint,
    kills int,
    avg_damage_per_hit double,
    PRIMARY KEY (weapon_type, date)
) WITH CLUSTERING ORDER BY (date DESC);

-- ============================================================================
-- MATERIALIZED VIEWS FOR COMMON QUERIES
-- ============================================================================

-- Top killers by date (for leaderboards)
CREATE MATERIALIZED VIEW IF NOT EXISTS darkages.player_kills_by_date AS
SELECT player_id, session_date, kills
FROM darkages.player_combat_stats
WHERE player_id IS NOT NULL AND session_date IS NOT NULL AND kills IS NOT NULL
PRIMARY KEY (session_date, kills, player_id)
WITH CLUSTERING ORDER BY (kills DESC, player_id ASC);

-- ============================================================================
-- BATCH INSERT EXAMPLE
-- ============================================================================
-- Use UNLOGGED batches for high-throughput writes
-- Only batch operations within the same partition key for best performance

/*
BEGIN UNLOGGED BATCH
    INSERT INTO combat_events (event_id, event_time, zone_id, attacker_id, target_id, 
                               event_type, damage_amount, is_critical, weapon_type,
                               position_x, position_y, position_z, server_tick)
    VALUES (uuid(), toTimestamp(now()), 1, 1001, 1002, 'damage', 250, false, 'sword', 100.5, 0.0, 200.3, 12345);
    
    INSERT INTO combat_events (event_id, event_time, zone_id, attacker_id, target_id, 
                               event_type, damage_amount, is_critical, weapon_type,
                               position_x, position_y, position_z, server_tick)
    VALUES (uuid(), toTimestamp(now()), 1, 1001, 1002, 'damage', 300, true, 'sword', 101.0, 0.0, 200.5, 12346);
APPLY BATCH;
*/

-- ============================================================================
-- SAMPLE QUERIES
-- ============================================================================

-- Get player's combat stats for today
-- SELECT * FROM player_combat_stats WHERE player_id = 1001 AND session_date = toDate(now());

-- Get kill feed for zone (last 50 kills)
-- SELECT * FROM kill_feed WHERE time_bucket = '2024-01-15' AND zone_id = 1 LIMIT 50;

-- Get top killers today (using materialized view)
-- SELECT * FROM player_kills_by_date WHERE session_date = toDate(now()) LIMIT 10;

-- Get player's recent combat events
-- SELECT * FROM combat_events WHERE attacker_id = 1001 LIMIT 100;

-- ============================================================================
-- TUNING RECOMMENDATIONS
-- ============================================================================
-- 
-- 1. For high-throughput writes, use:
--    - UNLOGGED batches for same-partition writes
--    - Asynchronous writes from application
--    - Prepared statements for repeated queries
--
-- 2. For time-series data, consider:
--    - Time-based partitioning (TTL old data)
--    - Compaction strategy: TimeWindowCompactionStrategy
--
-- 3. For analytics workloads:
--    - Use Spark or Presto for complex aggregations
--    - Consider denormalized tables for common queries
--
-- ============================================================================

-- Grant permissions if needed (adjust username)
-- GRANT ALL ON KEYSPACE darkages TO 'darkages_user';
