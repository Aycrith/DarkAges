# DarkAges MMO - Phases 6-8 Development Roadmap

**Document Version:** 1.0  
**Purpose:** Structured work packages for parallel agent development  
**Target:** AI Development Agents  

---

## Executive Summary

Phases 0-5 established the foundation with stub implementations. Phases 6-8 focus on:
- **Phase 6:** External Integration (real networking, databases)
- **Phase 7:** Client Implementation (Godot 4.x)
- **Phase 8:** Production Hardening (security, monitoring, chaos testing)

---

## Phase 6: External Integration (Weeks 17-22)

### Goal
Replace all stub implementations with production-ready external libraries.

### Success Criteria
- [ ] Server accepts 1000 concurrent UDP connections via GameNetworkingSockets
- [ ] Redis handles 10k ops/sec with <1ms latency
- [ ] ScyllaDB persists 100k combat events/sec
- [ ] FlatBuffers serialization < 50μs per entity state

---

## Phase 7: Client Implementation (Weeks 23-30)

### Goal
Functional Godot 4.x client with prediction, interpolation, and combat UI.

### Success Criteria
- [ ] Client connects to server and renders 100 entities at 60 FPS
- [ ] Input prediction matches server within 50ms
- [ ] Combat UI shows health bars, ability cooldowns, target lock
- [ ] Client handles zone handoffs seamlessly

---

## Phase 8: Production Hardening (Weeks 31-38)

### Goal
Production-ready infrastructure with monitoring, chaos testing, and security audit.

### Success Criteria
- [ ] 99.9% uptime during 7-day stress test
- [ ] Detect and auto-mitigate DDoS attacks
- [ ] Zero critical security vulnerabilities
- [ ] Auto-scaling zone servers based on load

---

## Work Package Structure

Each work package follows this template:

```
WP-[PHASE]-[ID]: [Title]
═════════════════════════════════════════
Agent: [NETWORK|PHYSICS|DATABASE|CLIENT|SECURITY|DEVOPS]
Duration: [X] weeks
Dependencies: [WP-XX, WP-YY]

Deliverables:
1. [Specific, measurable outcome]
2. [Specific, measurable outcome]

Interfaces:
- Input: [What this WP needs from others]
- Output: [What this WP provides to others]

Quality Gate:
- [Testable condition for completion]
```

---

## PHASE 6 WORK PACKAGES

### WP-6-1: GameNetworkingSockets Integration
**Agent:** NETWORK  
**Duration:** 2 weeks  
**Dependencies:** None

**Deliverables:**
1. Replace NetworkManager_stub.cpp with full GNS implementation
2. Implement connection quality monitoring (RTT, packet loss, jitter)
3. Implement reliable/unreliable channels
4. IPv4/IPv6 dual-stack support

**Interfaces:**
- Input: ZoneServer tick loop, CombatSystem events
- Output: ClientInputPacket[], ConnectionQuality metrics

**Quality Gate:**
- 1000 concurrent connections maintained for 1 hour
- <1% packet loss under normal conditions
- <50ms average RTT on LAN

---

### WP-6-2: Redis Hot-State Integration
**Agent:** DATABASE  
**Duration:** 2 weeks  
**Dependencies:** None

**Deliverables:**
1. Replace RedisManager_stub.cpp with hiredis implementation
2. Implement session state caching (player positions, health)
3. Implement cross-zone pub/sub for entity migration
4. Connection pooling with failover

**Interfaces:**
- Input: ZoneServer player login/logout, entity migration requests
- Output: Session data, zone channel messages

**Quality Gate:**
- <1ms latency for GET/SET operations
- 10k ops/sec sustained throughput
- Automatic reconnection on failure

---

### WP-6-3: ScyllaDB Persistence Layer
**Agent:** DATABASE  
**Duration:** 2 weeks  
**Dependencies:** None

**Deliverables:**
1. Replace ScyllaManager_stub.cpp with cassandra-cpp-driver
2. Implement combat event logging schema
3. Implement player stats aggregation
4. Async write batching for performance

