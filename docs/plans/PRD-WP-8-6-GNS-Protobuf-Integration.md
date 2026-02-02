# Product Requirements Document: WP-8-6 GNS Protobuf Integration

**Project**: DarkAges MMO  
**Work Package**: WP-8-6  
**Status**: IN PROGRESS (Day 1/14)  
**Owner**: NETWORK_AGENT  
**Priority**: CRITICAL  

---

## 1. Executive Summary

Replace GameNetworkingSockets stubs with full Protobuf integration for production-ready networking. This is a critical blocker for production deployment.

### Current State
- GNS headers and partial implementation exist
- Protocol buffers schema defined but not integrated
- NetworkManager uses stub implementation
- **Risk**: HIGH - Networking layer not production-ready

### Target State
- Full GNS integration with Protobuf serialization
- Reliable/unreliable channels operational
- Encryption (SRV) enabled
- 10,000+ concurrent connection support

---

## 2. Requirements

### 2.1 Functional Requirements

| ID | Requirement | Priority | Acceptance Criteria |
|----|-------------|----------|---------------------|
| GNS-001 | Protobuf message serialization | P0 | < 0.1ms per message |
| GNS-002 | Reliable ordered channel | P0 | 99.99% delivery guarantee |
| GNS-003 | Unreliable channel for snapshots | P0 | < 20KB/s per player |
| GNS-004 | Connection handshake | P0 | < 10ms establishment |
| GNS-005 | SRV encryption | P1 | All traffic encrypted |
| GNS-006 | NAT punchthrough | P2 | P2P when possible |

### 2.2 Performance Requirements

| Metric | Target | Current |
|--------|--------|---------|
| Connection establishment | < 10ms | N/A (stub) |
| Message latency (p99) | < 50ms | N/A |
| Throughput per server | 10,000 conn | N/A |
| Packet loss recovery | < 100ms | N/A |
| CPU overhead per conn | < 0.1% | N/A |

---

## 3. Technical Design

### 3.1 Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    NetworkManager (Facade)                      │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────┐  ┌─────────────────────────────────┐  │
│  │  GNSNetworkManager  │  │      Protocol (Protobuf)        │  │
│  │                     │  │                                 │  │
│  │  - Connection mgmt  │◄─┤  - ClientInput                  │  │
│  │  - Channel routing  │  │  - ServerSnapshot               │  │
│  │  - Encryption       │  │  - ReliableEvent                │  │
│  │  - Stats collection │  │  - Handshake                    │  │
│  └─────────────────────┘  └─────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
            ┌───────────────┐   ┌───────────────┐
            │ Reliable Msg  │   │ Unreliable Msg│
            │  (TCP-like)   │   │   (UDP-like)  │
            └───────────────┘   └───────────────┘
```

### 3.2 Protocol Definition

**File**: `src/shared/proto/network_protocol.proto`

```protobuf
syntax = "proto3";
package DarkAges.Network;

// Client -> Server input message
message ClientInput {
    uint32 sequence = 1;        // Input sequence for reconciliation
    uint32 timestamp = 2;       // Client send time (ms)
    uint32 input_flags = 3;     // Bitmask: WASD + actions
    float yaw = 4;              // Camera rotation
    float pitch = 5;
    uint32 target_entity = 6;   // For lock-on targeting
}

// Server -> Client snapshot
message ServerSnapshot {
    uint32 server_tick = 1;
    uint32 baseline_tick = 2;   // For delta compression
    repeated EntityState entities = 3;
    repeated uint32 removed_entities = 4;  // Entity IDs despawned
}

message EntityState {
    uint32 entity_id = 1;
    uint32 type = 2;            // 0=player, 1=projectile, 2=loot
    
    // Position (quantized)
    sint32 pos_x = 3;           // actual = x / 64.0
    sint32 pos_y = 4;
    sint32 pos_z = 5;
    
    // Velocity (quantized, optional)
    sint32 vel_x = 6;
    sint32 vel_y = 7;
    sint32 vel_z = 8;
    
    // State
    uint32 health_percent = 9;  // 0-100
    uint32 anim_state = 10;     // Enum: idle, run, attack
    uint32 team_id = 11;
}

