# DarkAges Autonomous Iteration Log

All autonomous improvements tracked here. Most recent first.


### ✅ 2026-04-17 11:36 UTC — Implement Adaptive Rate Limiting
- **Task:** Implement the TODO in AdaptiveRateLimiter::allow() — apply effectiveRate to underlying limiter
- **Branch:** autonomous/connection-pool-tests
- **Build:** PASS
- **Tests:** PASS (392 cases, 2752 assertions)
- **PR:** https://github.com/Aycrith/DarkAges/pull/9
- **Changes:**
  - Added `TokenBucketRateLimiter::setTokensPerSecond()` for runtime rate adjustment
  - Updated `AdaptiveRateLimiter::allow()` to call `limiter_.setTokensPerSecond(effectiveRate)`
  - Initialized AdaptiveRateLimiter's internal limiter with config's normalRate (was using defaults)
  - 3 new test cases: token rate change, load-based adjustment, enforcement under load
  - Fixed [[nodiscard]] compiler warning in TestViolationTracker.cpp

### ✅ 2026-04-17 — Test Coverage: PositionHistory, CircuitBreaker, ViolationTracker
- **Task:** Add unit tests for previously untested components
- **Status:** SUCCESS
- **Changes:** 3 new test files + 1 bugfix:
  - `TestPositionHistory.cpp` (529 lines, 25 test cases, 115 assertions)
    - PositionHistory: record, circular eviction, time-based eviction, binary search, interpolation, clear
    - LagCompensator: record, retrieve, validateHit (within/outside radius), missing history, rewind limit
  - `TestCircuitBreaker.cpp` (246 lines, 15 test cases, 46 assertions)
    - State machine: CLOSED, OPEN, HALF_OPEN transitions
    - forceState() fix: reset lastFailureTime so OPEN state correctly rejects requests immediately
  - `TestViolationTracker.cpp` (283 lines, 20 test cases, 51 assertions)
    - Profile management: create, retrieve, remove
    - Statistics: incrementDetections, getDetectionCount, resetStatistics
    - Callbacks: onCheatDetected, onPlayerBanned, onPlayerKicked
    - Severity handling: CRITICAL kicks, BAN bans, INFO just logs
  - Fixed: `CircuitBreaker.cpp` - forceState(OPEN) now resets lastFailureTime so allowRequest() correctly returns false
- **Validation:** Build PASS, Tests PASS (344 test cases, 332 passed, 10 skipped, 2355 assertions, 0 failures)
- **Coverage increased:** 23 -> 20 untested source files

### ✅ 2026-04-17 — Branch Cleanup + MovementValidator Tests
- **Task:** Review all PRs, merge unmerged work, clean up stale branches
- **Status:** SUCCESS
- **Branch:** autonomous/20260417-movement-validator-tests (deleted after merge)
- **Changes:**
  1. Pushed protobuf guard fix to main (was stuck — gh auth restored)
  2. Saved + fixed 682-line TestMovementValidator.cpp (12 test cases, 87 assertions)
     - Fixed missing `entity` in "Speed at exactly the limit" SECTION
     - Fixed float precision boundary checks (fixed-point rounding ±0.01f)
     - Suppressed unused variable warning
  3. Deleted stale branches: autonomous/test-pubsub-manager, autonomous/20260417-movement-validator-tests
  4. Pruned remote refs — repo now clean (only main)
- **PRs verified merged:** #1, #2, #3, #4, #5 — all MERGED
- **Validation:** Build PASS, Tests PASS (275 passed, 10 skipped, 2181 assertions)
### ✅ 2026-04-15 — DDoSProtection Refactor: Extract CircuitBreaker, InputValidator, TrafficAnalyzer, ConnectionThrottler
- **Task:** Refactor DDoSProtection.cpp (717 lines) + DDoSProtection.hpp (414 lines) by extracting cohesive subsystems into separate files
- **Status:** SUCCESS
- **Branch:** `autonomous/ddos-refactor`
- **Changes:** 14 files, +772/-649 lines. Extracted 5 subsystems:
  - `CircuitBreaker.hpp/.cpp` (174 lines) - external service circuit breaker pattern
  - `InputValidator.hpp/.cpp` (98 lines) - position, rotation, packet, protocol validation
  - `TrafficAnalyzer.hpp/.cpp` (212 lines) - traffic pattern analysis and attack detection
  - `RateLimiter.hpp` (+133 lines) - added TokenBucketRateLimiter template + ConnectionThrottler class
  - `ConnectionThrottler.cpp` (137 lines) - per-IP connection throttling + AdaptiveRateLimiter
