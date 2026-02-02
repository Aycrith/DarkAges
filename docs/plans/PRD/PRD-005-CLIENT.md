# PRD-005: Godot Client

**Version:** 1.0  
**Status:** ✅ Code Complete  
**Owner:** CLIENT_AGENT

---

## 1. Overview

### 1.1 Purpose
Third-person PvP MMO client with client-side prediction, entity interpolation, and responsive combat UI.

### 1.2 Scope
- Player controller (prediction)
- Remote player interpolation
- Combat UI (health, abilities)
- Zone transition handling

---

## 2. Requirements

### 2.1 Performance Requirements
| Metric | Target | Critical |
|--------|--------|----------|
| Frame Rate | 60 FPS | 30 FPS |
| Input Latency | <50ms | <100ms |
| Prediction Error P95 | <50ms | <100ms |
| Entity Capacity | 100 | 50 |

### 2.2 Functional Requirements
| ID | Requirement | Priority |
|----|-------------|----------|
| CL-001 | CharacterBody3D player controller | P0 |
| CL-002 | Client-side prediction with reconciliation | P0 |
| CL-003 | Remote player interpolation (100ms buffer) | P0 |
| CL-004 | Health bars (self/target/party) | P1 |
| CL-005 | Ability bar with cooldowns | P1 |
| CL-006 | Target lock-on system (100m) | P1 |
| CL-007 | Seamless zone handoff | P0 |
| CL-008 | LOD system for distant entities | P2 |

---

## 3. Implementation Status

| Component | Location | Lines | Tested |
|-----------|----------|-------|--------|
| PredictedPlayer | `src/client/scripts/` | ~400 | ⚠️ |
| RemotePlayer | `src/client/scripts/` | ~300 | ⚠️ |
| NetworkClient | `src/client/src/` | ~500 | ⚠️ |
| CombatUI | `src/client/scenes/` | ~600 | ⚠️ |

---

## 4. Acceptance Criteria
- [ ] 60 FPS with 100 entities on mid-tier GPU
- [ ] Prediction matches server within 50ms
- [ ] Smooth reconciliation (no snapping)
- [ ] Zone handoff without loading screen
