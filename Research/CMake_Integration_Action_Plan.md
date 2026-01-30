# CMake FetchContent Integration Action Plan

**For**: DarkAges MMO Server Project  
**Goal**: Implement robust, maintainable external dependency management  
**Priority**: High (blocks clean build)

---

## Executive Summary

Based on comprehensive research of CMake FetchContent best practices and analysis of the DarkAges project's specific requirements, this action plan provides step-by-step guidance for fixing the CMakeLists.txt configuration.

### Critical Issues to Address

1. **MSVC Runtime Library** - Must be set before any FetchContent calls on Windows
2. **Dependency Order** - All dependencies must be declared before any `FetchContent_MakeAvailable` calls
3. **Target Name Consistency** - Need `::` namespaced aliases for all dependencies
4. **cassandra-cpp-driver** - Complex build; recommend vcpkg on Windows or conditional skip
5. **FlatBuffers Code Generation** - Needs proper CMake integration

---

## Phase 1: Immediate Fixes (Priority 1)

### 1.1 Fix MSVC Runtime Configuration

**Location**: Top of CMakeLists.txt, before `project()` command

```cmake
# Windows/MSVC runtime MUST be set before FetchContent
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
```

**Why**: GameNetworkingSockets and other dependencies detect and use this setting at configure time. Setting it after FetchContent calls results in link errors.

**Testing**: 
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
# Should configure without runtime warnings
```

### 1.2 Reorder Dependency Declarations

**Current Problem**: Dependencies are declared and populated in the same `if` blocks.

**Fix**: Declare ALL dependencies first, then call `FetchContent_MakeAvailable` once at the end.

```cmake
# At the top of dependency section
include(FetchContent)

# Declare all dependencies
FetchContent_Declare(nlohmann_json ...)
FetchContent_Declare(flatbuffers ...)
FetchContent_Declare(GameNetworkingSockets ...)
FetchContent_Declare(hiredis ...)

# Only ONE MakeAvailable call at the end
FetchContent_MakeAvailable(nlohmann_json flatbuffers GameNetworkingSockets hiredis)
```

### 1.3 Fix cassandra-cpp-driver on Windows

**Current Problem**: Complex build with many dependencies that often fail on Windows.

**Immediate Fix**: Skip FetchContent on Windows, use stub or vcpkg.

```cmake
if(ENABLE_SCYLLA)
    if(WIN32 AND NOT DEFINED FORCE_CASSANDRA)
        message(STATUS "cassandra: Skipping FetchContent on Windows (use vcpkg)")
        set(CASSANDRA_FOUND FALSE)
    else()
        # Existing FetchContent code
    endif()
endif()
```

---

## Phase 2: Code Organization (Priority 2)

### 2.1 Create Modular CMake Structure

Create `cmake/` directory with separate modules:

```
cmake/
├── WindowsSetup.cmake       # MSVC runtime, Windows libs
├── Dependencies.cmake       # All FetchContent logic
├── FlatBuffersCodegen.cmake # Code generation
└── PrintSummary.cmake       # Build summary
```

### 2.2 Extract Windows Setup

**File**: `cmake/WindowsSetup.cmake`

```cmake
if(NOT WIN32)
    return()
endif()

# MSVC runtime
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Windows system libraries
set(WINDOWS_LIBS ws2_32 winmm crypt32 iphlpapi bcrypt)
```

**In main CMakeLists.txt**:
```cmake
include(cmake/WindowsSetup.cmake)  # Must be before any FetchContent
```

### 2.3 Centralize Version Pins

At the top of `cmake/Dependencies.cmake`:

```cmake
set(DEPS_GNS_VERSION "v1.4.1")
set(DEPS_HIREDIS_VERSION "v1.2.0")
set(DEPS_FLATBUFFERS_VERSION "v24.3.25")
set(DEPS_JSON_VERSION "v3.11.3")
# ... etc
```

Benefits:
- Single location for version updates
- Easy to see what versions are in use
- Can be overridden by parent projects

---

## Phase 3: Dependency Improvements (Priority 3)

### 3.1 GameNetworkingSockets

**Current Issue**: Crypto backend configuration

**Improvement**: Use BCrypt on Windows (built-in, no OpenSSL dependency)

```cmake
if(WIN32)
    set(USE_CRYPTO "BCrypt" CACHE STRING "" FORCE)
else()
    find_package(OpenSSL REQUIRED)
    set(USE_CRYPTO "OpenSSL" CACHE STRING "" FORCE)
endif()
```

### 3.2 hiredis Target Names

**Current Issue**: Creates `hiredis` target, not `hiredis::hiredis`

**Fix**: Create alias after MakeAvailable

```cmake
FetchContent_MakeAvailable(hiredis)
if(TARGET hiredis AND NOT TARGET hiredis::hiredis)
    add_library(hiredis::hiredis ALIAS hiredis)
endif()
```

### 3.3 nlohmann/json

**Current**: Already working, but can use imported target

```cmake
# Instead of include directory, use target
target_link_libraries(darkages_core PUBLIC nlohmann_json::nlohmann_json)
```

### 3.4 FlatBuffers Code Generation

**Current**: Hardcoded single schema file

**Improvement**: Support multiple schemas with glob

```cmake
file(GLOB FBS_SCHEMAS "${CMAKE_SOURCE_DIR}/src/shared/proto/*.fbs")