- **DDoSProtection reduction:** .hpp 414→148 (64%), .cpp 717→335 (53%)
- **Architecture:** DDoSProtection remains a facade with identical public API
- **Validation:** Build PASS, Tests PASS (190 passed, 10 skipped Redis, 1475 assertions)
- **Public API:** Unchanged - added includes to TestDDoSProtection.cpp only

---

### ✅ 2026-04-15 — AntiCheat Refactor: Extract MovementValidator and ViolationTracker
- **Task:** Refactor AntiCheat.cpp (922 lines) by extracting logically cohesive groups into separate classes/files
- **Status:** SUCCESS
- **Branch:** `autonomous/anticheat-refactor`
- **Changes:** 8 files, +1106/-877 lines. Extracted 3 new files:
  - `AntiCheatTypes.hpp` (152 lines) - shared enums, structs, callbacks (breaks circular dependency)
  - `MovementValidator.hpp/.cpp` (478 lines) - speed/teleport/fly/noclip detection + position helpers
  - `ViolationTracker.hpp/.cpp` (281 lines) - profile management, trust scores, ban/kick, statistics
- **AntiCheat reduction:** .hpp 403→195 (52%), .cpp 922→446 (52%)
- **Architecture:** AntiCheatSystem now a facade delegating to MovementValidator + ViolationTracker
- **Validation:** Build PASS, Tests PASS (190 passed, 10 skipped Redis)
- **Public API:** Unchanged - zero test modifications needed

---

### ✅ 2026-04-16 — PlayerManager Unit Tests
- **Task:** Add unit tests for PlayerManager
- **Status:** SUCCESS
- **Branch:** `feature/player-manager-tests`
- **Changes:** 2 files, +501 lines. 15 test cases, 168 assertions covering register/unregister, lookup, lifecycle, state management
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — ZoneHandoff Tests + Iterator Bugfix
- **Task:** Add unit tests for ZoneHandoffController
- **Status:** SUCCESS
- **Branch:** `feature/zone-handoff-tests`
- **Changes:** 2 files, +741/-20 lines. 13 test cases, 85 assertions. **Fixed real UB:** erase during range-for in update()
- **Coverage:** Handoff phases, initiation, cancellation, timeouts, concurrent handoffs, statistics
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — CrossZoneMessenger Unit Tests
- **Task:** Add unit tests for CrossZoneMessenger
- **Status:** SUCCESS
- **Branch:** `feature/crosszone-messenger-tests`
- **Changes:** 1 file, +695 lines. 15 test cases, 165 assertions covering serialization, message filtering, callbacks
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — MetricsExporter Comprehensive Unit Tests
- **Task:** Add unit tests for MetricsExporter (no existing test file)
- **Status:** SUCCESS
- **Branch:** `feature/metrics-exporter-tests`
- **Changes:** 3 files, +399 lines. TestMetricsExporter.cpp (16 test cases, 66 assertions), registered in root CMakeLists.txt.
- **Coverage:** Counter (increment/labeled/serialize), Gauge (set/inc/dec/labels), Histogram (observe/buckets/labels), MetricsExporter singleton, ScopedTimer RAII
- **Pitfalls:** Private constructor (singleton), `{}` initializer ambiguity, histogram setprecision(4) format
- **Validation:** Build PASS, Tests PASS (9 suites, all passing)

---

