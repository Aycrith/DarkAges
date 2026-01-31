# DarkAges MMO

A high-density PvP MMO inspired by Dark Age of Camelot and Ark Raiders, targeting 100-1000 concurrent players per shard with zero budget.

## Project Status: Phase 8 Production Hardening

**Previous Phases (0-7)**: ✅ Implementation Complete (~38,600 lines)  
**Current Phase**: Phase 8 - Production Hardening (Week 1 of 8)  
**Server Status**: ✅ Operational (60Hz tick rate, stable)

See [CURRENT_STATUS.md](CURRENT_STATUS.md) for daily updates • [PROJECT_STATUS.md](PROJECT_STATUS.md) for detailed history • [PHASE8_EXECUTION_PLAN.md](PHASE8_EXECUTION_PLAN.md) for roadmap

### Completed Work Packages

**Phase 6 - External Integration:**
- ✅ **WP-6-2**: Redis Hot-State Integration (hiredis, connection pooling)
- ✅ **WP-6-4**: FlatBuffers Protocol (delta compression, binary serialization)
- ✅ **WP-6-5**: Integration Testing Framework (Docker, Python bots, CI/CD)

**Phase 7 - Client Implementation:**
- ✅ **WP-7-3**: Client Entity Interpolation (Godot C#, 100ms delay buffer)

### Phase 8 Work Packages (In Progress)

| WP | Component | Status | Agent |
|----|-----------|--------|-------|
| WP-8-1 | Production Monitoring | 🟡 Day 1/14 | DEVOPS |
| WP-8-2 | Security Audit | ⏳ Planned | SECURITY |
| WP-8-3 | Performance Optimization | ⏳ Planned | PHYSICS |
| WP-8-4 | Load Testing | ⏳ Planned | DEVOPS |
| WP-8-5 | Documentation Cleanup | ⏳ Planned | ALL |
| WP-8-6 | GNS Full Integration | 🟡 Day 1/14 | NETWORK |

### What's Implemented
- ✅ **Server**: 18,000+ lines (ECS, physics, combat, sharding, security) - **OPERATIONAL**
- ✅ **Client**: 3,500+ lines (Godot 4.x, prediction, interpolation) - **OPERATIONAL**
- ✅ **Testing**: 15,000+ lines (Three-tier infrastructure) - **OPERATIONAL**
- ✅ **Build System**: CMake, MSVC 2022, cross-platform CI/CD - **COMPLETE**
- ✅ **External Libraries**: Redis ✅, FlatBuffers ✅, GNS ⚠️ (partial/stubs)

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

### Completed: Phases 0-7 (Foundation through Client Implementation)
All core architecture, external integrations, and client features implemented:
- ✅ Phase 0: Foundation (ECS, spatial hash, movement)
- ✅ Phase 1: Networking stubs (protocol, delta compression)
- ✅ Phase 2: Multi-player sync (AOI, replication)
- ✅ Phase 3: Combat system (lag compensation, hit detection)
- ✅ Phase 4: Spatial sharding (zones, entity migration, Aura Projection)
- ✅ Phase 5: Optimization & security (DDoS, memory pools, profiling)
- ✅ Phase 6: External Integration (Redis ✅, FlatBuffers ✅, Testing Framework ✅)
- ✅ Phase 7: Client Implementation (Interpolation ✅, partial completion)

**Quality Gates Passed**: Server operational at 60Hz, three-tier testing infrastructure validated

### Current: Phase 8 Production Hardening (Week 1 of 8)
Final preparation for production deployment and scaling:
- 🟡 WP-8-1: Production Monitoring (Prometheus/Grafana) - **IN PROGRESS**
- ⏳ WP-8-2: Security Audit & Hardening
- ⏳ WP-8-3: Performance Optimization & Profiling
- ⏳ WP-8-4: Load Testing (100+ concurrent players)
- ⏳ WP-8-5: Documentation Cleanup
- 🟡 WP-8-6: GameNetworkingSockets Full Integration - **IN PROGRESS**

**Quality Gate**: Production-ready server capable of 100+ concurrent players with monitoring, security hardening, and comprehensive documentation

See [PHASE8_EXECUTION_PLAN.md](PHASE8_EXECUTION_PLAN.md) for detailed 8-week roadmap • [CURRENT_STATUS.md](CURRENT_STATUS.md) for daily progress

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
