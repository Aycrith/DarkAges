# DarkAges MMO - AI Agent Context Document

**Project**: DarkAges - High-Density PvP MMO  
**Status**: Planning & Foundation Phase  
**Current Phase**: Phase 0 - Foundation (Week 1-2)  
**Language**: English (All code and documentation)  

---

## 1. Project Overview

DarkAges is a third-person PvP MMO inspired by Dark Age of Camelot and Ark Raiders, designed for high-density combat scenarios. The project is currently in the architectural planning phase with extensive documentation but **no implementation code yet**.

### Core Design Goals
- **100-1000 concurrent players per shard** via spatial sharding
- **60Hz server tick** with <50ms input latency
- **Authoritative server** (never trust client)
- **Zero budget** (open-source stack only)

### Technology Stack

| Layer | Technology | Rationale |
|-------|------------|-----------|
| **Client** | Godot 4.x | Zero licensing, adequate 3D, GDScript/C# support |
| **Server** | C++20 + EnTT | Zero-overhead, cache-friendly ECS |
| **Networking** | GameNetworkingSockets | Production-proven, congestion control |
| **Protocol** | FlatBuffers | Zero-copy, versioning, small payload |
| **Hot State** | Redis | Sub-millisecond access, pub/sub |
| **Persistence** | ScyllaDB | Cassandra-compatible, high write throughput |
| **Physics** | Custom Kinematic | Deterministic, O(n) spatial hash |
| **Build** | CMake + Docker | Cross-platform, reproducible builds |

---

## 2. Project Structure

```
C:\Dev\DarkAges\
├── AGENTS.md                      # This file - AI agent context
├── AI_COORDINATION_PROTOCOL.md    # Multi-agent workflow rules
├── ImplementationRoadmapGuide.md  # Technical specs and code examples
├── Prompt.md                      # AI agent implementation directive
├── ResearchForPlans.md            # Architectural research and decisions
│
# FUTURE STRUCTURE (to be created):
├── src/
│   ├── client/                    # Godot 4.x project
│   │   ├── assets/
│   │   ├── src/                   # GDScript/C# source
│   │   │   ├── networking/
│   │   │   ├── prediction/
│   │   │   └── combat/
│   │   └── project.godot
│   │
│   ├── server/                    # C++ ECS server
│   │   ├── CMakeLists.txt
│   │   ├── src/
│   │   │   ├── ecs/              # Components/Systems
│   │   │   ├── netcode/          # GameNetworkingSockets wrapper
│   │   │   ├── physics/          # Spatial hash, kinematic solver
│   │   │   ├── zones/            # Spatial sharding logic
│   │   │   ├── combat/           # Lag compensation, hit detection
│   │   │   └── db/               # Redis/ScyllaDB integration
│   │   ├── tests/                # Catch2 unit tests
│   │   └── dockerfile.server
│   │
│   └── shared/                    # Protocol definitions
│       ├── proto/                # FlatBuffers schemas
│       └── constants/            # Shared enums/constants
│
├── infra/                        # Infrastructure as Code
│   ├── docker-compose.yml        # Local development stack
│   ├── docker-compose.dev.yml    # Multi-zone development
│   └── k8s/                      # Kubernetes manifests (future)
│
├── deps/                         # Git submodules
│   ├── entt/                     # ECS framework
│   └── GameNetworkingSockets/    # Valve networking library
│
├── docs/                         # Living documentation
│   ├── architecture/             # ADRs, system diagrams
│   ├── network-protocol/         # Protocol specs
│   └── database-schema/          # Schema definitions
│
└── tools/                        # Development utilities
    ├── stress-test/              # Python bot swarms
    ├── packet-analyzer/          # Network debugging
    └── db-migrations/            # Schema migrations
```

---

## 3. Development Phases

### Phase 0: Foundation (Current - Weeks 1-2)
**Goal**: Single player moves on screen with server authority

**Deliverables**:
- [ ] Godot project setup with CharacterBody3D
- [ ] C++ server skeleton with EnTT ECS
- [ ] GameNetworkingSockets basic UDP communication
- [ ] Redis connection for session data
- [ ] FlatBuffers protocol definition

**Quality Gate**: Client moves cube @ 60 FPS, server validates positions, no memory leaks in 1-hour test

### Phase 1: Prediction & Reconciliation (Weeks 3-4)
**Goal**: Client predicts, server corrects errors

**Deliverables**:
- [ ] Client-side prediction buffer
- [ ] Server reconciliation protocol
- [ ] Delta compression for position updates
- [ ] Position validation (anti-cheat)

### Phase 2: Multi-Player Sync (Weeks 5-6)
**Goal**: 10 players see each other smoothly

**Deliverables**:
- [ ] Entity interest management (AOI)
- [ ] Entity interpolation for remote players
- [ ] Spatial partitioning for replication

### Phase 3: Combat & Lag Compensation (Weeks 7-8)
**Goal**: Hits register correctly despite latency

**Deliverables**:
- [ ] Lag compensator (2-second history buffer)
- [ ] Server rewind hit detection
- [ ] Combat logging to ScyllaDB

