# DarkAges MMO

A high-density PvP MMO inspired by Dark Age of Camelot and Ark Raiders, targeting 100-1000 concurrent players per shard with zero budget.

## Project Status: Phase 0 - Foundation

**Goal**: Single player moves on screen with server authority

- ✅ Server ECS architecture (C++/EnTT)
- ✅ Basic networking layer (stub for GameNetworkingSockets)
- ✅ Kinematic character movement with anti-cheat
- ✅ Spatial hashing for collision detection
- ✅ Redis/ScyllaDB integration (stubs)
- ✅ Godot 4 client with client-side prediction
- ⏳ Full integration testing

## Quick Start

### Prerequisites

- **Windows**: Visual Studio 2022 or MinGW-w64
- **Linux**: GCC 11+ or Clang 14+
- **macOS**: Xcode 14+
- CMake 3.20+
- Docker Desktop (for infrastructure)
- Godot 4.2+ (for client)

### Clone and Build

```bash
# Clone the repository
git clone <repository-url>
cd DarkAges

# Start infrastructure (Redis + ScyllaDB)
docker-compose up -d

# Build server (Release)
./build.sh Release --tests

# Or on Windows
.\build.ps1 Release -Tests
```

### Run the Server

```bash
# Run the zone server
./build/bin/darkages-server --port 7777 --zone 1

# Or with custom options
./build/bin/darkages-server --port 7777 --zone 1 --redis-host localhost --redis-port 6379
```

### Run the Client

1. Open Godot 4.2+
2. Import `src/client/project.godot`
3. Run the project (F5)
4. Enter server address (default: `127.0.0.1:7777`)
5. Click Connect

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         CLIENT (Godot 4)                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │   Input      │  │  Prediction  │  │  Interpolation   │  │
│  │   System     │→ │   Buffer     │→ │  Remote Players  │  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
└────────────────────────────────┬────────────────────────────┘
                                 │ UDP (GameNetworkingSockets)
                                 ▼
┌─────────────────────────────────────────────────────────────┐
│                    ZONE SERVER (C++/EnTT)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │   Network    │  │   Physics    │  │   Replication    │  │
│  │   (GNS)      │→ │   (Spatial)  │→ │   (Snapshots)    │  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
│  ┌──────────────┐  ┌──────────────┐                         │
│  │   Movement   │  │   Anti-Cheat │                         │
│  │   System     │  │   Validator  │                         │
│  └──────────────┘  └──────────────┘                         │
└────────────────────────────────┬────────────────────────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         ▼                       ▼                       ▼
┌──────────────────┐   ┌──────────────────┐   ┌──────────────────┐
│      Redis       │   │    ScyllaDB      │   │   Zone Server    │
│   (Hot State)    │   │  (Persistence)   │   │    (Shard N)     │
└──────────────────┘   └──────────────────┘   └──────────────────┘
```

## Technology Stack

| Layer | Technology | Rationale |
|-------|------------|-----------|
| **Client** | Godot 4.x | Zero licensing, adequate 3D |
| **Server** | C++20 + EnTT | Zero-overhead ECS |
| **Networking** | GameNetworkingSockets | Production-proven UDP |
| **Protocol** | FlatBuffers | Zero-copy serialization |
| **Hot State** | Redis | Sub-millisecond access |
| **Persistence** | ScyllaDB | High write throughput |
| **Physics** | Custom Kinematic | Deterministic, O(n) spatial hash |

## Project Structure

```
C:\Dev\DarkAges\
├── src/
│   ├── client/          # Godot 4.x project
│   │   ├── src/
│   │   │   ├── networking/
│   │   │   ├── prediction/
│   │   │   └── combat/
│   │   └── project.godot
│   │
│   ├── server/          # C++ ECS server
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── ecs/     # Components
│   │   │   ├── physics/ # Spatial hash, movement
│   │   │   ├── netcode/ # GNS wrapper
│   │   │   └── zones/   # Zone server
│   │   ├── src/
│   │   └── tests/       # Catch2 tests
│   │
│   └── shared/          # Protocol definitions
│       ├── proto/       # FlatBuffers
│       └── constants/   # Shared enums
│
├── infra/               # Docker, K8s
├── tools/               # Stress tests, utilities
└── docs/                # Architecture docs
```

## Development Phases

### Phase 0: Foundation (Current)
- [x] Godot project with CharacterBody3D
- [x] C++ server skeleton with EnTT
- [x] Basic UDP communication
- [x] Redis connection
- [ ] Full integration test

**Quality Gate**: Client moves cube @ 60 FPS, server validates positions

### Phase 1: Prediction & Reconciliation (Weeks 3-4)
- Client prediction buffer
- Server reconciliation protocol
- Delta compression

### Phase 2: Multi-Player Sync (Weeks 5-6)
- Entity interest management (AOI)
- Entity interpolation
- 10-player testing

### Phase 3: Combat & Lag Compensation (Weeks 7-8)
- Lag compensator (2s history)
- Server rewind hit detection
- Combat logging

### Phase 4: Spatial Sharding (Weeks 9-12)
- Zone server architecture
- Aura Projection buffer zones
- Entity migration

### Phase 5: Optimization & Scale (Weeks 13-16)
- 400+ player stress testing
- Anti-cheat validation
- Chaos testing

## Performance Budgets

| Resource | Limit |
|----------|-------|
| Tick Budget | 16.67ms (60Hz) |
| Network Down | 20 KB/s per player |
| Network Up | 2 KB/s per player |
| Memory/Player | 512 KB |
| Max Entities/Zone | 4000 |

## Coding Standards

### Performance (Non-Negotiable)
```cpp
// ZERO allocations during game tick
// BAD: std::vector inside update loop
// GOOD: Reuse member variable buffer

// Cache coherence: Structure of Arrays (SoA)
// Use EnTT's storage patterns

// Determinism: Use fixed-point arithmetic
using Fixed = int32_t;  // 1000 units = 1.0f
```

### Safety
- Input validation: Clamp all floats to ±10000.0f
- Memory safety: AddressSanitizer in debug builds
- Circuit breakers: External service failures don't crash server

### Commit Messages
```
[AGENT] Brief description - Performance impact

Examples:
[NETWORK] Implement delta compression - Reduces bandwidth by 80%
[PHYSICS] Optimize spatial hash queries - 2x faster collision
```

## Testing

### Unit Tests
```bash
# Build and run tests
./build.sh Debug --tests
ctest --test-dir build --output-on-failure
```

### Stress Testing
```bash
# Run Python bot swarm
cd tools/stress-test
python bot_swarm.py --count 100 --duration 300
```

## Documentation

- [Implementation Roadmap](ImplementationRoadmapGuide.md) - Technical specs
- [AI Coordination](AI_COORDINATION_PROTOCOL.md) - Multi-agent workflow
- [Research](ResearchForPlans.md) - Architectural decisions
- [Agent Instructions](AGENTS.md) - Development guidelines

## License

MIT License - See LICENSE file for details

## Contributing

This project follows a structured AI agent workflow. See [AI_COORDINATION_PROTOCOL.md](AI_COORDINATION_PROTOCOL.md) for contribution guidelines.

---

**Remember**: *The client is the enemy. Validate everything. Trust nothing.*
