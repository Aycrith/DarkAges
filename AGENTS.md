# DarkAges MMO - AI Agent Context Document

**Project**: DarkAges - High-Density PvP MMO  
**Status**: Phase 0 - Foundation COMPLETE with Testing Infrastructure  
**Current Phase**: Testing & Validation Phase  
**Language**: English (All code and documentation)  

---

## 1. Project Overview

DarkAges is a third-person PvP MMO inspired by Dark Age of Camelot and Ark Raiders, designed for high-density combat scenarios. The project has moved from planning into the **Testing & Validation Phase** with comprehensive three-tier testing infrastructure now operational.

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
| **Testing** | Three-Tier Strategy | Unit → Simulation → Real Execution (MCP) |

---

## 2. Project Structure

```
C:\Dev\DarkAges\
├── AGENTS.md                      # This file - AI agent context
├── AI_COORDINATION_PROTOCOL.md    # Multi-agent workflow rules
├── ImplementationRoadmapGuide.md  # Technical specs and code examples
├── Prompt.md                      # AI agent implementation directive
├── ResearchForPlans.md            # Architectural research and decisions
├── TESTING_STRATEGY.md            # Canonical testing strategy
├── MCP_INTEGRATION_COMPLETE.md    # Godot MCP integration summary
├── TESTING_INFRASTRUCTURE_COMPLETE.md # Testing infrastructure summary
│
├── src/
│   ├── client/                    # Godot 4.x project (C#)
│   │   ├── assets/
│   │   ├── src/                   # C# source
│   │   │   ├── networking/
│   │   │   ├── prediction/
│   │   │   └── combat/
│   │   ├── scenes/                # .tscn scene files
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
│   ├── database-schema/          # Schema definitions
│   └── testing/                  # Testing documentation
│       ├── COMPREHENSIVE_TESTING_PLAN.md
│       └── ARCHITECTURE_RECONCILIATION.md
│
└── tools/                        # Development utilities
    ├── testing/                  # TESTING INFRASTRUCTURE
    │   ├── TestRunner.py         # Master orchestrator
    │   ├── TestDashboard.py      # Web/console dashboard
    │   ├── telemetry/            # Metrics collection
    │   │   └── MetricsCollector.py
    │   ├── scenarios/            # Test scenarios
    │   │   └── MovementTestScenarios.py
    │   └── reports/              # Test output (generated)
    │
    ├── automated-qa/             # REAL EXECUTION TESTING
    │   ├── AutomatedQA.py        # Process orchestration
    │   └── godot-mcp/            # Godot MCP integration
    │       ├── client.py
    │       ├── test_movement_sync_mcp.py
    │       └── README.md
    │
    ├── test-orchestrator/        # SIMULATION TESTING
    │   └── TestOrchestrator.py   # Network simulation
    │
    ├── stress-test/              # Load testing
    ├── packet-analyzer/          # Network debugging
    └── db-migrations/            # Schema migrations
```

---

## 3. Development Phases

### Phase 0: Foundation ✅ COMPLETE
**Goal**: Single player moves on screen with server authority

**Deliverables**:
- [x] Godot project setup with CharacterBody3D
- [x] C++ server skeleton with EnTT ECS
- [x] GameNetworkingSockets basic UDP communication
- [x] Redis connection for session data
- [x] FlatBuffers protocol definition
- [x] **Three-tier testing infrastructure**

**Quality Gate**: Client moves cube @ 60 FPS, server validates positions, no memory leaks in 1-hour test

### Phase 1: Prediction & Reconciliation (Current)
**Goal**: Client predicts, server corrects errors

**Deliverables**:
- [ ] Client-side prediction buffer
- [ ] Server reconciliation protocol
- [ ] Delta compression for position updates
- [ ] Position validation (anti-cheat)
- [x] **Testing infrastructure for validation** ✅

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

## 4. Testing Architecture (UPDATED - CRITICAL)

The project now has a **comprehensive three-tier testing infrastructure**. All agents must understand and work within this framework.

