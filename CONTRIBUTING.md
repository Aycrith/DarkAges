# Contributing to DarkAges MMO

Thank you for contributing to DarkAges! This document outlines the workflow for multi-agent development across the client, server, and infrastructure components.

## Repository Structure

This is a **monorepo** containing all components of the DarkAges MMO:

```
DarkAges/
├── src/
│   ├── client/          # Godot 4.x C# client
│   ├── server/          # C++20 EnTT server
│   └── shared/          # Protocol definitions & constants
├── deps/                # Git submodules (EnTT, GLM)
├── infra/               # Docker, K8s, monitoring configs
├── tools/               # Stress tests, chaos engineering
└── docs/                # Architecture & protocol documentation
```

## Getting Started

### Clone with Submodules

```bash
git clone --recursive https://github.com/Aycrith/DarkAges.git
cd DarkAges
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

### Update Submodules

```bash
git submodule update --remote
```

## Agent Specializations

When contributing, identify with one of these agent roles in your commits:

| Agent | Scope | Example Commit |
|-------|-------|----------------|
| `[NETWORK]` | GameNetworkingSockets, serialization, compression | `[NETWORK] Add delta compression for position updates` |
| `[PHYSICS]` | EnTT ECS, spatial hash, movement | `[PHYSICS] Optimize spatial hash query performance` |
| `[DATABASE]` | Redis, ScyllaDB, persistence | `[DATABASE] Add async write batching` |
| `[CLIENT]` | Godot, prediction, interpolation | `[CLIENT] Implement client-side prediction` |
| `[SECURITY]` | Anti-cheat, validation, rate limiting | `[SECURITY] Add movement validation` |
| `[DEVOPS]` | Docker, CI/CD, monitoring | `[DEVOPS] Add Prometheus metrics exporter` |

## Branch Naming Convention

```
feature/[agent]-[description]
fix/[agent]-[description]
docs/[description]
```

Examples:
- `feature/network-delta-compression`
- `fix/physics-spatial-hash-crash`
- `docs/protocol-spec-update`

## Commit Message Format

```
[AGENT] Brief summary - Impact

- Detailed change 1
- Detailed change 2
- Performance/memory implications

Refs #[issue-number]
```

Example:
```
[NETWORK] Implement delta compression - Reduces bandwidth by 80%

- Added bit-packed position encoding
- Implemented delta-only updates for stationary entities
- Added fallback to full snapshot when delta > threshold

Bandwidth: 20 KB/s → 4 KB/s for 100 players
```

## Pull Request Process

1. **Create feature branch** from `main`
2. **Write tests** for new functionality
3. **Ensure build passes** locally: `./build.sh` or `build.ps1`
4. **Update documentation** if changing interfaces
5. **Submit PR** with clear description and agent tag
6. **Require review** from relevant agent domain

## Performance Budgets

All changes must respect these hard limits:

| Metric | Budget | Violation Action |
|--------|--------|------------------|
| Server tick | <16ms | Profile, optimize, or request exception |
| Memory/player | <512 KB | Review memory allocations |
| Downstream/player | <20 KB/s | Enable compression, reduce update rate |
| Build time | <5 min | Parallelize or cache dependencies |

## Code Style

### C++ (Server)
- C++20 standard
- No allocations during game tick
- Use EnTT sparse sets, not maps
- Fixed-point arithmetic for determinism

### C# (Client)
- Godot 4.x best practices
- Match server physics exactly
- 60 FPS minimum target

### Python (Tools)
- Type hints required
- Black formatting
- pytest for tests

## Testing Requirements

### Unit Tests (Catch2)
```cpp
TEST_CASE("Spatial Hash basic operations", "[spatial]") {
    // Your test here
}
```

### Integration Tests
```bash
# Run before submitting PR
./build.sh && ctest --output-on-failure
./tools/stress-test/bot_swarm.py --count 100 --duration 60
```

## Security Checklist

Before submitting:
- [ ] Input validation (clamp floats, check bounds)
- [ ] No sensitive data in logs
- [ ] Rate limiting for new endpoints
- [ ] Memory safety (no raw pointers without checks)

## Communication

### Cross-Agent Interface Changes

When changing interfaces between agents, create an **Interface Contract Comment**:

```cpp
// [AGENT-NETWORKING] Contract for PhysicsAgent:
// - Must call NetworkManager::broadcastEvent() within 1ms
// - Event timestamp must match physics tick
// - See NetworkManager.hpp:45 for latency requirements
```

### Issue Labels

- `network`, `physics`, `database`, `client`, `security`, `devops`
- `performance`, `bug`, `enhancement`, `documentation`
- `phase-0`, `phase-1`, etc.

## Questions?

See [AGENTS.md](AGENTS.md) for detailed agent context.
See [ImplementationRoadmapGuide.md](ImplementationRoadmapGuide.md) for technical specs.

## License

[Specify your license here]