**Interfaces:**
- Input: CombatSystem hit events, player session end
- Output: Queryable combat history, leaderboards

**Quality Gate:**
- 100k writes/sec sustained
- Write latency <10ms p99
- Successful read-after-write consistency

---

### WP-6-4: FlatBuffers Protocol Implementation
**Agent:** NETWORK  
**Duration:** 1 week  
**Dependencies:** None

**Deliverables:**
1. Define .fbs schemas for all packet types
2. Implement serializeInput/deserializeInput
3. Implement delta compression with baselines
4. Versioning for backward compatibility

**Interfaces:**
- Input: EntityStateData[], InputState
- Output: std::vector<uint8_t> serialized packets

**Quality Gate:**
- Serialization <50μs per entity
- Delta compression achieves >80% bandwidth reduction
- Backward compatibility for 2 protocol versions

---

### WP-6-5: Integration Testing Framework
**Agent:** DEVOPS  
**Duration:** 1 week  
**Dependencies:** WP-6-1, WP-6-2, WP-6-3

**Deliverables:**
1. Docker Compose stack with Redis + ScyllaDB
2. Python bot swarm for load testing
3. Network chaos testing (packet loss, latency injection)
4. Automated integration test suite

**Interfaces:**
- Input: All server components
- Output: Test reports, performance benchmarks

**Quality Gate:**
- 100 player simulation runs successfully
- All integration tests pass
- Performance benchmarks meet Phase 6 criteria

---

## PHASE 7 WORK PACKAGES

### WP-7-1: Godot Project Foundation
**Agent:** CLIENT  
**Duration:** 2 weeks  
**Dependencies:** WP-6-4 (protocol definition)