### ✅ 2026-04-16 — NetworkManager Raw New Replacement
- **Task:** Replace raw `new` with `std::make_unique` in NetworkManager callback (line 112)
- **Status:** SUCCESS
- **Branch:** `feature/network-manager-unique-ptr`
- **Changes:** 1 file, +40/-8 lines. Added MoveOnlyFunction wrapper, replaced raw new/delete with make_unique
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — Adjacent Zone Loading for AuraProjectionManager
- **Task:** Populate adjacentZones vector (replaces TODO at ZoneServer.cpp:73)
- **Status:** SUCCESS
- **Branch:** `feature/adjacent-zone-loading`
- **Changes:** 1 file, +39/-2 lines. Grid-based 2x2 adjacency computation, 8-directional neighbor detection
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — MetricsExporter Comprehensive Unit Tests
- **Task:** Add unit tests for MetricsExporter (no existing test file)
- **Status:** SUCCESS
- **Branch:** `feature/metrics-exporter-tests`
- **Changes:** 3 files, +399 lines. TestMetricsExporter.cpp (16 test cases, 66 assertions), registered in root CMakeLists.txt.
- **Coverage:** Counter (increment/labeled/serialize), Gauge (set/inc/dec/labels), Histogram (observe/buckets/labels), MetricsExporter singleton, ScopedTimer RAII
- **Pitfalls:** Private constructor (singleton), `{}` initializer ambiguity, histogram setprecision(4) format
- **Validation:** Build PASS, Tests PASS (9 suites, all passing)

---

### ✅ 2026-04-16 — NetworkManager Raw New Replacement
- **Task:** Replace raw `new` with `std::make_unique` in NetworkManager callback (line 112)
- **Status:** SUCCESS
- **Branch:** `feature/network-manager-unique-ptr`
- **Changes:** 1 file, +40/-8 lines. Added MoveOnlyFunction wrapper, replaced raw new/delete with make_unique
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — Adjacent Zone Loading for AuraProjectionManager
- **Task:** Populate adjacentZones vector (replaces TODO at ZoneServer.cpp:73)
- **Status:** SUCCESS
- **Branch:** `feature/adjacent-zone-loading`
- **Changes:** 1 file, +39/-2 lines. Grid-based 2x2 adjacency computation, 8-directional neighbor detection
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — AbilitySystem Full Implementation
- **Task:** Replace AbilitySystem stubs with real casting, cooldown, mana logic; wire into CombatSystem
- **Status:** SUCCESS
- **Branch:** `feature/ability-system-implementation`
- **Changes:** 5 files, +332/-25 lines.
  1. castAbility(): validates ability, checks mana/range/cooldown, applies damage/heal effects
  2. getAbility(): real lookup by abilityId from registered abilities
  3. hasMana(): checks Mana component on caster entity
  4. Wire CombatSystem ABILITY case (replaces TODO stub)
  5. Expands tests from 3 to 6 cases with 23 assertions
- **Pitfall:** Agent added abilityId to AbilityDefinition constructor but tests used old 5-arg signature — fixed by adding 5-arg overload.
- **Validation:** Build PASS, Tests PASS (127 cases, 829 assertions)

---

### ✅ 2026-04-16 — Anti-Cheat No-Clip Collision Detection
- **Task:** Implement detectNoClip() using SpatialHash for world geometry collision
- **Status:** SUCCESS
- **Branch:** `feature/ability-system-implementation`
- **Changes:** 4 files, +297/-5 lines.
  1. Added SpatialHash* member to AntiCheatSystem with setSpatialHash() setter
  2. detectNoClip(): queries spatial hash, checks XZ circle + Y-axis overlap with StaticTag+BoudingVolume
  3. Wired spatial hash in ZoneServer::initializeAntiCheat
  4. 5 test cases, 21 assertions
- **Pitfalls:**
  - EnTT tag components use `registry.has<T>()` not `try_get<T>()` — wrong EnTT API used
  - ViolationSeverity::HIGH doesn't exist — should use CRITICAL
  - Test distances too fast for speed hack detection — needed 1000ms dtMs
  - Friend class approach for private method broke — rewrote tests to use public validateMovement()
- **Validation:** Build PASS, Tests PASS (130 cases, 841 assertions)

---

### ✅ 2026-04-16 — sendCombatEvent Packet Sending
- **Task:** Send EventPacket to clients in ZoneServer::sendCombatEvent
- **Status:** SUCCESS
- **Branch:** `feature/ability-system-implementation`
- **Changes:** 1 file, +18/-4 lines. Uses ProtobufProtocol::createDamageEvent pattern from validateAndApplyInput.
- **Validation:** Build PASS, Tests PASS (130 cases, 841 assertions)

---

### ✅ 2026-04-15 — PacketValidator Test Fixes
- **Task:** Fix 4 test failures in TestPacketValidator.cpp
- **Status:** SUCCESS
- **Branch:** `autonomous/respawn-timer`
- **Changes:** 1 file, +17/-9 lines.
  1. ClampPosition: compared fixed-point values against fixed-point-converted bounds (1000x offset); now uses float world bounds directly
  2. ValidateSpeed: missing negative speed rejection
  3. ValidateAbilityId: rejected ID 0 (basic attack); now allows it
  4. ValidatePlayerName: auto-fixed empty names; now rejects them
