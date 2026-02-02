# PRD-002: Networking & Protocol

**Version:** 1.0  
**Status:** ✅ Code Complete  
**Owner:** NETWORK_AGENT

---

## 1. Overview

### 1.1 Purpose
Provide reliable, low-latency networking layer for client-server communication with support for 1000+ concurrent connections.

### 1.2 Scope
- GameNetworkingSockets integration
- FlatBuffers serialization
- Delta compression
- Client-side prediction protocol

---

## 2. Requirements

### 2.1 Performance Requirements
| Metric | Target | Critical |
|--------|--------|----------|
| Connection Capacity | 1000+ | 500 |
| RTT (LAN) | <10ms | <50ms |
| Packet Loss Tolerance | 5% | 10% |
| Bandwidth per Player | <20KB/s | <50KB/s |
| Serialization Time | <50μs/entity | <100μs |

### 2.2 Functional Requirements
| ID | Requirement | Priority |
|----|-------------|----------|
| NET-001 | GameNetworkingSockets wrapper | P0 |
| NET-002 | Reliable + unreliable channels | P0 |
| NET-003 | FlatBuffers protocol (ClientInput, Snapshot) | P0 |
| NET-004 | Delta compression with baselines | P0 |
| NET-005 | Connection quality monitoring | P1 |
| NET-006 | IPv4/IPv6 dual-stack | P2 |
| NET-007 | Packet encryption (SRV) | P1 |

---

## 3. Protocol Messages

```
ClientInput (60Hz Client→Server)
├── sequence: uint32
├── timestamp: uint32
├── input_flags: uint8 (WASD/actions)
├── yaw/pitch: float
└── target_entity: uint32

Snapshot (60Hz Server→Client)
├── server_tick: uint32
├── baseline_tick: uint32
├── entities[]: EntityState
└── removed_entities[]: uint32

EntityState (per entity)
├── id: uint32
├── present_mask: uint16
├── pos: Vec3 (quantized int16)
├── rot: Quat (quantized int8)
├── health_percent: uint8
└── anim_state: uint8
```

---

## 4. Implementation Status

| Component | Location | Lines | Tested |
|-----------|----------|-------|--------|
| NetworkManager | `src/server/src/netcode/` | ~800 | ⚠️ |
| GNSNetworkManager | `src/server/src/netcode/` | ~1700 | ⚠️ |
| Protocol (FlatBuffers) | `src/shared/proto/` | ~800 | ⚠️ |
| Protobuf Protocol | `src/shared/proto/` | ~1200 | ⚠️ |

---

## 5. Acceptance Criteria
- [ ] 1000 concurrent connections sustained 1 hour
- [ ] <1% packet loss under normal conditions
- [ ] Delta compression achieves >80% bandwidth reduction
- [ ] Protocol backward compatible for 2 versions

---

## 6. Related Documents
- [PRP-002](../PRP/PRP-002-PROTOCOL-UPGRADE.md) - Protocol version migration
- [TASK-002](../tasks/TASK-002-LOAD-TEST.md) - Connection load testing