### Phase 4: Spatial Sharding (Weeks 9-12)
**Goal**: 200+ players across 2+ zone servers

**Deliverables**:
- [ ] Zone server architecture
- [ ] Aura Projection buffer zones (50m overlap)
- [ ] Entity migration between zones

### Phase 5: Optimization & Scale (Weeks 13-16)
**Goal**: 400+ players per shard, production hardening

**Deliverables**:
- [ ] Performance profiling (Perfetto)
- [ ] Anti-cheat validation
- [ ] Chaos testing framework

---

## 4. Agent Specializations

When implementing code, identify your specialization and stay within your domain:

### NETWORK_AGENT
- **Scope**: GameNetworkingSockets, packet serialization, delta compression, latency compensation
- **Key Files**: `src/server/net/*`, `src/shared/proto/*`
- **Constraints**: Downstream bandwidth < 20 KB/s per player, support IPv4/IPv6

### PHYSICS_AGENT
- **Scope**: EnTT ECS, spatial hashing, kinematic controllers, collision detection
- **Key Files**: `src/server/ecs/*`, `src/server/physics/*`, `src/server/spatial/*`
- **Constraints**: Tick budget < 16ms, O(n) collision detection, no allocations during tick

### DATABASE_AGENT
- **Scope**: Redis hot-state, ScyllaDB persistence, write-through caching, event sourcing
- **Key Files**: `src/server/db/*`, `infra/docker-compose*.yml`, SQL/CQL migrations
- **Constraints**: Redis < 1ms latency, ScyllaDB async writes only

### CLIENT_AGENT
- **Scope**: Godot 4.x, client-side prediction, entity interpolation, GDScript/C#
- **Key Files**: `src/client/*`, protocol serialization code
- **Constraints**: Match server physics exactly, 60 FPS minimum

### SECURITY_AGENT
- **Scope**: Input validation, anti-cheat, server authority enforcement, cryptography
- **Key Files**: `src/server/auth/*`, validation layers
- **Constraints**: Clamp all inputs, validate movement bounds, enable SRV encryption

### DEVOPS_AGENT
- **Scope**: Docker, CI/CD, monitoring, deployment scripts, stress testing tools
- **Key Files**: `infra/*`, `.github/workflows/*`, `tools/stress-test/*`

---

## 5. Coding Standards

### Performance (Non-Negotiable)
```cpp
// ZERO allocations during game tick - pre-allocate all buffers
// BAD: std::vector inside update loop
// GOOD: Reuse member variable buffer

// Cache coherence: Structure of Arrays (SoA) for ECS
// Use EnTT's storage patterns - sparse sets, not maps

// Determinism: Use fixed-point arithmetic
using Fixed = int32_t;  // 1000 units = 1.0f
constexpr Fixed FIXED_PRECISION = 1000;

// Latency: No blocking I/O in game thread
// All DB writes async via job queue
```

### Safety
- **Input validation**: Clamp all floats to ±10000.0f, check array bounds, validate enum values
- **Memory safety**: Use AddressSanitizer in debug builds (`-fsanitize=address`)
- **Null checks**: Every pointer dereference must have nullptr check or annotation
- **Circuit breakers**: Any external service failure must not crash server

### Maintainability
- **Self-documenting**: Complex algorithms need 3-line comment explaining WHY
- **Agent signatures**: Comment `// [AGENT-TYPE]` before functions crossing domain boundaries
- **No magic numbers**: Define constants in `Constants.hpp`
- **Testing**: Every system class needs corresponding `Test[class].cpp` with Catch2

### Version Control
- **Branch naming**: `feature/[agent]-[description]`  
  Example: `feature/network-delta-compression`
- **Commit messages**: `[AGENT] Brief description - Performance impact`  
  Example: `[NETWORK] Implement delta compression - Reduces bandwidth by 80%`
- **Never commit**: Build artifacts (`/build`, `*.exe`), IDE files (`.vscode`), large assets

---

## 6. Performance Budgets (Hard Constraints)

### Per-Zone Server Limits
```yaml
CPU:
  tick_budget_ms: 16.0  # 60Hz hard limit
  breakdown:
    network_io: 2.0ms
    physics_broadphase: 3.0ms
    physics_narrowphase: 4.0ms
    game_logic: 4.0ms
    replication: 2.0ms
    persistence_async: 1.0ms (must not block tick)

Memory:
  per_player_kb: 512        # All components + history buffers
  per_entity_kb: 128        # NPCs, projectiles
  max_entities_per_zone: 4000
  total_zone_budget_gb: 4   # Hard limit

Network:
  downstream_per_player_kbps: 20   # 2.5 KB/s max
  upstream_per_player_kbps: 2      # 250 bytes/s max
  snapshot_rate_hz: 20             # Baseline, scalable to 60Hz
  
  QoS degradation order:
    1. reduce_update_rate_for_distant_entities  # >100m: 5Hz
    2. reduce_position_precision              # 32-bit -> 16-bit
    3. cull_non_essential_animations
    4. disable_physics_for_static_objects
```