- **Validation:** Build PASS, Tests PASS (104 cases, 789 assertions)

---

### ✅ 2026-04-14 — Respawn Timer Implementation
- **Task:** Implement respawn timer (replaces TODO at ZoneServer.cpp:639)
- **Status:** SUCCESS
- **Branch:** `autonomous/respawn-timer`
- **PR:** https://github.com/Aycrith/DarkAges/pull/2
- **Changes:** 2 files, +48 lines. Added PendingRespawn queue, 5s delay, health restore, spawn teleport.
- **Validation:** Build PASS, Tests PASS (96 cases, 742 assertions)

---

### ✅ 2026-04-14 — Linux Build Fix
- **Task:** Fix compilation on Linux (g++ C++20)
- **Status:** SUCCESS
- **Branch:** `autonomous/fix-linux-build`
- **PR:** https://github.com/Aycrith/DarkAges/pull/1
- **Changes:** 3 files, +45/-5 lines. Missing includes, conditional test compilation, incomplete Redis stub.
- **Validation:** Build PASS, Tests PASS (96 cases, 742 assertions)

---


---

### ✅ 2026-04-15 — Anti-Cheat Event Logging + Ban Persistence
- **Task:** Implement anti-cheat event logging to ScyllaDB and ban persistence to Redis
- **Status:** SUCCESS
- **Branch:** `feature/security-anticheat-logging`
- **Changes:** 4 files, +130/-7 lines. Added AntiCheatEvent struct, logAntiCheatEvent/logAntiCheatEventsBatch methods in ScyllaManager, implemented in ZoneServer::onCheatDetected, Redis ban storage.
- **Validation:** Build PASS, Tests PASS (124 cases, 817 assertions)

---

### ✅ 2026-04-15 — Damage/Hit Packet Sending
- **Task:** Send damage events and hit confirmations to clients
- **Status:** SUCCESS
- **Branch:** `feature/network-damage-packet-sending`
- **Changes:** 1 file, +21/-5 lines. Used ProtobufProtocol::createDamageEvent() to send to target and attacker.
- **Validation:** Build PASS, Tests PASS (124 cases, 817 assertions)

---

### ✅ 2026-04-15 — RedisManager Connection Pool Extraction
- **Task:** Extract connection pool from RedisManager.cpp (1655 lines)
- **Status:** SUCCESS
- **Branch:** `feature/refactor-connection-pool`
- **Changes:** 7 files, +313/-204 lines. Created ConnectionPool.hpp/cpp (289 lines), reduced RedisManager by 206 lines.
- **Pitfall:** Agent modified src/server/CMakeLists.txt instead of root CMakeLists.txt — fixed manually.
- **Validation:** Build PASS, Tests PASS (124 cases, 817 assertions)


---

### ✅ 2026-04-16 — Collision Layer Masks and Team Filtering
- **Task:** Implement BroadPhaseSystem::canCollide() with layer masks and team checks
- **Status:** SUCCESS (OpenCode agent + manual fix for EnTT `has` → `all_of`)
- **Branch:** `autonomous/spatial-layer-masks`
- **Changes:** 3 files, +278/-13 lines. Added CollisionLayer component with bitmask layers, CollisionLayerMask namespace, factory methods, auto-assignment by tag, bidirectional mask check, same-team filtering, projectile-owner exclusion.
- **Pitfall:** OpenCode used `registry.has<T>()` — EnTT version requires `registry.all_of<T>()`
- **Validation:** Build PASS, Tests PASS (131 cases, 859 assertions — +1 test, +18 assertions)

---

### ✅ 2026-04-16 — Aura Sync Serialization and Zone Ownership Transfer
- **Task:** Implement aura entity state sync via Redis and zone ownership transfer notifications
- **Status:** SUCCESS (manual implementation — OpenCode timed out 2x at 300s)
- **Branch:** `autonomous/aura-zone-transfers`
- **Changes:** 1 file, +57/-13 lines. syncAuraState() now serializes AuraEntityState batch into ZoneMessage payload and publishes via redis_->publishToZone(). handleAuraEntityMigration() sends MIGRATION_COMPLETE to previous zone and updates Redis entity:zone key.
- **Note:** OpenCode CLI struggled with this codebase's C++ complexity — timed out twice with no output before 300s. Implemented manually.
- **Validation:** Build PASS, Tests PASS (131 cases, 859 assertions)
- **Remaining TODOs:** 1 (config population at ZoneServer.cpp:73 — infrastructure-dependent, low priority)


