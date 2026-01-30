# DarkAges MMO - Local Testing & Validation Framework

**Status:** Phase 8 Blocker  
**Purpose:** Comprehensive validation before production hardening

This framework provides automated environment setup, service orchestration, and multi-client testing to validate that the DarkAges MMO server and client work correctly end-to-end.

## 🚨 Critical Requirement

**NO Phase 8 work can begin until this validation framework passes all tests.**

## Quick Start

```powershell
# Full validation (setup + services + tests)
.\tools\validation\run_validation_tests.ps1

# Skip setup if already done
.\tools\validation\run_validation_tests.ps1 -SkipSetup

# Keep services running after tests (for debugging)
.\tools\validation\run_validation_tests.ps1 -KeepServicesRunning
```

## Framework Components

### 1. Environment Setup (`setup_environment.ps1`)

Automatically installs and validates all dependencies:
- CMake, Git, Python
- Docker Desktop
- Godot 4.x
- EnTT and GLM dependencies
- Builds server and runs unit tests

```powershell
# Full setup
.\tools\validation\setup_environment.ps1

# Skip build step
.\tools\validation\setup_environment.ps1 -SkipBuild

# Skip Docker installation
.\tools\validation\setup_environment.ps1 -SkipDocker
```

### 2. Service Orchestrator (`orchestrate_services.ps1`)

Manages all required services with health checks:
- Redis (port 6379)
- ScyllaDB (port 9042)
- DarkAges Server (port 7777)

```powershell
# Start all services
.\tools\validation\orchestrate_services.ps1 -Start

# Check service status
.\tools\validation\orchestrate_services.ps1 -Status

# Restart all services
.\tools\validation\orchestrate_services.ps1 -Restart

# Stop all services
.\tools\validation\orchestrate_services.ps1 -Stop
```

### 3. Multi-Client Test Suite (`multi_client_test.py`)

Comprehensive test suite covering:

| Test | Description | Requirement |
|------|-------------|-------------|
| 01 | Single client connectivity | 100% success |
| 02 | 10 client movement validation | 80% connection, 80% movement |
| 03 | 50 player stress test | 80% connection, 90% survival |
| 04 | Entity synchronization | 80% see other entities |
| 05 | Packet loss recovery | Connection maintained |

```powershell
# Run all tests
python tools\validation\multi_client_test.py

# Run specific test
python tools\validation\multi_client_test.py --test 01

# Connect to different server
python tools\validation\multi_client_test.py --host 192.168.1.100 --port 7777
```

### 4. Master Validation Runner (`run_validation_tests.ps1`)

Orchestrates the complete validation workflow:
1. Environment setup (if needed)
2. Service startup with health checks
3. Multi-client test execution
4. Service cleanup
5. Report generation

## Success Criteria

| Metric | Minimum | Target |
|--------|---------|--------|
| Single client connect | 100% | 100% |
| 10 client connection | 80% | 100% |
| 10 client movement | 80% | 95% |
| 50 player connection | 80% | 90% |
| 50 player survival | 90% | 98% |
| Entity sync | 80% | 95% |
| Packet loss recovery | ✓ | ✓ |

## Output Files

| File | Description |
|------|-------------|
| `validation/.validation_status` | `PASSED` or `FAILED` marker |
| `validation/.setup_status` | `READY` when setup complete |
| `validation_report_*.json` | Detailed JSON test results |
| `validation_test_*.log` | Test execution log |
| `logs/setup.log` | Setup script log |
| `logs/server.log` | Server output |

## Test Report Format

```json
{
  "timestamp": "2026-01-30T12:34:56",
  "server": "127.0.0.1:7777",
  "summary": {
    "total": 5,
    "passed": 5,
    "failed": 0
  },
  "tests": [
    {
      "test_name": "single_client_connectivity",
      "passed": true,
      "start_time": 1234567890.0,
      "end_time": 1234567891.5,
      "clients_connected": 1,
      "clients_expected": 1,
      ...
    }
  ]
}
```

## Troubleshooting

### Docker Not Running
```powershell
# Start Docker Desktop manually, then re-run
.\tools\validation\run_validation_tests.ps1
```

### Server Binary Not Found
```powershell
# Build server first
.\tools\validation\setup_environment.ps1
```

### Connection Failures
```powershell
# Check service status
.\tools\validation\orchestrate_services.ps1 -Status

# View server logs
Get-Content logs/server.log -Tail 50
```

### Test Timeouts
- Ensure Docker has sufficient resources (4GB+ RAM allocated)
- Check Windows Defender/firewall isn't blocking port 7777
- Verify no other services using ports 6379, 9042, 7777

## CI/CD Integration

```yaml
# Example GitHub Actions workflow
- name: Run Validation Tests
  shell: pwsh
  run: |
    .\tools\validation\run_validation_tests.ps1 -SkipSetup
    if ($LASTEXITCODE -ne 0) { exit 1 }
```

## Architecture

```
run_validation_tests.ps1 (Master)
    ├── setup_environment.ps1
    │   ├── Install dependencies
    │   ├── Clone submodules
    │   └── Build project
    │
    ├── orchestrate_services.ps1
    │   ├── Start Redis (Docker)
    │   ├── Start ScyllaDB (Docker)
    │   └── Start DarkAges Server
    │
    └── multi_client_test.py
        ├── Test 01: Single client
        ├── Test 02: 10 client movement
        ├── Test 03: 50 player stress
        ├── Test 04: Entity sync
        └── Test 05: Packet loss recovery
```

## Development

To extend the validation framework:

1. Add new tests to `multi_client_test.py`
2. Follow the existing test pattern with `TestMetrics`
3. Update success criteria in this README
4. Add test to `run_validation_tests.ps1` summary output

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All validations passed |
| 1 | One or more validations failed |
| 2 | Environment setup incomplete |
