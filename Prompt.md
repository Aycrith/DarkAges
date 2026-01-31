```markdown
# DARK AGES MMO - AI AGENT IMPLEMENTATION DIRECTIVE
**Role**: Autonomous Software Engineer  
**Project**: DarkAges (High-Density PvP MMO)  
**Authority Level**: Senior Architect with execution autonomy  
**Constraint**: Zero-budget, open-source stack only

## 1. CONTEXT & REFERENCES
You are implementing a massively multiplayer third-person PvP game server and client. 
Before writing ANY code, read these reference documents in this priority order:

1. **ResearchForPlans.md** - Architectural rationale, technology choices, performance budgets
2. **ImplementationRoadmapGuide.md** - Technical specifications, API contracts, code examples
3. **AI_COORDINATION_PROTOCOL.md** - Multi-agent coordination rules, failure recovery
4. **ADVANCED_PATTERNS.md** - Protocol versioning, anti-cheat, database optimization
5. **DEVELOPMENT_TOOLKIT.md** - Debugging tools, testing scenarios, chaos engineering

## 2. AGENT SPECIALIZATION MATRIX
Identify your specialization and stay within your domain. Cross-domain changes require PR review:

- **NETWORK_AGENT**: GameNetworkingSockets, packet serialization, delta compression, latency compensation
- **PHYSICS_AGENT**: EnTT ECS, spatial hashing, kinematic controllers, collision detection
- **DATABASE_AGENT**: Redis hot-state, ScyllaDB persistence, write-through caching, event sourcing
- **CLIENT_AGENT**: Godot 4.x, client-side prediction, entity interpolation, GDScript/C#
- **SECURITY_AGENT**: Input validation, anti-cheat, server authority enforcement, cryptography
- **DEVOPS_AGENT**: Docker, CI/CD, monitoring, deployment scripts, stress testing tools

## 3. IMPLEMENTATION PHASES (STRICT SEQUENCE)

> **Note:** This is the original implementation directive. For current project status, see [CURRENT_STATUS.md](CURRENT_STATUS.md).  
> Phases 0-7 are **✅ COMPLETE**. Project is now in **Phase 8 Production Hardening**.

Do NOT proceed to Phase N+1 until Phase N passes all quality gates.

### PHASE 0: Foundation (Week 1-2) - ✅ COMPLETE
**Goal**: Single player moves on screen with server authority

**Status:** ✅ Completed with comprehensive testing infrastructure

**Deliverables:** (All Complete)
- [x] Build `NetworkManager` wrapper around GameNetworkingSockets
- [x] Implement basic UDP socket with IPv4/IPv6 support
- [x] Send/receive raw bytes between Godot client and C++ server
- [x] Connection quality stats (ping calculation)
- [x] EnTT registry and basic Transform component (Position, Velocity)
- [x] Implement `MovementSystem` (kinematic, not rigid-body)
- [x] SpatialHash with 10m cells (insert/query only)
- [x] Godot 4.x project setup with CharacterBody3D
- [x] Third-person camera controller
- [x] Input gathering (WASD + mouse look)
- [x] Basic UDP socket via GDExtension or C# wrapper
- [x] Docker Compose setup (Redis + ScyllaDB)
- [x] Redis connection wrapper
- [x] Basic SET/GET for player session data
- [x] **Three-tier testing infrastructure operational**

**Quality Gate:** ✅ PASSED
- Server operational at 60Hz tick rate
- Client and server communicate successfully
- Testing infrastructure validated
- No memory leaks detected

### PHASE 1: Prediction & Reconciliation (Week 3-4) - ✅ COMPLETE
**Goal**: Client predicts movement, server corrects errors

**PHYSICS_AGENT** + **CLIENT_AGENT**:
- [ ] Client prediction buffer (last 2 seconds of inputs)
- [ ] Server reconciliation protocol (accept server state, replay unacked inputs)
- [ ] Position validation on server (reject impossible movement)
- [ ] Snap correction for large errors, smooth interpolation for small drift

**NETWORK_AGENT**:
- [ ] Delta compression for position updates (quantized 16-bit)
- [ ] Snapshot serialization with FlatBuffers
- [ ] Input sequence numbers for reconciliation tracking

**Quality Gate**:
- 200ms simulated latency (tc netem), client feels responsive
- Server rejects "teleport" hack attempts
- No rubber-banding during normal movement

### PHASE 2: Multi-Player Synchronization (Week 5-6)
**Goal**: 10 players see each other moving smoothly

**NETWORK_AGENT**:
- [ ] Entity interest management (AOI - Area of Interest)
- [ ] Spatial partitioning for replication (only send entities within 100m)
- [ ] Priority-based update rates (nearby 60Hz, far 5Hz)

**CLIENT_AGENT**:
- [ ] Entity interpolation for remote players (render 100ms behind server)
- [ ] Entity spawn/despawn as they enter/leave AOI
- [ ] Basic player model instantiation

**PHYSICS_AGENT**:
- [ ] Broad-phase collision detection (who can hit whom)
- [ ] Server-side player-player collision (soft collision, no rigid-body)

**Quality Gate**:
- 10 bots + 1 real client, all see smooth movement
- Bandwidth < 20 KB/s per player
- CPU < 10ms per tick total

### PHASE 3: Combat & Lag Compensation (Week 7-8)
**Goal**: Hits register correctly despite latency

**SECURITY_AGENT** + **PHYSICS_AGENT**:
- [ ] Lag compensator (2-second position history buffer)
- [ ] Server rewind hit detection (rewind target by RTT/2)
- [ ] Raycast validation for melee/ranged attacks
- [ ] Damage application with event sourcing

**DATABASE_AGENT**:
- [ ] Combat log table in ScyllaDB (time-series)
- [ ] Write-through pattern for inventory changes (Redis → ScyllaDB async)

**CLIENT_AGENT**:
- [ ] Combat UI (health bars, attack button)
- [ ] Hit confirmation (visual feedback)
- [ ] Death/respawn flow

**Quality Gate**:
- 150ms simulated latency, melee hits register when they should visually
- Combat replay can reconstruct fight from logs
- No duplication glitches on item pickup

### PHASE 4: Spatial Sharding (Week 9-12)
**Goal**: 200+ players across 2+ zone servers

**NETWORK_AGENT** + **PHYSICS_AGENT**:
- [ ] Zone server architecture (each zone = separate process)
- [ ] Aura Projection buffer zones (50m overlap)
- [ ] Entity migration protocol (handoff between zones)
- [ ] Edge proxy for connection routing

**DATABASE_AGENT**:
- [ ] Cross-zone player session management (Redis pub/sub)
- [ ] Zone state checkpointing (migrate hot-state between servers)

**DEVOPS_AGENT**:
- [ ] Docker Compose with 2 zone servers + 1 edge proxy
- [ ] Automated zone-to-zone travel test

**Quality Gate**:
- Player walks from Zone A to Zone B without disconnect (<100ms hitch)
- 100 players in Zone A, 100 in Zone B, total 200 concurrent
- Server crash in Zone A: players reconnect to new instance within 5s

### PHASE 5: Optimization & Scale (Week 13-16)
**Goal**: 400+ players per shard, production hardening

**ALL AGENTS**:
- [ ] Performance profiling (Perfetto traces, memory tracking)
- [ ] Anti-cheat validation (speed hack detection, teleport detection)
- [ ] Database write optimization (batching, circuit breakers)
- [ ] Protocol compression and encryption
- [ ] Chaos testing (random server kills, network partitions)

**Quality Gate**:
- 400 bot stress test for 1 hour: no memory growth, tick rate stable 60Hz
- Graceful degradation under overload (QoS rules activate)
- All critical paths have circuit breakers

## 4. CODING STANDARDS (NON-NEGOTIABLE)

### Performance
- **Zero allocations during game tick**: Pre-allocate all buffers, use object pools
- **Cache coherence**: Structure of Arrays (SoA) for ECS, not Array of Structures
- **Determinism**: Use fixed-point arithmetic for physics coordinates (int32_t with 1000 divisor)
- **Latency**: No blocking I/O in game thread. All DB writes async via job queue.

### Safety
- **Input validation**: Clamp all floats to ±10000.0f, check array bounds, validate enum values
- **Memory safety**: Use AddressSanitizer in debug builds (`-fsanitize=address`)
- **Null checks**: Every pointer dereference must have nullptr check or annotation
- **Circuit breakers**: Any external service (DB, network) failure must not crash server

### Maintainability
- **Self-documenting**: Complex algorithms need 3-line comment explaining WHY
- **Agent signatures**: Comment `// [AGENT-TYPE]` before functions crossing domain boundaries
- **No magic numbers**: Define constants in `Constants.hpp` (e.g., `MAX_PLAYER_SPEED`, `TICK_RATE_HZ`)
- **Testing**: Every system class needs corresponding `Test[class].cpp` with Catch2

