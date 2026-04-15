# Improvement Task Queue

Prioritized list of autonomous improvements for DarkAges.

## P0 — Build/Test Health
- [ ] Verify full build compiles on Linux (currently MSVC-only CMake config)
- [ ] Fix any failing tests after build

## P1 — Code Quality: Raw Memory Management
- [x] ScyllaManager: Replace raw `new WriteCallbackData` with `make_unique` (lines 533, 600, 658) ✅ 2026-04-15 (PR #4)

## P1 — Unimplemented TODOs (smallest scope first)
- [x] ZoneServer:328 — Implement player state persistence stub ✅ 2026-04-15 (PR #3)
- [x] ZoneServer:553 — Implement event packet broadcast ✅ 2026-04-15
- [x] ZoneServer:639 — Implement respawn timer ✅ 2026-04-14 (PR #2)

## P1 — Combat System (core gameplay)
- [x] CombatSystem — Implement ability system stub ✅ 2026-04-15 (commit 107ad78)
- [x] CombatSystem — Implement projectile/raycast stub ✅ 2026-04-15 (commit 107ad78)

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