### Three-Tier Testing Model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      VALIDATION TIER (Real Execution)                        │
│                                                                              │
│  Components: Godot MCP + AutomatedQA.py                                      │
│  Purpose:    Critical gamestate validation with real services                │
│  Usage:      python TestRunner.py --tier=validation --mcp                    │
│                                                                              │
│  Key Files:                                                                  │
│   - tools/automated-qa/godot-mcp/client.py                                  │
│   - tools/automated-qa/AutomatedQA.py                                       │
│   - tools/testing/scenarios/MovementTestScenarios.py                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      SIMULATION TIER (Protocol Testing)                      │
│                                                                              │
│  Components: TestOrchestrator.py + MetricsCollector.py                       │
│  Purpose:    Validate sync logic without real processes                      │
│  Usage:      python TestRunner.py --tier=simulation                          │
│                                                                              │
│  Key Files:                                                                  │
│   - tools/test-orchestrator/TestOrchestrator.py                             │
│   - tools/testing/scenarios/MovementTestScenarios.py                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      FOUNDATION TIER (Unit Tests)                            │
│                                                                              │
│  Components: Catch2 + C++ Test Suite                                         │
│  Purpose:    Fast, deterministic component testing                           │
│  Usage:      ./darkages_server --test  OR  ctest                             │
│                                                                              │
│  Key Files:                                                                  │
│   - src/server/tests/*.cpp                                                  │
│   - Any new component should have Test[Component].cpp                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Testing Workflow for Agents

**When Implementing New Features:**

1. **Write Foundation Tests First**
   ```cpp
   // Create Test[Feature].cpp
   TEST_CASE("[feature] basic functionality", "[foundation]") {
       // Test isolated component
   }
   ```

2. **Add Simulation Tests**
   ```python
   # Add scenario to tools/testing/scenarios/
   async def scenario_[feature]():
       with MetricsCollector("[feature]") as collector:
           # Test protocol/sync logic
   ```

3. **Create Validation Test (if UI/network involved)**
   ```python
   # Add to godot-mcp test suite
   # Tests real Godot client interaction
   ```

### Test Orchestration

**Master Test Runner:** `tools/testing/TestRunner.py`

```bash
# Run all tiers
python tools/testing/TestRunner.py --tier=all

# Run specific tier
python tools/testing/TestRunner.py --tier=foundation
python tools/testing/TestRunner.py --tier=simulation
python tools/testing/TestRunner.py --tier=validation --mcp

# View results
python tools/testing/TestDashboard.py --console
```

### Human-in-the-Loop (Validation Tier)

The Validation Tier supports human oversight for critical tests:

```python
# In your test scenario
from automated_qa.harness import HumanInterface

human = HumanInterface()
response = human.request_validation(
    message="Does the character movement look smooth?",
    screenshot="path/to/screenshot.png",
    level=HumanInterface.QUESTION
)
# Response: continue, pause, skip, retry, abort
```

### Performance Budgets (Testing)

| Tier | Target Duration | Parallel Execution |
|------|-----------------|-------------------|
| Foundation | < 1ms per test | Yes (unlimited) |
| Simulation | < 5s per scenario | Yes (up to 10) |
| Validation | < 60s per test | Limited (2-3) |

---

## 5. Agent Specializations (UPDATED)

When implementing code, identify your specialization and stay within your domain. **All agents must now consider testing requirements.**

### NETWORK_AGENT
- **Scope**: GameNetworkingSockets, packet serialization, delta compression, latency compensation
- **Key Files**: `src/server/net/*`, `src/shared/proto/*`
- **Testing Requirements**:
  - Unit tests for packet serialization
  - Simulation tests for latency effects
  - Validation tests with real network stack
- **Constraints**: Downstream bandwidth < 20 KB/s per player, support IPv4/IPv6

### PHYSICS_AGENT
- **Scope**: EnTT ECS, spatial hashing, kinematic controllers, collision detection
- **Key Files**: `src/server/ecs/*`, `src/server/physics/*`, `src/server/spatial/*`
- **Testing Requirements**:
  - Unit tests for spatial queries
  - Simulation tests for collision scenarios
- **Constraints**: Tick budget < 16ms, O(n) collision detection, no allocations during tick

### DATABASE_AGENT
- **Scope**: Redis hot-state, ScyllaDB persistence, write-through caching, event sourcing
- **Key Files**: `src/server/db/*`, `infra/docker-compose*.yml`, SQL/CQL migrations
- **Testing Requirements**:
  - Unit tests for DB operations
  - Integration tests for Redis/ScyllaDB
- **Constraints**: Redis < 1ms latency, ScyllaDB async writes only

### CLIENT_AGENT
- **Scope**: Godot 4.x, client-side prediction, entity interpolation, GDScript/C#
- **Key Files**: `src/client/*`, protocol serialization code
- **Testing Requirements**:
  - Unit tests for prediction algorithms
  - MCP validation tests for rendering
- **Constraints**: Match server physics exactly, 60 FPS minimum

### SECURITY_AGENT
- **Scope**: Input validation, anti-cheat, server authority enforcement, cryptography
- **Key Files**: `src/server/auth/*`, validation layers
- **Testing Requirements**:
  - Unit tests for validation functions
  - Simulation tests for cheat detection
- **Constraints**: Clamp all inputs, validate movement bounds, enable SRV encryption

### DEVOPS_AGENT
- **Scope**: Docker, CI/CD, monitoring, deployment scripts, stress testing tools
- **Key Files**: `infra/*`, `.github/workflows/*`, `tools/stress-test/*`
- **Testing Requirements**:
  - Maintain CI/CD pipeline
  - Ensure all test tiers run in CI
- **Constraints**: CI must pass all tiers before merge

### QA_AGENT (NEW)
- **Scope**: Test scenario development, metrics analysis, test automation
- **Key Files**: `tools/testing/*`, `tools/automated-qa/*`
- **Responsibilities**:
  - Create test scenarios for new features
  - Analyze test metrics and reports
  - Maintain test documentation
- **Constraints**: All scenarios must have clear pass/fail criteria

---

## 6. Coding Standards (UPDATED WITH TESTING)

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

### Testing Requirements (NEW)
```cpp
// Every system class MUST have corresponding test file

// Example: src/server/physics/SpatialHash.hpp
//          src/server/tests/TestSpatialHash.cpp

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "physics/SpatialHash.hpp"

TEST_CASE("Spatial Hash basic operations", "[foundation]") {
    SpatialHash hash;
    hash.insert(1, 5.0f, 5.0f);
    auto nearby = hash.query(5.0f, 5.0f, 1.0f);
    REQUIRE(nearby.size() == 1);
}

// Agent signature comment for cross-domain functions
// [AGENT-PHYSICS] Contract for NetworkAgent:
// - Must call within 1ms of collision detection
// - See NetworkManager.hpp line 45
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
- **Test commits**: Include `[TEST]` tag for test-only changes  
  Example: `[TEST] Add spatial hash collision scenarios`

---

## 7. Performance Budgets (Hard Constraints)

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

Testing:
  foundation_tier_max_ms: 1        # Per test
  simulation_tier_max_s: 5         # Per scenario
  validation_tier_max_s: 60        # Per test
  ci_total_max_m: 15               # Total CI pipeline
```

---

## 8. Cross-Agent Communication

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
4. **Run integration tests together** using TestRunner.py

### Testing Coordination Protocol

When your changes affect multiple domains:

```bash
# 1. Run Foundation Tier first
./build/Release/darkages_server.exe --test

# 2. Run Simulation Tier for integration
python tools/testing/TestRunner.py --tier=simulation

# 3. Coordinate with other agents for Validation Tier
python tools/testing/TestRunner.py --tier=validation --mcp --human-oversight
```

---

## 9. Testing Strategy (CRITICAL)

### Unit Tests (Foundation Tier)
```cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "spatial/SpatialHash.hpp"

TEST_CASE("Spatial Hash basic operations", "[foundation]") {
    SpatialHash hash;
    hash.insert(1, 5.0f, 5.0f);
    auto nearby = hash.query(5.0f, 5.0f, 1.0f);
    REQUIRE(nearby.size() == 1);
}
```

### Integration Tests (Simulation Tier)
```python
# tools/testing/scenarios/MovementTestScenarios.py

async def scenario_basic_movement():
    with MetricsCollector("movement") as collector:
        # Simulate client prediction
        # Simulate server reconciliation
        # Validate sync metrics
        pass
```

### Real Execution Tests (Validation Tier)
```python
# tools/automated-qa/godot-mcp/test_movement_sync_mcp.py

async def test_movement_synchronization():
    # Launch real server
    # Launch Godot client via MCP
    # Inject inputs
    # Capture screenshots
    # Validate results
    pass
```

### Chaos Engineering
```bash
# Randomly kill zone servers to test migration
./tools/chaos-test.sh --kill-interval 30s --duration 10m
```

---

## 10. Security Checklist

Before claiming any feature complete:

- [ ] **Input Validation**: Clamp all floats, check bounds on positions
- [ ] **Rate Limiting**: Max 60 inputs/sec per client
- [ ] **Movement Validation**: Server calculates max possible distance per tick
- [ ] **Memory Safety**: AddressSanitizer in debug builds
- [ ] **Network Encryption**: Enable SRV encryption in GNS
- [ ] **Anti-Cheat Logging**: Log all damage events with context
- [ ] **Foundation Tests**: Unit tests pass
- [ ] **Simulation Tests**: Sync scenarios pass
- [ ] **Validation Tests**: Real execution passes (if applicable)

---

## 11. Emergency Procedures

### If Tick Overrun Detected (>16ms)
1. Immediately log which system took too long
2. Activate QoS degradation (reduce replication rate, coarser collision)
3. Notify all agents to profile their systems
4. **Run Simulation Tier** to identify bottleneck

### If Memory Leak Detected
1. Use AddressSanitizer or Valgrind
2. Check for missing `registry.destroy(entity)` calls
3. Check network buffers growing unbounded
4. **Run Foundation Tier** memory tests

### If Desynchronization Detected
1. Log client and server state hashes
2. Enable full packet capture
3. Check floating-point determinism
4. **Run Validation Tier** with MCP to capture real behavior
5. Fallback: Force full snapshot resync

### If CI/CD Tests Fail
1. Check which tier failed
2. Run that tier locally: `python TestRunner.py --tier=[tier]`
3. Fix issues and re-run
4. **Never merge with failing tests**

---

## 12. Reference Documents

Read these in priority order when implementing:

1. **TESTING_STRATEGY.md** - Canonical testing strategy and success criteria
2. **ResearchForPlans.md** - Architectural rationale, technology choices, performance budgets
3. **ImplementationRoadmapGuide.md** - Technical specs, API contracts, code examples
4. **AI_COORDINATION_PROTOCOL.md** - Multi-agent coordination, failure recovery
5. **docs/testing/COMPREHENSIVE_TESTING_PLAN.md** - Detailed test scenarios
6. **docs/testing/ARCHITECTURE_RECONCILIATION.md** - Deviation resolution
7. **Prompt.md** - Implementation phases, coding standards, immediate actions

---

## 13. Glossary

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
| **MCP** | Model Context Protocol - AI assistant integration |
| **Foundation Tier** | Unit tests (C++/Catch2) |
| **Simulation Tier** | Protocol testing (Python/TestOrchestrator) |
| **Validation Tier** | Real execution (Godot MCP/AutomatedQA) |

---

## 14. Current Status & Next Actions

**Current Phase**: Phase 1 - Prediction & Reconciliation  
**Project State**: Foundation COMPLETE, Testing Infrastructure OPERATIONAL  
**Testing Status**: Three-tier infrastructure deployed and validated

### Immediate Next Steps (for any agent starting work):

1. **Read your specialization section above**
2. **Review TESTING_STRATEGY.md** for test requirements
3. **Understand the three-tier testing model**
4. Create feature branch: `git checkout -b feature/[agent]-[description]`
5. **Write tests BEFORE implementation** (Foundation Tier first)
6. Implement feature
7. Run all three tiers: `python TestRunner.py --tier=all`
8. Commit with signature: `[AGENT] Implemented [feature] - [TEST] Added coverage`

### Example First Tasks by Agent:

**NETWORK_AGENT**: 
- Implement GameNetworkingSockets wrapper
- **Write**: `TestNetworkProtocol.cpp` (Foundation)
- **Write**: `scenario_packet_loss.py` (Simulation)
- Test with: `python TestRunner.py --tier=simulation`

**PHYSICS_AGENT**: 
- Setup EnTT registry with Position component
- **Write**: `TestSpatialHash.cpp` (Foundation)
- Create 1000 entities, verify <1ms update
- Test with: `python TestRunner.py --tier=foundation`

**CLIENT_AGENT**: 
- Setup Godot 4.0 project with CharacterBody3D
- **Write**: MCP validation test for movement (Validation)
- Display connection status UI
- Test with: `python TestRunner.py --tier=validation --mcp`

**QA_AGENT**: 
- Create 5 new test scenarios for movement edge cases
- Analyze test metrics from recent runs
- Update test documentation

---

*This document is living documentation. Update it when architecture changes or new conventions are established.*

**Last Updated:** 2026-01-30  
**Update Reason:** Testing infrastructure reconciliation  
**Status:** ARCHITECTURALLY ALIGNED