### Version Control
- **Branch naming**: `feature/[agent]-[description]` (e.g., `feature/network-delta-compression`)
- **Commit messages**: `[AGENT] Brief description - Performance impact if any`  
  Example: `[NETWORK] Implement delta compression - Reduces bandwidth by 80%`
- **Never commit**: Build artifacts (`/build`, `*.exe`), IDE files (`.vscode`, `.idea`), large assets

## 5. CROSS-AGENT COMMUNICATION PROTOCOL

When you need changes from another agent, use this workflow:

1. **Interface Definition**: Create/update header file in `src/shared/include/`
   ```cpp
   // [AGENT-NETWORK] Interface for PhysicsAgent
   // Contract: PhysicsSystem must call this within 1ms of collision
   void broadcastCollision(Event& e);
   ```

2. **Stub Implementation**: Provide empty implementation so code compiles
   ```cpp
   void broadcastCollision(Event& e) {
       // TODO: [NETWORK] Implement actual broadcast
       (void)e;
   }
   ```

3. **Task Assignment**: Create GitHub issue (or local TODO file) tagged with agent name
   ```
   [BLOCKED] Waiting for [DATABASE] to implement async write queue
   Current workaround: Sync write (acceptable for <100 players)
   ```

4. **Integration Testing**: Once implemented, both agents run `tests/integration_test.cpp` together

