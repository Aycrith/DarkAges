# Master Remediation Plan: Phase 8 Entry
## DarkAges MMO - Full System Integration

**Version:** 1.0  
**Date:** 2026-01-30  
**Status:** Planning Complete - Ready for Execution  
**Estimated Duration:** 24-32 hours  
**Parallel Workstreams:** 4  
**Agent Swarm Size:** 6-8 specialized agents

---

## Executive Summary

This document provides the comprehensive, research-validated plan to remediate the DarkAges MMO system to meet Phase 8 entry criteria. Based on validation testing, we identified three critical blockers:

1. **CMake/Build System** - External dependencies not properly linked
2. **Server Core** - Non-functional stub main loop  
3. **Infrastructure** - Docker not installed for database services

This plan uses **4 parallel workstreams** with **6-8 specialized agents** to complete remediation in 24-32 hours.

---

## Research Foundation

All planning is based on comprehensive research:

| Research Area | Document | Key Findings |
|--------------|----------|--------------|
| CMake FetchContent | `Research/CMake_FetchContent_Best_Practices_Research.md` (28KB) | MSVC runtime lib must be set BEFORE FetchContent_Declare |
| GNS Integration | `Research/GameNetworkingSockets_Integration_Guide.md` (33KB) | BCrypt preferred over OpenSSL on Windows |
| Database Clients | `Research/REDIS_SCYLLA_CPP_INTEGRATION_RESEARCH.md` (30KB) | hiredis NOT thread-safe, requires connection pool |
| Server Architecture | `Research/Server_Architecture_Research.md` (in analysis) | Fixed timestep, 60Hz, update order critical |

---

## Critical Path Analysis

### Dependency Graph

```
Stream 1: CMake/Build (Foundation)
├── Task 1.1: Fix CMakeLists.txt MSVC runtime [BLOCKER]
├── Task 1.2: Configure GNS FetchContent
├── Task 1.3: Configure hiredis FetchContent
├── Task 1.4: Configure FlatBuffers FetchContent
├── Task 1.5: Windows library linking
└── Task 1.6: Build verification

Stream 2: Server Core (Critical Path)
├── Task 2.1: Implement ZoneServer::run() [BLOCKER]
├── Task 2.2: Wire NetworkManager to GNS
├── Task 2.3: Wire RedisManager to hiredis
├── Task 2.4: Integrate all systems in main()
├── Task 2.5: Add signal handling
└── Task 2.6: Server functionality test

Stream 3: Infrastructure (Independent)
├── Task 3.1: Install Docker Desktop
├── Task 3.2: Configure WSL2
├── Task 3.3: Start Redis container
├── Task 3.4: Start ScyllaDB container
├── Task 3.5: Verify service health
└── Task 3.6: Create docker-compose.dev.yml

Stream 4: Integration & Validation (Final)
├── Task 4.1: Fix script encoding [DEP: All streams]
├── Task 4.2: Run full validation suite
├── Task 4.3: Fix any integration issues
├── Task 4.4: Performance benchmarking
└── Task 4.5: Generate validation report
```

### Critical Path Duration

| Stream | Tasks | Parallel Agents | Estimated Duration |
|--------|-------|-----------------|-------------------|
| Stream 1: CMake/Build | 6 | 2 | 8-10 hours |
| Stream 2: Server Core | 6 | 3 | 10-12 hours |
| Stream 3: Infrastructure | 6 | 1 | 4-6 hours |
| Stream 4: Validation | 5 | 2 | 4-6 hours |
| **Total** | **23** | **8** | **24-32 hours** |

---

## Agent Role Definitions

### AGENT-1: Build System Specialist
**Focus:** Stream 1 (CMake/Build System)  
**Skills:** CMake, C++ build systems, Windows/MSVC  
**Tasks:** 1.1, 1.2, 1.3, 1.4, 1.5, 1.6

### AGENT-2: Server Core Engineer  
**Focus:** Stream 2 (Server Implementation)  
**Skills:** C++20, GameNetworkingSockets, server architecture  
**Tasks:** 2.1, 2.2, 2.5, 2.6

### AGENT-3: Database Integration Specialist
**Focus:** Stream 2 (Database wiring)  
**Skills:** hiredis, cassandra-cpp-driver, C++  
**Tasks:** 2.3, 2.4

### AGENT-4: DevOps/Infrastructure Engineer
**Focus:** Stream 3 (Docker/Infrastructure)  
**Skills:** Docker, Windows, PowerShell  
**Tasks:** 3.1, 3.2, 3.3, 3.4, 3.5, 3.6

