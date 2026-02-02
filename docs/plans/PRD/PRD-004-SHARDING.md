# PRD-004: Spatial Sharding

**Version:** 1.0  
**Status:** ✅ Code Complete  
**Owner:** NETWORK_AGENT

---

## 1. Overview

### 1.1 Purpose
Seamless world scaling through spatial sharding with 200m hexagonal zones and 50m aura buffers for smooth handoffs.

### 1.2 Scope
- Zone grid management
- Entity migration between zones
- Aura projection (cross-zone visibility)
- Redis pub/sub for cross-zone messaging

---

## 2. Requirements

### 2.1 Performance Requirements
| Metric | Target | Critical |
|--------|--------|----------|
| Zone Handoff Time | <500ms | <1000ms |
| Aura Buffer | 50m | 30m |
| Cross-Zone Latency | <10ms | <50ms |

### 2.2 Functional Requirements
| ID | Requirement | Priority |
|----|-------------|----------|
| SH-001 | Hexagonal zone grid (200m diameter) | P0 |
| SH-002 | 5-state migration machine | P0 |
| SH-003 | Aura projection manager | P0 |
| SH-004 | Redis pub/sub for entity sync | P1 |
| SH-005 | No entity duplication/loss on handoff | P0 |

---

## 3. Migration States
```
NORMAL → NOTIFYING → MIGRATING → HANDED_OFF → CLEANUP
   ↓         ↓           ↓            ↓
Timeout   Timeout     Timeout      Timeout → Rollback
```

---

## 4. Implementation Status

| Component | Location | Lines | Tested |
|-----------|----------|-------|--------|
| EntityMigration | `src/server/src/zones/` | ~800 | ⚠️ |
| AuraProjectionManager | `src/server/include/zones/` | ~500 | ⚠️ |
| CrossZoneMessenger | `src/server/include/zones/` | ~400 | ⚠️ |

---

## 5. Acceptance Criteria
- [ ] Zone handoff <500ms perceived
- [ ] No duplication during migration
- [ ] Cross-zone combat works correctly
