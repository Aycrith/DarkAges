# DarkAges Autonomous Iteration Framework

**Purpose:** Enable AI agents to autonomously improve the DarkAges codebase without introducing regressions.

## How It Works

```
┌──────────────────────────────────────────────────────────────────┐
│                    ITERATION CYCLE                                │
│                                                                   │
│  1. ANALYZE          2. PLAN             3. IMPLEMENT             │
│  ┌─────────┐        ┌──────────┐       ┌──────────────┐          │
│  │Codebase │───────▶│Prioritize│──────▶│Agent Dispatch│          │
│  │Audit    │        │Tasks     │       │(OpenCode/Claude)        │
│  └─────────┘        └──────────┘       └──────┬───────┘          │
│                                                │                  │
│  6. DOCUMENT        5. MERGE/PR          4. VALIDATE              │
│  ┌─────────┐        ┌──────────┐       ┌──────────────┐          │
│  │Update   │◀───────│Review &  │◀──────│Build + Test  │          │
│  │Tracker  │        │Merge     │       │Regression    │          │
│  └─────────┘        └──────────┘       └──────────────┘          │
└──────────────────────────────────────────────────────────────────┘
```

## Rules of Engagement

1. **Never push to main directly** — all changes go through branches + PRs
2. **Every change must build** — `cmake --build` must succeed before merge
3. **Every change must pass tests** — existing test suite must pass
4. **Incremental changes** — one focused improvement per branch
5. **Two-agent review** — one agent implements, another reviews
6. **Rollback on failure** — if build/tests fail, auto-revert the branch

## Improvement Categories

| Category | Priority | Description |
|----------|----------|-------------|
| Build Fixes | P0 | Fix compilation errors, missing deps |
| Test Coverage | P1 | Add missing tests, fix broken tests |
| Code Quality | P1 | Refactoring, const correctness, RAII |
| Performance | P2 | Hot path optimization, memory pooling |
| Documentation | P2 | Inline docs, API docs |
| Security | P1 | Input validation, DDoS hardening |
| Infrastructure | P2 | Docker, CI/CD, monitoring |

## Agent Roles

- **Analyzer**: Scans codebase, identifies improvement opportunities
- **Implementer**: Writes code (OpenCode or Claude Code)
- **Reviewer**: Reviews changes for quality and correctness
- **Validator**: Runs builds and tests, reports results
- **Orchestrator**: Coordinates all agents (Hermes)

## Files

- `scripts/autonomous/iteration_runner.sh` — Main orchestration script
- `scripts/autonomous/analyze.sh` — Codebase analysis
- `scripts/autonomous/validate.sh` — Build + test validation
- `AUTONOMOUS_LOG.md` — Running log of all iterations