### AGENT-5: Integration Test Engineer
**Focus:** Stream 4 (Validation)  
**Skills:** Python, testing, networking  
**Tasks:** 4.1, 4.2, 4.3

### AGENT-6: Performance Engineer
**Focus:** Stream 4 (Benchmarking)  
**Skills:** Profiling, optimization, metrics  
**Tasks:** 4.4, 4.5

---

## Detailed Task Specifications

### STREAM 1: Build System (AGENT-1)

#### Task 1.1: Fix CMakeLists.txt MSVC Runtime (2 hours)
**Priority:** CRITICAL - BLOCKS ALL OTHER WORK  
**Research Reference:** `CMake_FetchContent_Best_Practices_Research.md`

```cmake
# MUST BE AT TOP OF CMakeLists.txt, before ANY project() or FetchContent
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# MUST be set before any FetchContent_Declare
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
```

**Validation:**
```powershell
cmake -B build -S . -G "Visual Studio 17 2022"
# Should configure without runtime library warnings
```

#### Task 1.2: Configure GameNetworkingSockets (1.5 hours)
**Research Reference:** GNS uses BCrypt on Windows (no OpenSSL needed)

```cmake
FetchContent_Declare(
    GameNetworkingSockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG v1.4.1
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(USE_CRYPTO "BCrypt" CACHE STRING "" FORCE)  # Windows built-in
```

#### Task 1.3: Configure hiredis (1 hour)
```cmake
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
)
```

#### Task 1.4: Configure FlatBuffers (1.5 hours)
```cmake
FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG v24.3.25
)

# Code generation
file(GLOB PROTO_FILES "${CMAKE_SOURCE_DIR}/src/shared/proto/*.fbs")
foreach(proto ${PROTO_FILES})
    # Generate C++ headers
endforeach()
```

#### Task 1.5: Windows Library Linking (1 hour)
```cmake
if(WIN32)
    target_link_libraries(darkages_server PRIVATE
        ws2_32      # Winsock
        winmm       # Windows multimedia
        crypt32     # Cryptography
        iphlpapi    # IP helper
        bcrypt      # Windows BCrypt (for GNS)
    )
endif()
```

#### Task 1.6: Build Verification (1 hour)
```powershell
# Clean build test
Remove-Item -Recurse -Force build
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release --parallel
# Verify darkages_server.exe exists and links all dependencies
```

---

### STREAM 2: Server Core (AGENT-2, AGENT-3)

#### Task 2.1: Implement ZoneServer::run() (AGENT-2, 4 hours)
**Research Reference:** `Server_Architecture_Research.md`

```cpp
// src/server/src/zones/ZoneServer.cpp

void ZoneServer::run() {
    setupSignalHandlers();
    running_ = true;
    
    // Main game loop at 60Hz
    auto nextTick = std::chrono::steady_clock::now();
    constexpr auto tickInterval = std::chrono::microseconds(16667); // 16.67ms
    
    while (running_) {
        auto frameStart = std::chrono::steady_clock::now();
        
        // Run one tick
        tick();
        
        // Frame limiting
        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed = frameEnd - frameStart;
        
        if (elapsed < tickInterval) {
            std::this_thread::sleep_for(tickInterval - elapsed);
        } else if (elapsed > tickInterval * 2) {
            // Log frame overrun
            LOG_WARNING("Tick overrun: {} us", 
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
        }
        
        nextTick += tickInterval;
    }
    
    shutdown();
}

void ZoneServer::setupSignalHandlers() {
    std::signal(SIGINT, [](int) { 
        ZoneServer::instance().requestShutdown(); 
    });
    std::signal(SIGTERM, [](int) { 
        ZoneServer::instance().requestShutdown(); 
    });
}

void ZoneServer::requestShutdown() {
    LOG_INFO("Shutdown requested...");
    running_ = false;
    shutdownRequested_ = true;
}
```

#### Task 2.2: Wire NetworkManager to GNS (AGENT-2, 3 hours)
**Existing Implementation:** `src/server/src/netcode/NetworkManager.cpp` (750 lines)

Verify the existing implementation is used instead of stub:

```cmake
# In CMakeLists.txt - ensure we use real implementation, not stub
if(ENABLE_GNS AND GameNetworkingSockets_FOUND)
    target_sources(darkages_server PRIVATE
        src/server/src/netcode/NetworkManager.cpp  # Real implementation
    )
else()
    target_sources(darkages_server PRIVATE
        src/server/src/netcode/NetworkManager_stub.cpp  # Fallback
    )
endif()
```