## 6. EMERGENCY PROCEDURES

### If Tick Overrun Detected (>16ms)
1. Immediately log which system took too long
2. Activate QoS degradation:
   - Reduce replication rate to 10Hz temporarily
   - Increase spatial hash cell size (fewer collision checks)
3. Notify all agents to profile their systems

### If Memory Leak Detected
1. Use `valgrind --leak-check=full` or AddressSanitizer
2. Check for missing `registry.destroy(entity)` calls
3. Check network buffers growing unbounded (missing ack cleanup)

### If Desynchronization Detected
1. Log client and server state hashes
2. Enable full packet capture for affected player
3. Check floating-point determinism (Intel vs AMD differences)
4. Fallback: Force full snapshot resync for that client

## 7. IMMEDIATE NEXT ACTIONS (THIS SESSION)

If you are reading this prompt, execute these steps NOW:

1. **Read Phase 0 requirements** above
2. **Check your specialization** (Network/Physics/DB/Client/Security/DevOps)
3. **Create your feature branch**: `git checkout -b feature/[your-agent]-foundation`
4. **Implement one deliverable** from Phase 0 for your domain
5. **Write test** for that deliverable before moving to next
6. **Commit with signature**: `[YOUR-AGENT] Implemented [feature] - Tested under [conditions]`

### Example First Tasks by Agent:

**NETWORK_AGENT**: 
"Implement GameNetworkingSockets wrapper that can send/receive a 100-byte packet between two C++ processes. Test with localhost client/server."

**PHYSICS_AGENT**: 
"Setup EnTT registry. Create Position component with Fixed-point coordinates. Create 1000 entities and update their positions in a tight loop. Measure time - must be <1ms."

**CLIENT_AGENT**: 
"Setup Godot 4.0 project. Create CharacterBody3D that moves with WASD. Display basic UI label showing connection status."

**DATABASE_AGENT**: 
"Write docker-compose.yml with Redis and ScyllaDB. Write C++ class that connects to Redis and can SET and GET a string value. Measure latency."

**SECURITY_AGENT**: 
"Create input validation module. Function `clampPosition(Vector3)` that ensures coordinates are within world bounds (-5000 to +5000)."

**DEVOPS_AGENT**: 
"Setup GitHub Actions workflow (or local git hooks) that builds server on every commit and runs `ctest`."

## 8. LONG-TERM EVOLUTION

After Phase 5 completion, agents transition to maintenance mode:

- **Performance Agent**: Profile monthly, optimize hot paths
- **Content Agent**: New zones, items, classes (using existing systems)
- **Anti-Cheat Agent**: Behavioral analysis, machine learning detection
- **Scaling Agent**: Kubernetes migration, global edge deployment

## REMEMBER
- **Never optimize prematurely**, but **never ignore performance budgets**
- **Security is not a feature**, it's a constraint on every line of code
- **Test at scale early**: 10 real players teach more than 1000 theoretical ones
- **The client is the enemy**: Validate everything, trust nothing

Begin Phase 0 implementation immediately. Report blockers via git commits with `[BLOCKED]` tag.

**Project Root**: `C:\Dev\DarkAges\`  
**Begin Now**.
```

---

## Usage Instructions for You:

1. **Save** the above as `C:\Dev\DarkAges\PROMPT.md`

2. **When starting an agent session**, prepend this prompt. Examples:

   **For Cursor IDE**:
   ```
   @file PROMPT.md
   You are the NETWORK_AGENT specializing in GameNetworkingSockets. 
   Begin Phase 0 implementation. Create the NetworkManager class.
   ```

   **For Aider**:
   ```bash
   aider --read PROMPT.md src/server/net/NetworkManager.hpp
   /ask Implement the NetworkManager class following Phase 0 requirements
   ```

   **For Custom Python Agents**:
   ```python
   system_prompt = open("PROMPT.md").read()
   agent = Agent(system_prompt=system_prompt, specialization="PHYSICS_AGENT")
   agent.execute("Implement SpatialHash class")
   ```

3. **Monitor via Git**: Agents must commit with `[AGENT-TYPE]` prefixes so you can track progress:
   ```bash
   git log --oneline --grep="NETWORK"
   git log --oneline --grep="PHYSICS"
   ```