# Parallel Agent Coordination Protocol

**Purpose:** Enable multiple AI agents to work simultaneously on DarkAges MMO  
**Version:** 1.0  
**Status:** Active

---

## Agent Work Assignment Matrix

### Current State (Phases 6-8)

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                         AGENT ASSIGNMENTS                              │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  NETWORK_AGENT_1 (Primary: WP-6-1, WP-6-4)                             │
│  ┌───────────────────────────────────────────────────────────────────────┐        │
│  │ Week 1-2: GameNetworkingSockets Integration (WP-6-1)            │        │
│  │ Week 3:   FlatBuffers Protocol Implementation (WP-6-4)          │        │
│  │ Week 7:   Zone Networking Support (WP-7-5 - assist CLIENT)      │        │
│  └───────────────────────────────────────────────────────────────────────┘        │
│                                                                         │
│  DATABASE_AGENT (Primary: WP-6-2, WP-6-3)                              │
│  ┌───────────────────────────────────────────────────────────────────────┐        │
│  │ Week 1-2: Redis Hot-State Integration (WP-6-2)                  │        │
│  │ Week 3-4: ScyllaDB Persistence Layer (WP-6-3)                   │        │
│  │ Week 9:   Monitoring Query Optimization (WP-8-1 - assist)       │        │
│  └───────────────────────────────────────────────────────────────────────┘        │
│                                                                         │
│  CLIENT_AGENT_1 (Primary: WP-7-1, WP-7-2, WP-7-5)                      │
│  ┌───────────────────────────────────────────────────────────────────────┐        │
│  │ Week 5-6:   Godot Project Foundation (WP-7-1)                   │        │
│  │ Week 7-8:   Client-Side Prediction (WP-7-2)                     │        │
│  │ Week 11:    Zone Transition Handling (WP-7-5)                   │        │
│  └───────────────────────────────────────────────────────────────────────┘        │
│                                                                         │
│  CLIENT_AGENT_2 (Primary: WP-7-3, WP-7-4, WP-7-6)                      │
│  ┌───────────────────────────────────────────────────────────────────────┐        │
│  │ Week 7-8:   Entity Interpolation (WP-7-3)                       │        │
│  │ Week 9-10:  Combat UI & Feedback (WP-7-4)                       │        │
│  │ Week 11:    Client Performance (WP-7-6)                         │        │
│  └───────────────────────────────────────────────────────────────────────┘        │
│                                                                         │
│  SECURITY_AGENT (Primary: WP-8-2)                                      │
│  ┌───────────────────────────────────────────────────────────────────────┐        │
│  │ Week 13-14: Security Audit & Hardening (WP-8-2)                  │        │
│  │ Week 15:     Penetration Testing Documentation                   │        │
│  │ Week 17:     GNS Encryption Review (WP-6-1 - assist)            │        │
│  └───────────────────────────────────────────────────────────────────────┘        │
│                                                                         │
│  DEVOPS_AGENT (Primary: WP-6-5, WP-8-1, WP-8-3, WP-8-4, WP-8-5)       │
│  ┌───────────────────────────────────────────────────────────────────────┐        │
│  │ Week 5:    Integration Testing Framework (WP-6-5)               │        │
│  │ Week 9-10: Production Monitoring (WP-8-1)                       │        │
│  │ Week 15-16: Chaos Engineering (WP-8-3)                          │        │
│  │ Week 17-18: Auto-Scaling (WP-8-4)                               │        │
│  │ Week 19:    Load Testing (WP-8-5)                               │        │
│  └───────────────────────────────────────────────────────────────────────┘        │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Work Package Dependencies

```
Phase 6 (Parallel Block 1 - Can Start Immediately)
══════════─────────────────────────────────────────────────────────══════════
WP-6-1 (GNS)       ║ WP-6-2 (Redis)     ║ WP-6-3 (Scylla)    ║ WP-6-4 (Protocol)
     ║                   ║                   ║                   ║
     ╚─────────────────┼─────────────────┼─────────────────┼─────────────────┘
                                                  ║
                                                  ▼
                                        WP-6-5 (Integration)
                                                  ║
                                                  ▼
Phase 7 (Parallel Block 2 - Starts After WP-6-4)
═══────────────────────────────────────────────────────────────────══════════
WP-7-1 (Foundation) ║ WP-7-2 (Prediction) ║ WP-7-3 (Interp)   ║ WP-7-4 (Combat UI)
     ║                   ║                   ║                   ║
     ╚─────────────────┼─────────────────┼─────────────────┼─────────────────┘
                                                  ║
                                                  ▼
                              WP-7-5 (Zone Handoff) + WP-7-6 (Perf)

Phase 8 (Parallel Block 3 - Starts After WP-6-5)
═══────────────────────────────────────────────────────────────────══════════
WP-8-1 (Monitoring) ║ WP-8-2 (Security)
     ║                   ║
     ╚─────────────────┼─────────────────┘
                         ║
                         ▼
            WP-8-3 (Chaos) + WP-8-4 (Auto-scale)
                         ║
                         ▼
                  WP-8-5 (Load Testing)
```