// Reliable event (combat, pickups, etc.)
message ReliableEvent {
    uint32 event_type = 1;
    uint32 timestamp = 2;
    uint32 source_entity = 3;
    uint32 target_entity = 4;
    bytes event_data = 5;
}

// Connection handshake
message Handshake {
    uint32 protocol_version = 1;
    uint32 client_version = 2;
    bytes auth_token = 3;
}

message HandshakeResponse {
    bool accepted = 1;
    uint32 server_tick = 2;
    uint32 your_entity_id = 3;
    string reject_reason = 4;  // If not accepted
}

// Server correction for misprediction
message ServerCorrection {
    uint32 server_tick = 1;
    uint32 last_processed_input = 2;
    
    sint32 pos_x = 3;
    sint32 pos_y = 4;
    sint32 pos_z = 5;
    
    sint32 vel_x = 6;
    sint32 vel_y = 7;
    sint32 vel_z = 8;
}
```

### 3.3 Integration Points

| Component | Interface | Change Required |
|-----------|-----------|-----------------|
| ZoneServer | NetworkManager | Replace stub with GNS |
| Client (Godot) | NetworkClient.cs | Add Protobuf deserialization |
| Redis | Session cache | Sync connection state |
| AntiCheat | Input validation | Validate via GNS callbacks |

---

## 4. Implementation Plan

### Phase 1: Protobuf Schema (Days 1-3)
- [ ] Define complete protocol schema
- [ ] Generate C++ headers
- [ ] Generate C# classes for client
- [ ] Write serialization unit tests

### Phase 2: GNS Core Integration (Days 4-7)
- [ ] Integrate protobuf serialization into GNSNetworkManager
- [ ] Implement reliable channel
- [ ] Implement unreliable channel
- [ ] Connection handshake

### Phase 3: Client Integration (Days 8-10)
- [ ] C# Protobuf client
- [ ] Client-side channel handling
- [ ] Snapshot deserialization
- [ ] Input serialization

### Phase 4: Testing & Optimization (Days 11-14)
- [ ] 100+ concurrent connection test
- [ ] Latency measurement
- [ ] Encryption validation
- [ ] Performance profiling

---

## 5. Testing Strategy

### Unit Tests (Foundation Tier)
```cpp
TEST_CASE("GNS Protobuf serialization", "[gns][protobuf]") {
    ClientInput input;
    input.set_sequence(12345);
    input.set_yaw(1.57f);
    
    auto serialized = Serialize(input);
    auto deserialized = Deserialize<ClientInput>(serialized);
    
    REQUIRE(deserialized.sequence() == 12345);
    REQUIRE(deserialized.yaw() == Approx(1.57f));
}
```

### Integration Tests (Simulation Tier)
```python
async def test_gns_reconnection():
    # Simulate connection drop and reconnection
    client.connect()
    await simulate_packet_loss(0.1, duration=5.0)
    assert client.is_connected()
    assert client.sequence_gap < 10
```

### Validation Tests (Real Execution)
```python
async def test_movement_with_gns():
    # Launch real server with GNS
    # Launch Godot client
    # Validate movement sync
```

---

## 6. Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Protobuf performance | HIGH | Benchmark early, consider FlatBuffers if needed |
| GNS library issues | MEDIUM | Test on both Windows/Linux before production |
| Client compatibility | HIGH | Version negotiation in handshake |
| Memory leaks | MEDIUM | AddressSanitizer in CI |

---

## 7. Dependencies

- protobuf v3.21+ (FetchContent)
- GameNetworkingSockets v1.4+ (already in deps/)
- CMake 3.20+

---

## 8. Success Criteria

- [ ] All GNS tests pass
- [ ] < 10ms connection establishment
- [ ] 99.99% reliable message delivery
- [ ] < 20KB/s per player downstream
- [ ] 10,000 concurrent connections supported
- [ ] All traffic encrypted

---

**Last Updated**: 2026-02-01  
**Next Review**: 2026-02-03 (End of Phase 1)