**Integration in ZoneServer:**
```cpp
bool ZoneServer::initialize(uint32_t zoneId, const ZoneConfig& config) {
    // ... existing init code ...
    
    // Initialize network
    if (!network_->initialize(config.port)) {
        LOG_ERROR("Failed to initialize network on port {}", config.port);
        return false;
    }
    
    LOG_INFO("ZoneServer initialized on port {}", config.port);
    return true;
}

void ZoneServer::updateNetwork() {
    // Poll for network events
    network_->update(getCurrentTimeMs());
    
    // Process pending inputs
    auto inputs = network_->getPendingInputs();
    for (const auto& input : inputs) {
        processClientInput(input);
    }
}
```

#### Task 2.3: Wire RedisManager to hiredis (AGENT-3, 2 hours)
**Existing Implementation:** `src/server/src/db/RedisManager.cpp` (47KB)

Verify CMake selects real implementation:
```cmake
if(ENABLE_REDIS AND hiredis_FOUND)
    target_sources(darkages_server PRIVATE
        src/server/src/db/RedisManager.cpp
    )
    target_compile_definitions(darkages_server PRIVATE ENABLE_REDIS=1)
endif()
```

#### Task 2.4: Integrate all systems in main() (AGENT-3, 2 hours)
**File:** `src/server/src/main.cpp` (replace stub)

```cpp
#include "zones/ZoneServer.hpp"
#include "Constants.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    try {
        // Parse command line
        uint16_t port = 7777;
        std::string redisHost = "localhost";
        uint16_t redisPort = 6379;
        std::string scyllaHost = "localhost";
        uint16_t scyllaPort = 9042;
        
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else if (arg == "--redis-host" && i + 1 < argc) {
                redisHost = argv[++i];
            } else if (arg == "--scylla-host" && i + 1 < argc) {
                scyllaHost = argv[++i];
            }
        }
        
        std::cout << "DarkAges Server v" << Constants::VERSION << "\n";
        std::cout << "Starting on port " << port << "...\n";
        
        // Create and initialize server
        ZoneServer server;
        
        ZoneConfig config;
        config.zoneId = 1;
        config.port = port;
        config.redisHost = redisHost;
        config.redisPort = redisPort;
        config.scyllaHost = scyllaHost;
        config.scyllaPort = scyllaPort;
        
        if (!server.initialize(config.zoneId, config)) {
            std::cerr << "Failed to initialize server\n";
            return 1;
        }
        
        std::cout << "Server initialized. Press Ctrl+C to stop.\n";
        
        // Run main loop (blocks until shutdown)
        server.run();
        
        std::cout << "Server shutdown complete.\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
```

#### Task 2.5: Add signal handling (AGENT-2, 1 hour)
(See Task 2.1 - already included)

#### Task 2.6: Server functionality test (AGENT-2, 1 hour)
```powershell
# Test server starts and listens
cd build
./darkages_server.exe --port 7777 &
Start-Sleep 5
netstat -an | findstr 7777  # Should show LISTENING
# Stop server
```

---

### STREAM 3: Infrastructure (AGENT-4)

#### Task 3.1: Install Docker Desktop (2 hours)
```powershell
# Install WSL2 first
wsl --install
# RESTART REQUIRED

# After restart, install Docker
winget install Docker.DockerDesktop

# Verify
wsl --version
docker --version
```

#### Task 3.2: Configure WSL2 (1 hour)
```powershell
# Set WSL2 as default
wsl --set-default-version 2

# Configure memory/CPU limits
# Create %USERPROFILE%\.wslconfig
```

#### Task 3.3: Start Redis Container (0.5 hours)
```powershell
cd infra
docker-compose up -d redis

# Verify
docker ps
docker logs darkages-redis
```

#### Task 3.4: Start ScyllaDB Container (0.5 hours)
```powershell
docker-compose up -d scylla

# Verify (takes ~60s to start)
docker logs darkages-scylla
```

#### Task 3.5: Verify Service Health (0.5 hours)
```powershell
# Test Redis
redis-cli ping  # Should return PONG

# Test ScyllaDB
cqlsh localhost 9042 -e "DESCRIBE KEYSPACES;"
```

