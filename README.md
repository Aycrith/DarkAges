# DarkAges MMO

A high-density PvP MMO inspired by Dark Age of Camelot and Ark Raiders, targeting 100-1000 concurrent players per shard with zero budget.

## Project Status: Phase 6 External Integration

**Previous Phases (0-5)**: ✅ Implementation Complete (~30,000 lines)  
**Current Phase**: Phase 6 - External Integration (Weeks 17-22)  
**Next**: Phase 7 - Client Implementation | Phase 8 - Production Hardening

See [PROJECT_STATUS.md](PROJECT_STATUS.md) for detailed status.

### Phase 6 Work Packages (In Progress)

| WP | Component | Status | Agent |
|----|-----------|--------|-------|
| WP-6-1 | GameNetworkingSockets | 🔴 Ready to Start | NETWORK |
| WP-6-2 | Redis Hot-State | 🔴 Ready to Start | DATABASE |
| WP-6-3 | ScyllaDB Persistence | 🔴 Ready to Start | DATABASE |
| WP-6-4 | FlatBuffers Protocol | 🔴 Ready to Start | NETWORK |
| WP-6-5 | Integration Testing | 🟡 After WP-6-1/2/3 | DEVOPS |

### What's Implemented
- ✅ **Server**: 18,000+ lines (ECS, physics, combat, sharding, security)
- ✅ **Client**: 3,500+ lines (Godot 4.x, prediction, interpolation)
- ✅ **Build System**: CMake, multi-compiler support, CI/CD
- ⏳ **External Libraries**: Stubs ready for integration

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

### Completed: Phases 0-5 (Implementation)
Phases 0-5 established the architectural foundation with complete code implementation:
- ✅ Phase 0: Foundation (ECS, spatial hash, movement)
- ✅ Phase 1: Networking stubs (protocol, delta compression)
- ✅ Phase 2: Multi-player sync (AOI, replication)
- ✅ Phase 3: Combat system (lag compensation, hit detection)
- ✅ Phase 4: Spatial sharding (zones, entity migration, Aura Projection)
- ✅ Phase 5: Optimization & security (DDoS, memory pools, profiling)

### Current: Phase 6 External Integration (Weeks 17-22)
Replace stub implementations with production external libraries:
- ⏳ WP-6-1: GameNetworkingSockets (1000 concurrent connections)
- ⏳ WP-6-2: Redis hot-state (<1ms latency)
- ⏳ WP-6-3: ScyllaDB persistence (100k writes/sec)
- ⏳ WP-6-4: FlatBuffers protocol (80% bandwidth reduction)
- ⏳ WP-6-5: Integration testing framework

**Quality Gate**: All external integrations tested and benchmarked

### Upcoming: Phase 7 Client Implementation (Weeks 23-30)
- WP-7-1: Godot 4.x foundation
- WP-7-2: Client-side prediction
- WP-7-3: Entity interpolation
- WP-7-4: Combat UI & feedback
- WP-7-5: Zone transitions
- WP-7-6: Client performance optimization

### Planned: Phase 8 Production Hardening (Weeks 31-38)
- WP-8-1: Monitoring & alerting (Prometheus/Grafana)
- WP-8-2: Security audit & hardening
- WP-8-3: Chaos engineering framework
- WP-8-4: Auto-scaling infrastructure
- WP-8-5: Load testing & capacity planning

See [PHASES_6_7_8_ROADMAP.md](PHASES_6_7_8_ROADMAP.md) for detailed roadmap.

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
