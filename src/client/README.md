# DarkAges MMO Client

## Overview

The DarkAges Client is a Godot 4.2+ C# implementation featuring:

- **Client-Side Prediction**: Local player movement is predicted immediately and reconciled with server corrections
- **Entity Interpolation**: Remote players are rendered with smooth interpolation between snapshots
- **UDP Networking**: Custom binary protocol over UDP for low-latency gameplay
- **FlatBuffers Serialization**: Efficient, zero-copy serialization for network messages

## Architecture

```
scripts/
├── Main.cs                 # Main game scene controller
└── UI.cs                   # UI controller for connection/debug

src/
├── networking/
│   ├── NetworkManager.cs   # UDP socket, packet sending/receiving
│   └── InputState.cs       # Input serialization structure
├── prediction/
│   ├── PredictedPlayer.cs  # Local player with prediction & reconciliation
│   └── PredictedInput.cs   # Input buffer entry
├── entities/
│   ├── RemotePlayer.cs     # Remote player with interpolation
│   └── RemotePlayerManager.cs  # Spawning/despawning management
├── combat/
│   ├── CombatEventSystem.cs
│   ├── DamageIndicator.cs
│   ├── HitMarker.cs
│   └── DeathCamera.cs
└── GameState.cs            # Global game state singleton
```

## Key Features

### Client-Side Prediction

The `PredictedPlayer` class handles:
1. **Immediate Feedback**: Player movement is applied locally without waiting for server
2. **Input Buffering**: Last 120 inputs are stored (2 seconds at 60Hz)
3. **Reconciliation**: When server correction arrives:
   - Remove acknowledged inputs from buffer
   - Calculate prediction error
   - If error < 0.1m: No correction needed
   - If error < 2.0m: Smoothly interpolate to correct position
   - If error > 2.0m: Snap to server position (possible cheat/desync)
   - Replay all unacknowledged inputs from corrected state

### Entity Interpolation

The `RemotePlayer` class handles:
1. **State Buffering**: Last 10 entity states stored (~166ms)
2. **Render-Time Interpolation**: Renders 100ms behind server for smoothness
3. **Extrapolation**: When buffer runs low, extrapolates from velocity (max 250ms)

### Network Protocol

Packet Types:
- `0x01` - ClientInput: Sent at 60Hz with input state
- `0x02` - Snapshot: Server world state broadcast
- `0x03` - Event: Reliable events (damage, death)
- `0x04` - Ping: Latency measurement
- `0x05` - Pong: Latency response
- `0x06` - ConnectionRequest: Initial handshake
- `0x07` - ConnectionResponse: Handshake response
- `0x08` - ServerCorrection: Prediction correction

## Building

### Requirements
- Godot 4.2+ with .NET support
- .NET 6.0 SDK

### Build Commands
```bash
# Open in editor
godot --path . --editor

# Build from command line
godot --path . --build-solutions

# Export release
godot --path . --export-release "Windows Desktop" ../build/client/DarkAgesClient.exe
```

## Configuration

Edit `project.godot` or use the Godot editor to configure:
- **ServerAddress**: Default server IP (127.0.0.1)
- **ServerPort**: Default server port (7777)
- **InputSendRate**: How often inputs are sent (60 Hz)

## Controls

| Key | Action |
|-----|--------|
| W/A/S/D | Move |
| Shift | Sprint |
| Space | Jump |
| Mouse | Camera Look |
| Left Click | Attack |
| Right Click | Block |
| Escape | Toggle Mouse Capture |

## Debugging

The debug panel (top-right when connected) shows:
- **Ping**: Round-trip time to server
- **FPS**: Current frame rate
- **Entities**: Number of networked entities
- **Position**: Local player position
- **Prediction Error**: Distance between predicted and server position

## Performance Budgets

- **Target FPS**: 60 minimum, 144 preferred
- **Input Send Rate**: 60 Hz (16.67ms interval)
- **Max Input Buffer**: 120 entries (2 seconds)
- **Max Interpolation Buffer**: 10 entries (~166ms)
- **Max Extrapolation**: 250ms

## License

Part of the DarkAges MMO project. See root LICENSE for details.