---

### ✅ 2026-04-16 — MetricsExporter Unit Tests
- **Task:** Add comprehensive unit tests for MetricsExporter
- **Status:** SUCCESS
- **Branch:** `feature/metrics-exporter-tests`
- **Changes:** 2 files, +399 lines. 16 test cases, 66 assertions
- **Validation:** Build PASS, Tests PASS

---

### ✅ 2026-04-16 — ProtobufProtocol Unit Tests
- **Task:** Add unit tests for ProtobufProtocol
- **Status:** SUCCESS
- **Branch:** `feature/protobuf-protocol-tests`
- **Changes:** 2 files, +740 lines. 10 test cases, 198 assertions covering all packet types, roundtrip serialization, boundary values, EntityState quantization
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — GNSNetworkManager Unit Tests
- **Task:** Add unit tests for GNSNetworkManager (GNS-conditional)
- **Status:** SUCCESS
- **Branch:** `feature/gns-network-manager-tests`
- **Changes:** 1 file, +410 lines. 31 test cases — type tests always available, implementation tests behind ENABLE_GNS
- **Validation:** Build PASS, Tests PASS (9 suites)

---

### ✅ 2026-04-16 — Batch Merge: 8 Feature Branches
- **Task:** Merge all 8 pending feature branches into main, resolve conflicts, verify build+tests
- **Status:** SUCCESS
- **Branches merged:** feature/network-manager-unique-ptr, feature/adjacent-zone-loading, feature/metrics-exporter-tests, feature/player-manager-tests, feature/zone-handoff-tests, feature/crosszone-messenger-tests, feature/protobuf-protocol-tests, feature/gns-network-manager-tests
- **Conflicts resolved:** AUTONOMOUS_LOG.md (metrics-exporter), CMakeLists.txt (metrics-exporter, protobuf-protocol)
- **Validation:** Build PASS, Tests PASS — **200 test cases, 190 passed, 10 skipped, 1475 assertions all passing**
- **Cleanup:** Deleted stale branches (autonomous/ability-system-stub, autonomous/projectile-raycast) + all merged feature branches (local+remote)
- **Test growth:** 131 cases / 859 assertions → 200 cases / 1475 assertions (+69 cases, +616 assertions)

---

### ✅ 2026-04-16 — ZoneServer Refactoring (CombatEventHandler + AuraZoneHandler)
- **Task:** Extract cohesive handler classes from ZoneServer.cpp (1833 lines)
- **Status:** SUCCESS (OpenCode agent)
- **Branch:** `autonomous/zoneserver-refactor`
- **Changes:** 6 files, +974/-56 lines. Extracted CombatEventHandler (~280 lines: combat, death, respawns) and AuraZoneHandler (~330 lines: aura sync, zone transitions, migration, handoffs)
- **Validation:** Build PASS, Tests PASS (200 cases, 190 passed, 1475 assertions)

---

### ✅ 2026-04-16 — RedisManager Refactoring (PlayerSessionManager + PubSubManager)
- **Task:** Extract cohesive manager classes from RedisManager.cpp (1455 lines)
- **Status:** SUCCESS (OpenCode agent, manual fix for duplicate type definitions)
- **Branch:** `autonomous/redismanager-refactor`
- **Changes:** 7 files, +765/-53 lines. Extracted PlayerSessionManager (session CRUD), PubSubManager (pub/sub, zone messaging), RedisInternal (shared state header). Removed duplicate PlayerSession/AsyncResult/ZoneMessage definitions from RedisManager.hpp.
- **Validation:** Build PASS, Tests PASS (200 cases, 190 passed, 1475 assertions)

---