---

## 7. Cross-Agent Communication

When agents need to coordinate, use **structured comments** in code:

```cpp
// [AGENT-NETWORKING] Contract for PhysicsAgent:
// - Must call NetworkManager::broadcastEvent() within 1ms of collision detection
// - Event timestamp must match physics tick, not wall clock
// - See NetworkManager.hpp line 45 for latency compensation requirements
```

### Interface Definition Protocol
1. Create/update header in `src/shared/include/`
2. Provide stub implementation for compilation
3. Create GitHub issue tagged with agent name
4. Run integration tests together

---

## 8. Testing Strategy

### Unit Tests (C++ Catch2)
```cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "spatial/SpatialHash.hpp"

TEST_CASE("Spatial Hash basic operations", "[spatial]") {
    SpatialHash hash;
    hash.insert(1, 5.0f, 5.0f);
    auto nearby = hash.query(5.0f, 5.0f, 1.0f);
    REQUIRE(nearby.size() == 1);
}
```

### Integration Tests (Python Bots)
```python
# tools/stress-test/bot_swarm.py
# Spawns N bots, validates server maintains 60Hz
```

### Chaos Engineering
```bash
# Randomly kill zone servers to test migration
./tools/chaos-test.sh --kill-interval 30s --duration 10m
```

---

## 9. Security Checklist

Before claiming any feature complete:

- [ ] **Input Validation**: Clamp all floats, check bounds on positions
- [ ] **Rate Limiting**: Max 60 inputs/sec per client
- [ ] **Movement Validation**: Server calculates max possible distance per tick
- [ ] **Memory Safety**: AddressSanitizer in debug builds
- [ ] **Network Encryption**: Enable SRV encryption in GNS
- [ ] **Anti-Cheat Logging**: Log all damage events with context

---

## 10. Emergency Procedures

### If Tick Overrun Detected (>16ms)
1. Immediately log which system took too long
2. Activate QoS degradation (reduce replication rate, coarser collision)
3. Notify all agents to profile their systems

### If Memory Leak Detected
1. Use AddressSanitizer or Valgrind
2. Check for missing `registry.destroy(entity)` calls
3. Check network buffers growing unbounded

### If Desynchronization Detected
1. Log client and server state hashes
2. Enable full packet capture
3. Check floating-point determinism
4. Fallback: Force full snapshot resync

---

## 11. Reference Documents

Read these in priority order when implementing:

1. **ResearchForPlans.md** - Architectural rationale, technology choices, performance budgets
2. **ImplementationRoadmapGuide.md** - Technical specs, API contracts, code examples
3. **AI_COORDINATION_PROTOCOL.md** - Multi-agent coordination, failure recovery
4. **Prompt.md** - Implementation phases, coding standards, immediate actions

---

## 12. Glossary

| Term | Definition |
|------|------------|
| **ECS** | Entity Component System - data-oriented architecture |
| **Spatial Hash** | 2D/3D grid for O(1) nearby object queries |
| **Delta Compression** | Sending only changed data between frames |
| **Client-Side Prediction** | Client simulates input immediately, server corrects later |
| **Lag Compensation** | Server rewinds state to validate hits |
| **Aura Projection** | 50m buffer zone for seamless server handoffs |
| **Spatial Sharding** | Splitting world into zones run by different servers |
| **EnTT** | Header-only C++ ECS library |
| **Fixed Point** | Integer math simulating decimals for determinism |
| **Kinematic** | Movement controlled by code, not physics simulation |
| **AOI** | Area of Interest - entities within player visibility |

---

## 13. Current Status & Next Actions

**Current Phase**: Phase 0 - Foundation  
**Project State**: Planning complete, implementation pending  
**No code exists yet** - This is a greenfield project

### Immediate Next Steps (for any agent starting work):

1. Read your specialization section above
2. Review relevant sections in `ImplementationRoadmapGuide.md`
3. Create feature branch: `git checkout -b feature/[agent]-foundation`
4. Implement Phase 0 deliverable for your domain
5. Write test before moving to next deliverable
6. Commit with signature: `[AGENT] Implemented [feature] - Tested under [conditions]`

### Example First Tasks by Agent:

**NETWORK_AGENT**: Implement GameNetworkingSockets wrapper that can send/receive 100-byte packets between two C++ processes.

**PHYSICS_AGENT**: Setup EnTT registry. Create Position component. Create 1000 entities and update positions - must be <1ms.

**CLIENT_AGENT**: Setup Godot 4.0 project. Create CharacterBody3D that moves with WASD. Display connection status UI.

**DATABASE_AGENT**: Write docker-compose.yml with Redis + ScyllaDB. Write C++ class that SET/GET strings from Redis.

**SECURITY_AGENT**: Create input validation module. Function `clampPosition(Vector3)` ensuring coordinates in world bounds.

**DEVOPS_AGENT**: Setup CI workflow that builds server on commit and runs `ctest`.

---

*This document is living documentation. Update it when architecture changes or new conventions are established.*
