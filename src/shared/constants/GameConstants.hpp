#pragma once

// [ALL-AGENTS] Shared constants between client and server
// This file is included in both C++ server and GDScript/C# client

#include <cstdint>

namespace DarkAges {
namespace SharedConstants {

// Protocol version - must match between client and server
inline constexpr uint32_t PROTOCOL_VERSION = 1;

// Default ports
inline constexpr uint16_t DEFAULT_GAME_PORT = 7777;
inline constexpr uint16_t DEFAULT_MONITORING_PORT = 7778;

// Tick rates
inline constexpr uint32_t SERVER_TICK_RATE = 60;
inline constexpr uint32_t CLIENT_INPUT_RATE = 60;
inline constexpr uint32_t SERVER_SNAPSHOT_RATE = 20;

// Physics constants (must match exactly between client and server)
inline constexpr float PLAYER_MAX_SPEED = 6.0f;
inline constexpr float PLAYER_SPRINT_MULTIPLIER = 1.5f;
inline constexpr float PLAYER_ACCELERATION = 10.0f;
inline constexpr float PLAYER_DECELERATION = 8.0f;
inline constexpr float PLAYER_ROTATION_SPEED = 720.0f;

// World bounds
inline constexpr float WORLD_MIN_X = -5000.0f;
inline constexpr float WORLD_MAX_X = 5000.0f;
inline constexpr float WORLD_MIN_Z = -5000.0f;
inline constexpr float WORLD_MAX_Z = 5000.0f;

// Interest management (AOI)
inline constexpr float AOI_RADIUS_NEAR = 50.0f;
inline constexpr float AOI_RADIUS_FAR = 200.0f;

// Network limits
inline constexpr uint32_t MAX_PACKET_SIZE = 1400;  // MTU-safe UDP packet
inline constexpr uint32_t MAX_ENTITIES_PER_SNAPSHOT = 256;

} // namespace SharedConstants
} // namespace DarkAges