### ✅ 2026-04-16 — ScyllaManager Refactoring (AntiCheatLogger + CombatEventLogger)
- **Task:** Extract cohesive logging classes from ScyllaManager.cpp (1105 lines)
- **Status:** SUCCESS (OpenCode agent)
- **Branch:** `autonomous/scyllamanager-refactor`
- **Changes:** 8 files, reduced ScyllaManager from 1105 → 791 lines (28% reduction). Extracted AntiCheatLogger (167 lines + stub), CombatEventLogger (654 lines + stub). All stub files created.
- **Validation:** Build PASS, Tests PASS (200 cases, 190 passed, 1475 assertions)

### ✅ 2026-04-15 21:04 UTC
- **Task:** Remove unused variable `positionCorrected` in ZoneServer.cpp (fix -Wunused-but-set-variable warning)
- **Status:** SUCCESS
- **Branch:** `autonomous/redismanager-refactor-v2`
- **Changes:** 1 file, -4 lines in `src/server/src/zones/ZoneServer.cpp`
- **Tests:** 190 passed, 10 skipped (Redis unavailable), 1475 assertions


### ✅ 2026-04-17 05:25 UTC
- **Task:** Fix Linux build — protobuf guard mismatch + duplicate stub symbols
- **Branch:** autonomous/20260417-fix-proto-guard
- **Build:** PASS
- **Tests:** PASS (263 cases, 2091 assertions)
- **PR:** Pushed directly to main (commit 38a5675)
- **Changes:**
  1. CMakeLists.txt: Changed `ENABLE_PROTOBUF=1` to `DARKAGES_HAS_PROTOBUF=1` (source code checks `#ifdef DARKAGES_HAS_PROTOBUF`)
  2. ScyllaManager_stub.cpp: Removed duplicate CombatEventLogger/AntiCheatLogger stub implementations (kept only in dedicated *_stub.cpp files)
  3. Created ZoneMessage.cpp: Moved serialize/deserialize from PubSubManager.cpp (only compiled with Redis) to always-compiled file
  4. Added ZoneMessage.cpp to SERVER_SOURCES (always compiled, no Redis dependency)
  5. Fixed stub isConnected() to return false (correct semantics for unavailable services)
- **Pushed:** 2026-04-17 by Hermes (manual session — gh auth restored)

### ✅ 2026-04-17 09:29 UTC — CircuitBreaker Unit Tests
- **Task:** Add unit tests for CircuitBreaker class (previously empty stub)
- **Branch:** autonomous/20260417-circuit-breaker-tests
- **Build:** PASS
- **Tests:** PASS (346 cases, 2550 assertions total — +21 new cases from before)
- **PR:** https://github.com/Aycrith/DarkAges/pull/7
- **Changes:** Filled in TestCircuitBreaker.cpp with 18 test cases covering:
  - Initialization (default + custom config)
  - CLOSED state (allow requests, success resets failure count, threshold tripping)
  - OPEN state (reject requests, ignore failures/successes)
  - HALF_OPEN state (limited calls, success recovery, failure reopens)
  - forceState (counter reset, state forcing)
  - Config variations (zero threshold, high threshold resilience)
  - Full lifecycle CLOSED → OPEN → HALF_OPEN → CLOSED
  - Thread safety (basic concurrent access)
- **Also merged:** Test coverage branch (InputValidator, RateLimiter, TrafficAnalyzer tests from PR#6)

### ✅ 2026-04-17 10:29 UTC
- **Task:** Fill empty test stubs — CircuitBreaker, PositionHistory, ViolationTracker
- **Branch:** autonomous/20260417-empty-test-stubs
- **Build:** PASS
- **Tests:** PASS (384 cases, 2645 assertions — +57 new test cases)
- **PR:** https://github.com/Aycrith/DarkAges/pull/8
- **Changes:**
  1. TestCircuitBreaker.cpp (0 → 16 tests): state machine transitions, forceState, custom config, edge cases, full lifecycle
  2. TestPositionHistory.cpp (0 → 22 tests): PositionHistory buffer ops, exact/interpolated lookup, time window, eviction; LagCompensator record/retrieve, multi-entity, remove, validateHit
  3. TestViolationTracker.cpp (0 → 19 tests): profile management, behavior tracking, severity enforcement (kick/ban/info), callbacks, manual enforcement, statistics, config
  4. Fixed Position helpers in tests to use FLOAT_TO_FIXED (Position stores raw fixed-point, not world units)
  5. Corrected darkades-codebase-conventions skill — Position stores raw fixed-point values, not world units as previously documented