**Deliverables:**
1. Godot 4.x project with CharacterBody3D
2. Basic third-person camera controller
3. UDP networking client (GDExtension or C#)
4. Protocol deserialization (FlatBuffers)

**Interfaces:**
- Input: Server snapshots, entity states
- Output: Client inputs, connection status

**Quality Gate:**
- Player moves on screen at 60 FPS
- Receives server position updates
- No memory leaks in 1-hour test

---

### WP-7-2: Client-Side Prediction
**Agent:** CLIENT  
**Duration:** 2 weeks  
**Dependencies:** WP-7-1

**Deliverables:**
1. Input prediction buffer (2-second window)
2. Local physics simulation matching server
3. Server reconciliation handling
4. Position error visualization (debug)

**Interfaces:**
- Input: InputState, Server corrections
- Output: Predicted positions, reconciliation status

**Quality Gate:**
- Prediction matches server within 50ms
- Smooth error correction (no snapping)
- Handles 200ms latency gracefully

---

### WP-7-3: Entity Interpolation
**Agent:** CLIENT  
**Duration:** 2 weeks  **Dependencies:** WP-7-1

**Deliverables:**
1. Remote player interpolation (render timestamp delay)
2. Position/rotation interpolation smoothing
3. Extrapolation for missing packets
4. Hit prediction visualization

**Interfaces:**
- Input: EntityStateData[] from server
- Output: Interpolated entity transforms

**Quality Gate:**
- Remote players move smoothly (no jitter)
- <100ms perceived latency for observers
- Handles packet loss without snapping

---

### WP-7-4: Combat UI & Feedback
**Agent:** CLIENT  
**Duration:** 2 weeks  
**Dependencies:** WP-7-1

**Deliverables:**
1. Health bars (self, target, party)
2. Ability bar with cooldown timers
3. Target lock-on system
4. Combat text (damage numbers, hits/misses)
5. Death/respawn UI

**Interfaces:**
- Input: CombatState, HitResult events
- Output: Player ability activation requests

**Quality Gate:**
- Health updates within 100ms of server
- Cooldown timers accurate to server
- Target lock works at 100m range

---

### WP-7-5: Zone Transition Client Handling
**Agent:** CLIENT  
**Duration:** 1 week  
**Dependencies:** WP-7-2, WP-7-3

**Deliverables:**
1. Seamless zone handoff (no loading screen)
2. Connection management to multiple zone servers
3. Entity visibility during aura transition
4. Position synchronization on handoff complete

**Interfaces:**
- Input: ZoneHandoff messages, AuraProjection events
- Output: Handoff acknowledgments, position confirmations

**Quality Gate:**
- Zone handoff <500ms perceived time
- No entity duplication or loss
- Smooth transition at zone boundaries

---

### WP-7-6: Client Performance Optimization
**Agent:** CLIENT  
**Duration:** 1 week  
**Dependencies:** WP-7-1, WP-7-2, WP-7-3

**Deliverables:**
1. LOD system for distant entities
2. Occlusion culling
3. Network bandwidth optimization (interest management)
4. GPU profiling integration

**Interfaces:**
- Input: AOI results, entity distances
- Output: Rendered frame, GPU metrics

**Quality Gate:**
- 100 entities at 60 FPS on mid-tier GPU
- <10ms GPU time for UI rendering
- <5 MB/s network downstream

---

## PHASE 8 WORK PACKAGES

### WP-8-1: Production Monitoring & Alerting
**Agent:** DEVOPS  
**Duration:** 2 weeks  
**Dependencies:** WP-6-1, WP-6-2, WP-6-3

**Deliverables:**
1. Prometheus metrics export
2. Grafana dashboards (player count, tick time, memory)
3. Alerting rules (high latency, memory, errors)
4. Distributed tracing (zone handoffs)

**Interfaces:**
- Input: Server metrics, performance counters
- Output: Metrics endpoint, alert notifications

**Quality Gate:**
- <5 second metric latency
- 99.9% alerting reliability
- Dashboards load <2 seconds

---

### WP-8-2: Security Audit & Hardening
**Agent:** SECURITY  
**Duration:** 2 weeks  
**Dependencies:** All Phase 6-7 WPs

**Deliverables:**
1. Code audit for all input validation
2. Packet encryption (GNS SRV)
3. Anti-cheat detection improvements
4. Rate limiting production tuning
5. Penetration test documentation

**Interfaces:**
- Input: All server network endpoints
- Output: Security recommendations, hardened configs

**Quality Gate:**
- Zero critical vulnerabilities
- All inputs validated and clamped
- DDoS mitigation effective at 10 Gbps

---

### WP-8-3: Chaos Engineering Framework
**Agent:** DEVOPS  
**Duration:** 2 weeks  
**Dependencies:** WP-6-5

**Deliverables:**
1. Random zone server kills
2. Network partition simulation
3. Database failover testing
4. Load spike generation
5. Automated recovery validation

**Interfaces:**
- Input: Kubernetes/Docker environment
- Output: Chaos test reports, recovery metrics

**Quality Gate:**
- Auto-recovery <30 seconds
- No data loss during failover
- 99.9% availability during chaos

---

### WP-8-4: Auto-Scaling Infrastructure
**Agent:** DEVOPS  
**Duration:** 2 weeks  
**Dependencies:** WP-8-1, WP-8-3

**Deliverables:**
1. Kubernetes operator for zone servers
2. Horizontal pod autoscaling based on player density
3. Zone splitting (divide overloaded zones)
4. Connection draining on scale-down

**Interfaces:**
- Input: Player density metrics, resource usage
- Output: Zone server pods, load balancer config

**Quality Gate:**
- Scale-up <60 seconds
- Scale-down without player disconnects
- Handle 10x load spike

---

### WP-8-5: Load Testing & Capacity Planning
**Agent:** DEVOPS  
**Duration:** 1 week  
**Dependencies:** WP-8-1, WP-8-4

**Deliverables:**
1. 1000-player bot swarm simulation
2. Capacity limits documentation
3. Bottleneck identification report
4. Scaling recommendations

**Interfaces:**
- Input: Production-like environment
- Output: Capacity report, performance baselines

**Quality Gate:**
- Sustained 1000 players for 24 hours
- <16ms tick time at 1000 players
- No memory leaks or crashes

---

## Parallel Workstreams

### Stream A: Networking & Databases (Phase 6)
**Can Start Immediately:** WP-6-1, WP-6-2, WP-6-3, WP-6-4  
**After Dependencies:** WP-6-5  
**Agent Types:** NETWORK, DATABASE, DEVOPS

### Stream B: Client Development (Phase 7)
**Can Start After:** WP-6-4 (protocol defined)  
**Parallel Work:** WP-7-1, WP-7-2, WP-7-3, WP-7-4  
**After Dependencies:** WP-7-5, WP-7-6  
**Agent Types:** CLIENT

### Stream C: Production Infrastructure (Phase 8)
**Can Start After:** WP-6-5 (integration complete)  
**Parallel Work:** WP-8-1, WP-8-2  
**After Dependencies:** WP-8-3, WP-8-4, WP-8-5  
**Agent Types:** DEVOPS, SECURITY

---

## Dependency Graph

```
Phase 6: Foundation
├── WP-6-4 (Protocol) ─────────────┐
├── WP-6-1 (GNS) ────────────────┤
├── WP-6-2 (Redis) ─────────────┤
└── WP-6-3 (ScyllaDB) ──────────┤
                             │
                             ▼
                    WP-6-5 (Integration)
                             │
                             ▼
Phase 7: Client ────────────────────────
├── WP-7-1 (Foundation) ────────────┐
├── WP-7-2 (Prediction) ────────────┤
├── WP-7-3 (Interpolation) ─────────┤
└── WP-7-4 (Combat UI) ───────────┤
                             │
                             ▼
              WP-7-5 (Zone Transition)
                             │
                             ▼
              WP-7-6 (Performance)
                             │
                             ▼
Phase 8: Production ──────────────────────
├── WP-8-1 (Monitoring) ────────────┐
└── WP-8-2 (Security) ────────────┤
                             │
                             ▼
              WP-8-3 (Chaos Testing)
                             │
                             ▼
              WP-8-4 (Auto-Scaling)
                             │
                             ▼
              WP-8-5 (Load Testing)
```

---

## Agent Assignment Strategy

### NETWORK_AGENT (2 agents recommended)
- Primary: WP-6-1, WP-6-4
- Secondary: WP-7-5 (zone networking)

### DATABASE_AGENT (1 agent)
- Primary: WP-6-2, WP-6-3
- Secondary: WP-8-1 (monitoring queries)

### CLIENT_AGENT (2 agents recommended)
- Primary: WP-7-1, WP-7-2, WP-7-3
- Secondary: WP-7-4, WP-7-5, WP-7-6

### SECURITY_AGENT (1 agent)
- Primary: WP-8-2
- Secondary: WP-6-1 (encryption), WP-8-3

### DEVOPS_AGENT (1-2 agents)
- Primary: WP-6-5, WP-8-1, WP-8-3, WP-8-4, WP-8-5
- Infrastructure for all phases

---

## Risk Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| GNS integration delays | Medium | High | Start WP-6-1 immediately, have fallback to enet |
| Godot GDExtension issues | Medium | High | C# fallback, or direct C++ module |
| ScyllaDB performance | Low | High | Cassandra compatibility allows switch |
| Client prediction desync | Medium | Medium | Extensive testing in WP-6-5 |
| Zone handoff edge cases | High | Medium | Comprehensive chaos testing in WP-8-3 |

---

## Success Metrics

### Phase 6
- 1000 concurrent UDP connections
- <1ms Redis latency
- 100k ScyllaDB writes/sec
- 80% bandwidth reduction via delta compression

### Phase 7
- 60 FPS client with 100 entities
- <50ms prediction error
- <500ms zone handoff time
- 5 MB/s max downstream

### Phase 8
- 99.9% uptime (7-day test)
- <30s auto-recovery
- 10 Gbps DDoS mitigation
- <60s auto-scaling

---

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-30 | AI Coordinator | Initial phased roadmap |

**Next Review:** After Phase 6 completion  
**Review Trigger:** Any WP completion or blocking issue
