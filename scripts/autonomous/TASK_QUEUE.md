# Improvement Task Queue

Prioritized list of autonomous improvements for DarkAges.

## P0 — Build/Test Health
- [ ] Verify full build compiles on Linux (currently MSVC-only CMake config)
- [ ] Fix any failing tests after build

## P1 — Code Quality: Raw Memory Management
- [ ] ScyllaManager: Replace raw `new WriteCallbackData` with `make_unique` (lines 533, 600, 658)

## P1 — Unimplemented TODOs (smallest scope first)
- [ ] ZoneServer:328 — Implement player state persistence stub
- [ ] ZoneServer:553 — Implement event packet broadcast
- [x] ZoneServer:639 — Implement respawn timer ✅ 2026-04-14 (PR #2)

## P1 — Combat System (core gameplay)
- [ ] CombatSystem — Implement ability system stub
- [ ] CombatSystem — Implement projectile/raycast stub

## P2 — Refactoring Candidates (large files)
- [ ] RedisManager.cpp (1655 lines) — extract connection pool to separate class
- [ ] ZoneServer.cpp (1632 lines) — extract player management, event handling

## P2 — Documentation
- [ ] Add inline documentation to key ECS components
- [ ] Document network protocol message format

## P3 — Anti-Cheat (ABSOLUTE LAST PRIORITY)
> Anti-cheat is deferred until all gameplay, networking, and infrastructure tasks are complete.
> Server authority is enforced by design — anti-cheat logging/detection is a polish pass, not a core requirement.
- [ ] ZoneServer:1088 — Implement anti-cheat event logging to database
- [ ] AntiCheat:357 — Implement collision detection stub

## Completed
- [x] ZoneServer:639 — Implement respawn timer ✅ 2026-04-14 (PR #2)
