#pragma once

#include <cstdint>
#include <chrono>
#include <cstddef>

// [ALL-AGENTS] Global constants for DarkAges MMO Server
// All magic numbers MUST be defined here, not scattered in code

namespace DarkAges {
namespace Constants {

// ============================================================================
// NETWORK CONSTANTS
// ============================================================================

// [NETWORK_AGENT] Tick rate and timing
inline constexpr uint32_t TICK_RATE_HZ = 60;
inline constexpr float DT_SECONDS = 1.0f / TICK_RATE_HZ;
inline constexpr auto DT_MILLISECONDS = std::chrono::milliseconds(1000 / TICK_RATE_HZ);
inline constexpr uint32_t TICK_BUDGET_US = 16666;  // 16.666ms = 60Hz

// [NETWORK_AGENT] Snapshot and replication rates
inline constexpr uint32_t SNAPSHOT_RATE_HZ = 20;  // Baseline snapshot rate
inline constexpr uint32_t INPUT_RATE_HZ = 60;     // Client input rate

// [NETWORK_AGENT] Bandwidth budgets (per player)
inline constexpr uint32_t MAX_DOWNSTREAM_BYTES_PER_SEC = 20480;  // 20 KB/s
inline constexpr uint32_t MAX_UPSTREAM_BYTES_PER_SEC = 2048;     // 2 KB/s

// [NETWORK_AGENT] Protocol constants
inline constexpr uint16_t DEFAULT_SERVER_PORT = 7777;
inline constexpr uint16_t DEFAULT_MONITORING_PORT = 7778;
inline constexpr uint32_t PROTOCOL_VERSION = 1;

// [NETWORK_AGENT] Connection quality
inline constexpr uint32_t MAX_RTT_MS = 300;       // Kick players above this
inline constexpr uint32_t MAX_PACKET_LOSS_PCT = 10;  // Warning threshold

// ============================================================================
// PHYSICS CONSTANTS
// ============================================================================

// [PHYSICS_AGENT] Fixed-point arithmetic
using Fixed = int32_t;
inline constexpr Fixed FIXED_PRECISION = 1000;  // 1000 units = 1.0
inline constexpr float FIXED_TO_FLOAT = 1.0f / FIXED_PRECISION;
inline constexpr float FLOAT_TO_FIXED = static_cast<float>(FIXED_PRECISION);

// [PHYSICS_AGENT] World bounds (meters)
inline constexpr float WORLD_MIN_X = -5000.0f;
inline constexpr float WORLD_MAX_X = 5000.0f;
inline constexpr float WORLD_MIN_Y = -100.0f;
inline constexpr float WORLD_MAX_Y = 500.0f;
inline constexpr float WORLD_MIN_Z = -5000.0f;
inline constexpr float WORLD_MAX_Z = 5000.0f;

// [PHYSICS_AGENT] Movement constants
inline constexpr float MAX_PLAYER_SPEED = 6.0f;        // m/s
inline constexpr float SPRINT_SPEED_MULTIPLIER = 1.5f;
inline constexpr float MAX_SPRINT_SPEED = MAX_PLAYER_SPEED * SPRINT_SPEED_MULTIPLIER;
inline constexpr float ACCELERATION = 10.0f;           // m/s^2
inline constexpr float DECELERATION = 8.0f;            // m/s^2
inline constexpr float ROTATION_SPEED = 720.0f;        // degrees/s
inline constexpr float JUMP_VELOCITY = 8.0f;           // m/s
inline constexpr float GRAVITY = 20.0f;                // m/s^2

// [PHYSICS_AGENT] Spatial hashing
inline constexpr float SPATIAL_HASH_CELL_SIZE = 10.0f;  // meters
inline constexpr uint32_t MAX_ENTITIES_PER_CELL = 500;

// [PHYSICS_AGENT] Collision
inline constexpr float PLAYER_COLLISION_RADIUS = 0.5f;
inline constexpr float PLAYER_HEIGHT = 1.8f;
inline constexpr float MAX_COLLISION_CHECKS_PER_FRAME = 1000;

// ============================================================================
// SPATIAL SHARDING CONSTANTS
// ============================================================================

// [ZONE_AGENT] Zone configuration
inline constexpr uint32_t MAX_ZONES = 256;
inline constexpr uint32_t MAX_PLAYERS_PER_ZONE = 400;
inline constexpr uint32_t MAX_ENTITIES_PER_ZONE = 4000;

// [ZONE_AGENT] Aura projection buffer (overlapping zones)
inline constexpr float AURA_BUFFER_METERS = 50.0f;
inline constexpr float ZONE_HANDOFF_THRESHOLD = 10.0f;  // Start handoff at this distance

// [ZONE_AGENT] Interest management (AOI)
inline constexpr float AOI_RADIUS_NEAR = 50.0f;    // 100% update rate
inline constexpr float AOI_RADIUS_MID = 100.0f;    // 50% update rate
inline constexpr float AOI_RADIUS_FAR = 200.0f;    // 10% update rate

// ============================================================================
// COMBAT CONSTANTS
// ============================================================================

// [COMBAT_AGENT] Lag compensation
inline constexpr uint32_t LAG_COMPENSATION_HISTORY_MS = 2000;  // 2 seconds
inline constexpr uint32_t MAX_REWIND_MS = 500;  // Don't rewind more than this

// [COMBAT_AGENT] Health and damage
inline constexpr int32_t MAX_HEALTH = 10000;  // Fixed-point: 1000 = 100 health
inline constexpr int32_t DEFAULT_HEALTH = 10000;

// ============================================================================
// DATABASE CONSTANTS
// ============================================================================

// [DATABASE_AGENT] Redis configuration
inline constexpr uint32_t REDIS_DEFAULT_PORT = 6379;
inline constexpr uint32_t REDIS_CONNECTION_TIMEOUT_MS = 100;
inline constexpr uint32_t REDIS_COMMAND_TIMEOUT_MS = 10;
inline constexpr uint32_t REDIS_KEY_TTL_SECONDS = 300;  // 5 minutes

// [DATABASE_AGENT] ScyllaDB configuration
inline constexpr uint32_t SCYLLA_DEFAULT_PORT = 9042;
inline constexpr uint32_t SCYLLA_WRITE_BATCH_SIZE = 1000;
inline constexpr uint32_t SCYLLA_WRITE_BATCH_INTERVAL_MS = 5000;  // 5 seconds

// [DATABASE_AGENT] Checkpointing
inline constexpr uint32_t PLAYER_SAVE_INTERVAL_MS = 30000;  // 30 seconds
inline constexpr uint32_t ZONE_CHECKPOINT_INTERVAL_MS = 60000;  // 60 seconds

// ============================================================================
// ANTI-CHEAT CONSTANTS
// ============================================================================

// [SECURITY_AGENT] Movement validation
inline constexpr float POSITION_TOLERANCE = 0.5f;  // Meters
inline constexpr float SPEED_TOLERANCE = 1.2f;     // 20% tolerance for lag
inline constexpr uint32_t MAX_INPUTS_PER_SECOND = 60;
inline constexpr uint32_t INPUT_RATE_LIMIT_WINDOW_MS = 1000;

// [SECURITY_AGENT] Teleport detection
inline constexpr float MAX_TELEPORT_DISTANCE = 100.0f;  // Immediate kick
inline constexpr uint32_t SUSPICIOUS_MOVEMENT_THRESHOLD = 3;  // Warnings before kick

// ============================================================================
// MEMORY BUDGETS
// ============================================================================

// [PERFORMANCE_AGENT] Per-Zone Server Memory Limits
inline constexpr size_t MEMORY_PER_PLAYER_KB = 512;           // All components + history buffers
inline constexpr size_t MEMORY_PER_ENTITY_KB = 128;           // NPCs, projectiles
inline constexpr size_t MEMORY_MAX_ENTITIES_PER_ZONE = 4000;
inline constexpr size_t MEMORY_ZONE_BUDGET_GB = 4;            // Hard limit: 4GB per zone

// [PERFORMANCE_AGENT] Memory Pool Sizes
inline constexpr size_t POOL_SMALL_BLOCK_SIZE = 64;           // Small components
inline constexpr size_t POOL_SMALL_BLOCK_COUNT = 10000;       // 640KB
inline constexpr size_t POOL_MEDIUM_BLOCK_SIZE = 256;         // Medium components
inline constexpr size_t POOL_MEDIUM_BLOCK_COUNT = 5000;       // 1.25MB
inline constexpr size_t POOL_LARGE_BLOCK_SIZE = 1024;         // Large components
inline constexpr size_t POOL_LARGE_BLOCK_COUNT = 1000;        // 1MB

// [PERFORMANCE_AGENT] Stack Allocator Size
inline constexpr size_t TEMP_ALLOCATOR_SIZE = 1024 * 1024;    // 1MB per tick

} // namespace Constants
} // namespace DarkAges