---

## Inter-Agent Communication Protocol

### 1. Daily Standup Format

Each agent posts daily:

```
[AGENT-TYPE] Daily Update - YYYY-MM-DD

Work Package: WP-X-Y
Status: [IN_PROGRESS|BLOCKED|COMPLETED]

Yesterday:
- Completed: [Specific deliverable]
- Issues encountered: [Any blockers]

Today:
- Planned: [Specific task]
- Dependencies needed: [From other agents]

Blockers:
- [None | WP-X-Y waiting for WP-A-B]

Interface Changes:
- [None | Modified function signature X]
```

### 2. Interface Change Notification

When modifying shared interfaces, agents MUST:

```
[INTERFACE CHANGE] - [AGENT-TYPE]

Affected Work Packages: WP-X-Y, WP-A-B
File: include/netcode/NetworkManager.hpp

Change:
- Old: void send(ConnectionID, std::span<uint8_t>)
- New: bool send(ConnectionID, std::span<uint8_t>, bool reliable)

Rationale: Need to distinguish reliable vs unreliable sends

Migration:
- WP-6-1 will update implementation
- WP-7-1 will update usage
- Backward compat: N/A (stub phase)

Deadline: 2026-02-15
```

### 3. Code Review Requirements

Before marking WP complete:

```
[CODE REVIEW REQUEST] WP-X-Y

Agent: [AGENT-TYPE]
Branch: feature/wp-x-y-short-name
Files Changed:
- src/server/...
- tests/...

Quality Gates Met:
- [x] All unit tests pass
- [x] No compiler warnings
- [x] Performance benchmarks met
- [x] Documentation updated

Reviewers Needed:
- NETWORK_AGENT for protocol changes
- SECURITY_AGENT for crypto changes

Test Command:
cd build && ctest -R wp_x_y_tests --output-on-failure
```

---

## Conflict Resolution

### File Ownership

| Directory | Primary Owner | Secondary |
|-----------|--------------|-----------|
| `src/server/netcode/` | NETWORK_AGENT | SECURITY_AGENT |
| `src/server/db/` | DATABASE_AGENT | DEVOPS_AGENT |
| `src/server/combat/` | PHYSICS_AGENT | SECURITY_AGENT |
| `src/client/` | CLIENT_AGENT | NETWORK_AGENT |
| `infra/` | DEVOPS_AGENT | All |
| `tests/` | All | DEVOPS_AGENT |

### Merge Conflict Resolution

1. **Both agents modify same file**
   - Rebase smaller change on larger change
   - Coordinate via interface change notification
   - Use feature flags if needed

2. **Interface disagreement**
   - Escalate to project coordinator
   - Document rationale for both approaches
   - Decision within 24 hours

3. **Test failures in integration**
   - DEVOPS_AGENT triages
   - Assign to responsible agent
   - Fix within 48 hours

---

## Testing Coordination

### Unit Test Ownership

| Test File | Owner | Run Command |
|-----------|-------|-------------|
| TestNetworkProtocol.cpp | NETWORK_AGENT | `./darkages_tests "[network]"` |
| TestRedis.cpp | DATABASE_AGENT | `./darkages_tests "[redis]"` |
| TestScyllaDB.cpp | DATABASE_AGENT | `./darkages_tests "[database]"` |
| TestGodotClient.cpp | CLIENT_AGENT | `godot --headless --test` |
| TestSecurity.cpp | SECURITY_AGENT | `./darkages_tests "[security]"` |
| TestMonitoring.cpp | DEVOPS_AGENT | `./darkages_tests "[monitoring]"` |

### Integration Test Schedule

| Week | Test | Lead Agent | Participants |
|------|------|------------|--------------|
| 5 | End-to-end connectivity | DEVOPS_AGENT | NETWORK, DATABASE |
| 10 | Client-server sync | DEVOPS_AGENT | NETWORK, CLIENT |
| 15 | Zone handoff | DEVOPS_AGENT | NETWORK, DATABASE, CLIENT |
| 20 | Full load test | DEVOPS_AGENT | All |

---

## Document Control

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-30 | Initial coordination protocol |

**Questions/Updates:** Post to #agent-coordination channel