foreach(FBS_SCHEMA ${FBS_SCHEMAS})
    get_filename_component(FBS_NAME ${FBS_SCHEMA} NAME_WE)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/generated/${FBS_NAME}_generated.h
        COMMAND flatbuffers::flatc --cpp -o ${CMAKE_BINARY_DIR}/generated ${FBS_SCHEMA}
        DEPENDS ${FBS_SCHEMA} flatbuffers::flatc
    )
endforeach()
```

---

## Phase 4: Testing and Validation (Priority 4)

### 4.1 Add Dependency Verification

Create `cmake/VerifyDependencies.cmake`:

```cmake
function(verify_dependency NAME TARGET)
    if(TARGET ${TARGET})
        message(STATUS "✓ ${NAME}: ${TARGET}")
    else()
        message(WARNING "✗ ${NAME}: ${TARGET} NOT FOUND")
    endif()
endfunction()

verify_dependency("EnTT" "EnTT::EnTT")
verify_dependency("GLM" "glm::glm")
verify_dependency("FlatBuffers" "flatbuffers::flatbuffers")
verify_dependency("GNS" "GameNetworkingSockets::GameNetworkingSockets")
verify_dependency("hiredis" "hiredis::hiredis")
```

### 4.2 Test Build Matrix

Test configurations:
- [ ] Windows + MSVC + Ninja
- [ ] Windows + MSVC + Visual Studio
- [ ] Windows + vcpkg integration
- [ ] Linux + GCC + Make
- [ ] Linux + Clang + Ninja
- [ ] macOS + Clang + Make

### 4.3 CI Integration

Cache FetchContent downloads:

```yaml
# GitHub Actions example
- name: Cache CMake dependencies
  uses: actions/cache@v3
  with:
    path: build/_deps
    key: deps-${{ runner.os }}-${{ hashFiles('**/CMakeLists.txt') }}
```

---

## Phase 5: Documentation (Priority 5)

### 5.1 Update BUILD.md

Add section on dependency management:

```markdown
## Dependencies

### Automatic FetchContent (Default)
Dependencies are fetched automatically at configure time:

```bash
cmake -B build -G Ninja
```

### Using System Packages
If you have dependencies installed:

```bash
cmake -B build -G Ninja -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS
```

### Using vcpkg (Recommended for Windows)
```bash
vcpkg install flatbuffers hiredis gamenetworkingsockets
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```
```

### 5.2 Document Version Requirements

Create `DEPENDENCIES.md`:

```markdown
| Dependency | Version | Purpose | Notes |
|------------|---------|---------|-------|
| GameNetworkingSockets | v1.4.1 | Networking | Use BCrypt on Windows |
| hiredis | v1.2.0 | Redis client | No SSL by default |
| FlatBuffers | v24.3.25 | Serialization | flatc compiler built |
| ... | ... | ... | ... |
```

---

## Implementation Timeline

| Phase | Tasks | Estimated Time | Priority |
|-------|-------|----------------|----------|
| Phase 1 | MSVC runtime fix, reorder declarations, skip cassandra on Windows | 2 hours | P1 - Critical |
| Phase 2 | Create cmake/ directory, modularize | 4 hours | P2 - High |
| Phase 3 | GNS BCrypt, target aliases, FlatBuffers glob | 3 hours | P3 - Medium |
| Phase 4 | Verification module, CI updates | 2 hours | P4 - Medium |
| Phase 5 | Documentation updates | 1 hour | P5 - Low |

**Total**: ~12 hours of focused work

---

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| GNS fails to build on Windows | High | Use BCrypt instead of OpenSSL; test early |
| cassandra too complex | Medium | Skip on Windows; use stub implementation |
| MSVC runtime mismatch | High | Set CMAKE_MSVC_RUNTIME_LIBRARY first |
| Long build times | Medium | Use GIT_SHALLOW; cache in CI |
| Version conflicts | Low | Pin exact versions; test upgrades separately |

---

## Success Criteria

- [ ] Clean CMake configuration on Windows with MSVC
- [ ] Clean CMake configuration on Linux with GCC/Clang
- [ ] All five dependencies found or gracefully degrade to stubs
- [ ] No runtime library mismatch warnings
- [ ] Build completes without link errors
- [ ] CI build time < 15 minutes with caching

---

## Quick Start for Implementation

1. **Backup current CMakeLists.txt**
   ```bash
   cp CMakeLists.txt CMakeLists.txt.backup
   ```

2. **Apply Phase 1 fixes to current file**
   - Add MSVC runtime setting at top
   - Reorder declarations
   - Skip cassandra on Windows

3. **Test on Windows**
   ```powershell
   cmake -B build -G Ninja
   cmake --build build
   ```

4. **If successful, proceed to Phase 2 (modularization)**

5. **Test all platforms**

---

## References

- Main Research Report: `CMake_FetchContent_Best_Practices_Research.md`
- Refactored Example: `CMakeLists_Refactored_Example.md`
- Quick Cheat Sheet: `CMake_FetchContent_CheatSheet.md`

---

*Action Plan for DarkAges MMO Project - CMake Integration*
