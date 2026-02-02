# PRD-001: Server Core Architecture

**Version:** 1.0  
**Status:** ✅ Code Complete  
**Owner:** CORE_AGENT

---

## 1. Overview

### 1.1 Purpose
Provide a high-performance authoritative game server capable of handling 100-1000 concurrent players per shard with 60Hz tick rate.

### 1.2 Scope
- Entity Component System (ECS) architecture
- Fixed timestep game loop
- Memory management
- Profiling infrastructure

---

## 2. Requirements

### 2.1 Performance Requirements
| Metric | Target | Critical |
|--------|--------|----------|
| Tick Rate | 60Hz (16.67ms) | 50Hz (20ms) |
| Tick Budget Used | <80% | <95% |
| Memory Growth | <10MB/hr | <50MB/hr |
| Entity Capacity | 1000/zone | 500/zone |

### 2.2 Functional Requirements
| ID | Requirement | Priority |
|----|-------------|----------|
| SC-001 | EnTT ECS for entity management | P0 |
| SC-002 | Fixed-point arithmetic for determinism | P0 |
| SC-003 | Stack allocator for zero-heap game loop | P1 |
| SC-004 | Memory pooling for frequent allocations | P1 |
| SC-005 | Perfetto profiling integration | P2 |

### 2.3 Technical Constraints
- C++20 standard
- Must compile with MSVC 2022 + GCC/Clang
- Binary size <1MB

---

## 3. Implementation Status

| Component | Files | Lines | Tested |
|-----------|-------|-------|--------|
| CoreTypes | `src/server/include/ecs/` | ~500 | ⚠️ |
| ZoneServer | `src/server/src/zones/` | ~800 | ⚠️ |
| MemoryPool | `src/server/include/memory/` | ~600 | ⚠️ |
| Profiler | `src/server/include/profiling/` | ~400 | ⚠️ |

---

## 4. Acceptance Criteria
- [ ] Server runs stable for 24 hours
- [ ] Tick time <16ms with 500 entities
- [ ] No memory leaks detected
- [ ] Perfetto traces capture full tick breakdown

---

## 5. Related Documents
- [PRP-001](../PRP/PRP-001-SERVER-OPTIMIZATION.md) - Optimization proposals
- [TASK-001](../tasks/TASK-001-UNIT-TESTS.md) - Unit test implementation
