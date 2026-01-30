# Pre-Phase 8 Remediation Plan

## Validation Result: NO-GO ❌

The validation test suite identified critical blockers that must be resolved before proceeding to Phase 8 (Production Hardening).

---

## Critical Issues Found

### 1. Server Binary Non-Functional (P0 BLOCKER)
**Issue:** The server binary exits immediately after basic verification. It does not start a network service.

**Root Cause:** 
- `src/server/src/main.cpp` is a minimal verification stub
- NetworkManager uses stub implementation instead of GameNetworkingSockets
- No actual game loop or network listening

**Fix Required:**
- Implement proper `ZoneServer::run()` main loop
- Replace NetworkManager_stub.cpp with full GNS implementation
- Wire up all systems (ECS, physics, networking)

**Effort:** 8-12 hours

---

### 2. External Dependencies Not Integrated (P0 BLOCKER)
**Issue:** CMake builds with stub implementations instead of real libraries.

**Missing:**
- GameNetworkingSockets (UDP networking)
- FlatBuffers (protocol serialization)
- hiredis (Redis client)
- cassandra-cpp-driver (ScyllaDB client)

**Root Cause:**
- `CMakeLists.txt` has FetchContent but conditional compilation defaults to stubs
- External libraries not downloading or linking properly
- Header paths not configured

**Fix Required:**
1. Fix CMakeLists.txt to properly fetch and link dependencies
2. Update include paths
3. Ensure libraries compile on Windows
4. Replace all stub files with real implementations

**Effort:** 12-16 hours

---

### 3. Docker Not Installed (P1)
**Issue:** ScyllaDB cannot run without Docker.

**Fix Options:**
- Option A: Install Docker Desktop (requires WSL2, system restart)
- Option B: Run ScyllaDB natively (complex on Windows)
- Option C: Skip ScyllaDB for local testing (use stub persistence)

**Recommendation:** Option A for full validation, Option C for rapid iteration

**Effort:** 2-4 hours (Docker install + WSL2 setup)

---

### 4. Script Encoding Issues (P2)
**Issue:** PowerShell/Python scripts fail on Unicode emojis on Windows.

**Fix:** Replace emojis with ASCII equivalents in scripts used on Windows

**Effort:** 1 hour

---

## Remediation Tasks

### Task 1: Fix CMakeLists.txt for External Dependencies (12-16 hours)

```cmake
# In CMakeLists.txt - ensure these are NOT optional

# GameNetworkingSockets
FetchContent_MakeAvailable(GameNetworkingSockets)
target_link_libraries(darkages_server PRIVATE GameNetworkingSockets::GameNetworkingSockets)

# hiredis  
FetchContent_MakeAvailable(hiredis)
target_link_libraries(darkages_server PRIVATE hiredis::hiredis)

# FlatBuffers
FetchContent_MakeAvailable(flatbuffers)
# ... code generation setup

# Remove conditional compilation that falls back to stubs
# USE_REAL_IMPLEMENTATIONS should be forced ON
```

### Task 2: Implement Full Server Main Loop (8-12 hours)

Create proper `src/server/src/main.cpp`:

```cpp
#include "zones/ZoneServer.hpp"
#include "netcode/NetworkManager.hpp"
#include "db/RedisManager.hpp"
#include "db/ScyllaManager.hpp"

int main(int argc, char* argv[]) {
    // Parse arguments
    uint16_t port = 7777;
    std::string redisHost = "localhost";
    std::string scyllaHost = "localhost";
    
    // Initialize systems
    ZoneServer server;
    
    if (!server.initialize(1, {redisHost, 6379}, {scyllaHost, 9042})) {
        std::cerr << "Failed to initialize server\n";
        return 1;
    }
    
    // Start network
    server.startListening(port);
    
    std::cout << "Server listening on port " << port << "\n";
    
    // Main game loop
    server.run();  // This blocks until shutdown
    
    return 0;
}
```

### Task 3: Replace Stub Implementations (8-12 hours)

Files to replace:
- `src/server/src/netcode/NetworkManager_stub.cpp` → Use full GNS implementation
- `src/server/src/db/RedisManager_stub.cpp` → Use hiredis implementation  
- `src/server/src/db/ScyllaManager_stub.cpp` → Use cassandra implementation
- `src/server/src/netcode/Protocol.cpp` → Ensure FlatBuffers integration works

### Task 4: Install Docker (2-4 hours)

```powershell
# Install WSL2 first (may require restart)
wsl --install

# Install Docker Desktop
winget install Docker.DockerDesktop

# After restart, verify
docker --version
docker run hello-world
```

### Task 5: Fix Script Encoding (1 hour)

Replace emojis in validation scripts:
```powershell
# Before
Write-Host "✅ PASS"

# After  
Write-Host "[PASS]"
```

---

## Revised Validation Process

After remediation, validation must be re-run:

```powershell
# Full clean validation
.\tools\validation\run_validation_tests.ps1

# Expected result: All 5 tests PASS
```

---

## Phase 8 Entry Criteria (Revised)

| Criterion | Requirement | Validation |
|-----------|-------------|------------|
| Server Functional | Accepts connections | Test 1 passes |
| Multi-Client | 10+ clients | Test 2 passes |
| Stress Test | 50 clients, 60s | Test 3 passes |
| Entity Sync | Clients see each other | Test 4 passes |
| Resilience | Handles packet loss | Test 5 passes |
| No Stubs | Real implementations | CMake with ENABLE_*=ON |
| Services | Redis + ScyllaDB | Docker running |

**ALL criteria must pass before Phase 8 begins.**

---

## Recommended Approach

### Option A: Full Remediation (24-32 hours)
- Fix CMakeLists.txt properly
- Integrate all external dependencies
- Implement full server
- Install Docker
- Re-run validation

### Option B: Development Mode (4-8 hours)
- Create "development build" mode with mock services
- Use Python-based mock server for client testing
- Skip ScyllaDB initially
- Proceed with Phase 8 using mock backend
- Full integration later

### Recommendation: Option B for faster progress

Rationale:
- Phase 8 (Monitoring, Security, Chaos) can be developed against mock server
- Client can be fully tested and polished
- Full backend integration becomes a separate milestone
- Faster time to playable demo

---

## Decision Required

**User Decision Needed:**

1. **Proceed with Option A (Full Remediation)**
   - 24-32 hours of work
   - Full server functional before Phase 8
   - All validation tests pass

2. **Proceed with Option B (Development Mode)**
   - 4-8 hours of work
   - Mock server for client development
   - Phase 8 proceeds in parallel
   - Full integration as separate milestone

3. **Other approach?**
   - Specify alternative strategy

---

*Awaiting user decision before proceeding.*
