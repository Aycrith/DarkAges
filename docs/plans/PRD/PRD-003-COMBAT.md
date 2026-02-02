# PRD-003: Combat System

**Version:** 1.0  
**Status:** ✅ Code Complete  
**Owner:** PHYSICS_AGENT

---

## 1. Overview

### 1.1 Purpose
Real-time PvP combat with server authority, lag compensation, and responsive hit feedback.

### 1.2 Scope
- Melee/ranged hit detection
- Lag compensation (rewind validation)
- Damage calculation
- Status effects

---

## 2. Requirements

### 2.1 Performance Requirements
| Metric | Target | Critical |
|--------|--------|----------|
| Hit Registration Latency | <100ms | <200ms |
| Rewind Buffer | 2 seconds | 1 second |
| Combat Tick Rate | 60Hz | 30Hz |

### 2.2 Functional Requirements
| ID | Requirement | Priority |
|----|-------------|----------|
| CMB-001 | Melee cone collision detection | P0 |
| CMB-002 | Ranged projectile trajectory | P0 |
| CMB-003 | Server-side rewind for hit validation | P0 |
| CMB-004 | Damage calculation with modifiers | P1 |
| CMB-005 | Status effect system | P2 |
| CMB-006 | Kill/death tracking | P1 |

---

## 3. Lag Compensation

```
Attack Flow:
1. Client sends attack (timestamp, target)
2. Server receives (delay = RTT/2 + jitter)
3. Server rewinds entity positions to attack_time
4. Validate hit against historical positions
5. Apply damage if valid
6. Send confirmation to client
```

---

## 4. Implementation Status

| Component | Location | Lines | Tested |
|-----------|----------|-------|--------|
| CombatSystem | `src/server/include/combat/` | ~900 | ⚠️ |
| LagCompensator | `src/server/include/combat/` | ~400 | ⚠️ |
| DamageSystem | `src/server/src/combat/` | ~300 | ⚠️ |

---

## 5. Acceptance Criteria
- [ ] Hits register within 100ms of client action
- [ ] Rewind correctly handles 200ms latency
- [ ] No false positives under normal conditions
- [ ] Combat feels responsive (player feedback)

---

## 6. Related Documents
- [PRP-003](../PRP/PRP-003-COMBAT-BALANCING.md) - Balance proposals
- [TASK-003](../tasks/TASK-003-COMBAT-VALIDATION.md) - Combat testing