#### Task 3.6: Create docker-compose.dev.yml (0.5 hours)
```yaml
version: '3.8'
services:
  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"
    volumes:
      - redis_data:/data
    command: redis-server --appendonly yes

  scylla:
    image: scylladb/scylla:5.4
    ports:
      - "9042:9042"
    volumes:
      - scylla_data:/var/lib/scylla
    command: --smp 1 --memory 1G --developer-mode 1

volumes:
  redis_data:
  scylla_data:
```

---

### STREAM 4: Integration & Validation (AGENT-5, AGENT-6)

#### Task 4.1: Fix script encoding (AGENT-5, 1 hour)
Replace Unicode emojis in PowerShell scripts:
```powershell
# Find and replace all emoji characters with ASCII equivalents
Get-ChildItem tools/validation/*.ps1 | ForEach-Object {
    (Get-Content $_) -replace '✅','[PASS]' -replace '❌','[FAIL]' -replace '⚠️','[WARN]' -replace 'ℹ️','[INFO]' -replace '⏳','[WAIT]' -replace '🟢','[UP]' -replace '🔴','[DOWN]' | Set-Content $_
}
```

#### Task 4.2: Run full validation suite (AGENT-5, 2 hours)
```powershell
.\tools\validation\run_validation_tests.ps1
```

**Expected Results:**
- Test 1: Single Client Connectivity - PASS
- Test 2: Ten Client Movement - PASS
- Test 3: Fifty Player Stress - PASS
- Test 4: Entity Synchronization - PASS
- Test 5: Packet Loss Recovery - PASS

#### Task 4.3: Fix integration issues (AGENT-5, 2 hours)
Timeboxed for fixing any issues found during validation.

#### Task 4.4: Performance benchmarking (AGENT-6, 1 hour)
```powershell
# Run with profiling
./darkages_server.exe --enable-profiling

# Collect metrics
python tools/stress-test/integration_harness.py --stress 50 --duration 300
```

**Target Metrics:**
- Tick time: <16ms (60Hz maintained)
- Memory: <500MB for 50 players
- CPU: <50% single core

#### Task 4.5: Generate validation report (AGENT-6, 1 hour)
Create `VALIDATION_REPORT_FINAL.md` with:
- All test results
- Performance metrics
- Known limitations
- Phase 8 readiness confirmation

---

## Coordination Protocol

### Communication Schedule

| Time | Activity | Participants |
|------|----------|--------------|
| Hour 0 | Kickoff, task assignment | All agents |
| Hour 4 | Stream 1 checkpoint | AGENT-1 + Coordinator |
| Hour 8 | Midpoint sync | All agents |
| Hour 12 | Stream 2 checkpoint | AGENT-2, AGENT-3 + Coordinator |
| Hour 16 | Integration prep | AGENT-4, AGENT-5 |
| Hour 20 | Pre-validation review | All agents |
| Hour 24-32 | Validation & fixes | AGENT-5, AGENT-6 |

### Issue Escalation

1. **Agent-blocked issues:** Post to coordination channel, 15min response
2. **Cross-stream dependencies:** AGENT-1 has priority (build blocks all)
3. **Validation failures:** AGENT-5 coordinates fixes, can pull any agent

### Success Criteria

| Checkpoint | Criteria | Gate |
|------------|----------|------|
| Hour 4 | CMake configures without errors | AGENT-1 |
| Hour 8 | Server binary links all dependencies | AGENT-1 |
| Hour 12 | Server starts and listens on port | AGENT-2 |
| Hour 16 | Docker services healthy | AGENT-4 |
| Hour 20 | All 5 validation tests attempted | AGENT-5 |
| Hour 32 | All 5 validation tests PASS | AGENT-5 |

---

## Risk Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| CMake dependency hell | Medium | High | Use BCrypt instead of OpenSSL; skip cassandra on Windows |
| GNS compile errors | Medium | High | Use specific v1.4.1 tag; disable examples/tests |
| Docker install fails | Low | Medium | Use local Redis/Scylla installs as fallback |
| Server crashes on start | Medium | High | Extensive logging; graceful fallback to stub mode |
| Validation timeout | Low | Medium | Parallel test execution; shorter smoke tests |

---

## Ready for Execution

This plan is:
- ✅ Research-validated
- ✅ Dependency-mapped
- ✅ Resource-allocated
- ✅ Risk-assessed
- ✅ Time-estimated

**Status: READY FOR AGENT SWARM EXECUTION**

Upon your approval, I will spawn 6-8 specialized agents to execute this plan in parallel.
