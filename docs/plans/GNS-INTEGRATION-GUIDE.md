# GNS Integration Guide - Critical Blocker Resolution

**Work Package**: WP-8-6  
**Status**: Implementation Required  
**Priority**: CRITICAL  

---

## Summary

GNS (GameNetworkingSockets) integration is the critical blocker preventing production deployment. Current implementation uses stubs that do not provide real network functionality.

---

## Implementation Phases

### Phase 1: Protobuf Schema (Days 1-2)

Define protocol messages in `src/shared/proto/network_protocol.proto`:

1. ClientInput - Client movement/actions
2. ServerSnapshot - Entity state updates  
3. ReliableEvent - Combat/pickups
4. Handshake/HandshakeResponse - Connection setup
5. ServerCorrection - Prediction reconciliation

### Phase 2: CMake Build Integration (Day 2)

1. Add protobuf to CMake FetchContent
2. Configure protoc code generation
3. Link protobuf library

### Phase 3: GNSNetworkManager (Days 3-8)

Implement full GNS integration:

1. Initialize GNS library
2. Create listen socket with callbacks
3. Implement connection management
4. Add reliable/unreliable send methods
5. Integrate protobuf serialization
6. Add SRV encryption

### Phase 4: Client Integration (Days 9-12)

1. Generate C# protobuf classes
2. Implement GNS client in Godot
3. Add channel handling
4. Test connection handshake

### Phase 5: Testing (Days 13-14)

1. 100+ concurrent connection test
2. Latency measurement
3. Encryption validation
4. Performance profiling

---

## Quick Win: Enhanced Stub

For immediate testing, enhance the stub to simulate network conditions:

1. Add simulated latency
2. Track virtual connections
3. Queue messages for delivery
4. Simulate packet loss

---

## Success Criteria

- [ ] < 10ms connection establishment
- [ ] 99.99% reliable delivery
- [ ] < 20KB/s per player
- [ ] 10,000 concurrent connections
- [ ] All traffic encrypted

---

**Estimated Duration**: 2 weeks  
**Risk Level**: HIGH  
**Blocking**: Production deployment
