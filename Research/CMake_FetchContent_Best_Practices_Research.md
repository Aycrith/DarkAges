# CMake FetchContent Best Practices Research Report

**Project**: DarkAges MMO Server  
**Date**: 2026-01-30  
**Purpose**: Integration of external C++ dependencies using CMake FetchContent

---

## Executive Summary

This research report provides comprehensive guidance on integrating five critical external dependencies into the DarkAges MMO server project using CMake's FetchContent module. The target libraries are:

1. **GameNetworkingSockets** (v1.4.1) - Valve's UDP networking library
2. **hiredis** (v1.2.0) - Redis C++ client
3. **cassandra-cpp-driver** (v2.17.1) - DataStax C/C++ driver
4. **FlatBuffers** (v24.3.25) - Google's serialization library
5. **nlohmann/json** (v3.11.3) - JSON for Modern C++

The report covers best practices, Windows-specific considerations, library-specific integration notes, common pitfalls, and recommended CMake patterns.

---

## 1. CMake FetchContent Best Practices

### 1.1 Core Principles

```cmake
# Include FetchContent module
include(FetchContent)

# Best practice: Declare ALL dependencies first before calling MakeAvailable
# This ensures parent project controls version choices

FetchContent_Declare(
    dependency1
    GIT_REPOSITORY https://github.com/user/repo1.git
    GIT_TAG v1.0.0  # Pin to specific version
    GIT_SHALLOW TRUE  # Speed up clone
)

FetchContent_Declare(
    dependency2
    GIT_REPOSITORY https://github.com/user/repo2.git
    GIT_TAG v2.0.0
    GIT_SHALLOW TRUE
)

# Then make them all available at once
FetchContent_MakeAvailable(dependency1 dependency2)
```

### 1.2 Version Pinning Strategies

**Recommended Approach**: Use Git tags with commit hash verification

```cmake
# Method 1: Tag with comment (good)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 703bd9caab50b139428cea1aaff9974ebee5742e  # release-1.10.0
)

# Method 2: For production, prefer commit hashes over tags
# Tags can be force-moved, commit hashes cannot
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e9d83440  # v3.11.3
    GIT_SHALLOW TRUE  # Cannot use with commit hash in old CMake, works in 3.22+
)
```

### 1.3 Handling Transitive Dependencies

```cmake
# Problem: Dependency A also uses FetchContent for dependency B
# Solution: Declare B before calling MakeAvailable on A

# CORRECT: Declare all details first
FetchContent_Declare(
    projB  # Also used internally by projA
    GIT_REPOSITORY https://github.com/company/projB.git
    GIT_TAG v1.2.3  # Parent controls version
)

FetchContent_Declare(
    projA
    GIT_REPOSITORY https://github.com/company/projA.git
    GIT_TAG v2.0.0
)

# Make projB first so projA finds it already populated
FetchContent_MakeAvailable(projB projA)
```

### 1.4 Build Time Optimization

```cmake
# 1. Use GIT_SHALLOW TRUE for faster clones (since CMake 3.22)
FetchContent_Declare(
    libname
    GIT_REPOSITORY https://github.com/user/repo.git
    GIT_TAG v1.0.0
    GIT_SHALLOW TRUE  # Only fetch the specific commit
)

# 2. Disable unnecessary build targets
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

# 3. Use FIND_PACKAGE_ARGS to prefer system packages
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
    FIND_PACKAGE_ARGS  # Try find_package first
)

# 4. Set QUIET mode to reduce log spam
set(FETCHCONTENT_QUIET ON CACHE BOOL "" FORCE)

# 5. For CI: Use FETCHCONTENT_FULLY_DISCONNECTED after initial setup
# set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
```

---

## 2. Windows-Specific Considerations

### 2.1 MSVC Runtime Library Configuration

**Critical Issue**: Mixing /MT (static) and /MD (dynamic) runtime libraries causes link errors or runtime crashes.

```cmake
# Detect MSVC runtime type
if(MSVC)
    # Option 1: Policy CMP0091 (CMake 3.15+)
    cmake_policy(SET CMP0091 NEW)  # Enable MSVC_RUNTIME_LIBRARY target property
    
    # Set default for all targets
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    
    # Option 2: For FetchContent dependencies that don't support CMP0091
    # Use the replacement pattern
    if(CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        # Static runtime - set flags for dependencies
        set(OPENSSL_MSVC_STATIC_RT ON CACHE BOOL "" FORCE)
    endif()
endif()
```

### 2.2 Static vs Dynamic Linking on Windows

```cmake
# Recommended: Static linking for server deployment simplicity
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Windows-specific link libraries for socket operations
target_link_libraries(your_target PRIVATE
    ws2_32      # Winsock2
    winmm       # Windows multimedia (timers)
    crypt32     # Cryptographic functions
    iphlpapi    # IP Helper API
    bcrypt      # Windows crypto (for some libraries)
)
```

### 2.3 OpenSSL Dependency Handling

```cmake
# Check for OpenSSL before fetching GNS
find_package(OpenSSL QUIET)

if(NOT OpenSSL_FOUND AND MSVC)
    message(WARNING "OpenSSL not found. Options:")
    message(STATUS "  1. Install via winget: winget install ShiningLight.OpenSSL")
    message(STATUS "  2. Use vcpkg: vcpkg install openssl:x64-windows")
    message(STATUS "  3. Set OPENSSL_ROOT_DIR to installation path")
endif()

# Tell GNS about OpenSSL location
set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
set(OPENSSL_MSVC_STATIC_RT TRUE CACHE BOOL "" FORCE)
```

### 2.4 vcpkg vs FetchContent Tradeoffs

| Aspect | vcpkg | FetchContent |
|--------|-------|--------------|
| **Binary caching** | ✅ Yes | ❌ No (builds from source) |
| **Version pinning** | ✅ Manifest mode | ✅ GIT_TAG |
| **Transitive deps** | ✅ Automatic | ⚠️ Manual handling |
| **CI speed** | ✅ Fast with cache | ❌ Slower |
| **Offline builds** | ✅ After initial setup | ✅ After initial setup |
| **Customization** | ⚠️ Limited | ✅ Full control |
| **Disk space** | ✅ Shared across projects | ❌ Per-project |
| **Windows support** | ✅ Excellent | ✅ Good |

**Recommendation**: Use FetchContent for development flexibility, consider vcpkg for CI optimization.

---

## 3. Library-Specific Integration

### 3.1 GameNetworkingSockets (v1.4.1)

**Dependencies**: OpenSSL/MSVCrt/libsodium, protobuf (optional), webrtc (optional)

```cmake
# Pre-configure dependencies
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)

# Linking preference
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)

# Crypto backend selection
set(USE_CRYPTO "OpenSSL" CACHE STRING "" FORCE)  # or "MBEDTLS", "BCrypt" (Windows)

# Disable P2P/WebRTC unless needed (reduces build complexity)
set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    GameNetworkingSockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG v1.4.1
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(GameNetworkingSockets)

# Linking
target_link_libraries(your_target PRIVATE
    GameNetworkingSockets::GameNetworkingSockets
)

# Windows additional libs
if(WIN32)
    target_link_libraries(your_target PRIVATE
        ws2_32 winmm crypt32 iphlpapi
    )
endif()
```

**Important Notes**:
- GNS requires a crypto backend (OpenSSL recommended on Linux/Mac, BCrypt on Windows)
- protobuf is required for some features but can be disabled
- On Windows with MSVC, ensure OpenSSL is built with matching runtime (/MT or /MD)

### 3.2 hiredis (v1.2.0)

**Dependencies**: None for basic functionality, OpenSSL for SSL support

```cmake
# hiredis build options
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)  # Set ON if SSL needed
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(hiredis)

# hiredis creates 'hiredis' target, not 'hiredis::hiredis'
target_link_libraries(your_target PRIVATE hiredis)

# Include directories (hiredis doesn't export them well)
target_include_directories(your_target PRIVATE
    ${hiredis_SOURCE_DIR}
    ${hiredis_SOURCE_DIR}/include
)
```

**Important Notes**:
- hiredis CMake support was added in v1.0.0 - ensure using recent version
- SSL support requires OpenSSL and setting `ENABLE_SSL=ON`
- On Windows, may need to link additional libraries (ws2_32)
- hiredis target names vary by version - verify with `cmake --build . --target help`

### 3.3 cassandra-cpp-driver (v2.17.1)

**Dependencies**: libuv (required), OpenSSL (recommended), zlib (optional)

```cmake
# cassandra driver build options
set(CASS_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CASS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CASS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CASS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CASS_BUILD_DOCS OFF CACHE BOOL "" FORCE)

# Use bundled dependencies if system not available
set(CASS_USE_OPENSSL ON CACHE BOOL "" FORCE)
set(CASS_USE_ZLIB ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    cassandra
    GIT_REPOSITORY https://github.com/datastax/cpp-driver.git
    GIT_TAG 2.17.1
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(cassandra)

# Linking - target name varies by version
target_link_libraries(your_target PRIVATE cassandra_static)

# Include directories
target_include_directories(your_target PRIVATE
    ${cassandra_SOURCE_DIR}/include
)
```

**Important Notes**:
- **High build complexity**: This library has many dependencies
- libuv is a hard dependency - must be available or bundled
- Consider using system package if available: `apt install libcassandra-dev`
- May be easier to use vcpkg for this dependency: `vcpkg install cassandra-cpp-driver`
- Static linking can be problematic on Windows due to libuv dependencies

### 3.4 FlatBuffers (v24.3.25)

**Dependencies**: None for runtime, compiler needs C++ compiler

```cmake
# FlatBuffers build options
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)   # Runtime library
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)    # Compiler
set(FLATBUFFERS_BUILD_FLATBUFFERS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG v24.3.25
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(flatbuffers)

# Link runtime library
target_link_libraries(your_target PRIVATE flatbuffers::flatbuffers)

# Code generation setup
set(FBS_SCHEMA ${CMAKE_SOURCE_DIR}/src/proto/game.fbs)
set(FBS_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${FBS_GENERATED_DIR})

add_custom_command(
    OUTPUT ${FBS_GENERATED_DIR}/game_generated.h
    COMMAND flatbuffers::flatc
        --cpp
        --gen-object-api
        --gen-compare
        -o ${FBS_GENERATED_DIR}
        ${FBS_SCHEMA}
    DEPENDS ${FBS_SCHEMA} flatbuffers::flatc
    COMMENT "Generating FlatBuffers code"
)

add_custom_target(fbs_generation ALL
    DEPENDS ${FBS_GENERATED_DIR}/game_generated.h
)
```

**Important Notes**:
- The `flatc` compiler is a build-time tool, not a runtime dependency
- Generated headers need `flatbuffers::flatbuffers` runtime library
- Use `--gen-object-api` for more C++-friendly interface
- Consider caching generated files in CI to speed up builds

### 3.5 nlohmann/json (v3.11.3)

**Dependencies**: None (header-only)

```cmake
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(json)

# Header-only, no linking needed
target_link_libraries(your_target PRIVATE nlohmann_json::nlohmann_json)
# OR just include directory:
target_include_directories(your_target PRIVATE
    ${json_SOURCE_DIR}/single_include
)
```

**Important Notes**:
- Pure header-only library, simplest integration
- Can also use `nlohmann_json::nlohmann_json` interface target
- Single-include version available at `single_include/nlohmann/json.hpp`

---

## 4. Common Pitfalls and Solutions

### 4.1 Header Path Issues

```cmake
# Problem: Different install layouts between FetchContent and system packages

# Solution: Use generator expressions to handle both cases
target_include_directories(your_target PRIVATE
    $<IF:$<TARGET_EXISTS:flatbuffers::flatbuffers>,
        ${flatbuffers_SOURCE_DIR}/include,
        ${Flatbuffers_INCLUDE_DIRS}
    >
)

# Better solution: Create interface wrapper
if(TARGET flatbuffers::flatbuffers)
    add_library(flatbuffers_wrapper INTERFACE)
    target_link_libraries(flatbuffers_wrapper INTERFACE flatbuffers::flatbuffers)
    target_include_directories(flatbuffers_wrapper INTERFACE
        ${flatbuffers_SOURCE_DIR}/include
    )
endif()
```

### 4.2 Link Order Problems

```cmake
# Problem: Undefined references due to incorrect link order

# Solution: Use target_link_libraries with proper dependency ordering
# Dependencies should come after dependents

target_link_libraries(your_server PRIVATE
    # Your libraries first
    darkages_core
    
    # Then external libraries in dependency order
    GameNetworkingSockets::GameNetworkingSockets
    cassandra_static      # Depends on libuv, openssl
    hiredis               # Depends on openssl (if SSL enabled)
    flatbuffers::flatbuffers
    nlohmann_json::nlohmann_json
    
    # System libraries last
    OpenSSL::SSL
    OpenSSL::Crypto
    ${CMAKE_THREAD_LIBS_INIT}
)
```

### 4.3 Runtime Library Mismatches (/MT vs /MD)

```cmake
# Problem: LNK2038: mismatch detected for 'RuntimeLibrary'

# Solution 1: Use CMAKE_MSVC_RUNTIME_LIBRARY (CMake 3.15+)
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Solution 2: Force all dependencies to use same runtime
if(MSVC)
    # Before FetchContent_MakeAvailable
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    
    # Tell specific dependencies
    set(OPENSSL_MSVC_STATIC_RT OFF CACHE BOOL "" FORCE)
    set(MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Solution 3: Override for specific targets after population
if(MSVC AND TARGET cassandra_static)
    set_property(TARGET cassandra_static PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
```

### 4.4 Debug/Release Configuration

```cmake
# Problem: Debug libraries not found or mixing configurations

# Solution: Use generator expressions for configuration-specific linking
target_link_libraries(your_target PRIVATE
    $<$<CONFIG:Debug>:hiredisd>
    $<$<CONFIG:Release>:hiredis>
    $<$<CONFIG:RelWithDebInfo>:hiredis>
)

# Better: Let CMake handle configuration via imported targets
# Modern CMake targets (flatbuffers::flatbuffers, etc.) handle this automatically
```

### 4.5 Target Name Inconsistencies

```cmake
# Problem: Different target names between versions or build methods

# Solution: Create alias targets
if(TARGET hiredis AND NOT TARGET hiredis::hiredis)
    add_library(hiredis::hiredis ALIAS hiredis)
endif()

if(TARGET cassandra_static AND NOT TARGET cassandra::cassandra)
    add_library(cassandra::cassandra ALIAS cassandra_static)
endif()
```

---

## 5. Recommended CMake Structure for DarkAges

### 5.1 Main CMakeLists.txt Structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(DarkAgesServer VERSION 0.1.0 LANGUAGES CXX)

# ============================================================================
# Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build tests" ON)
option(FETCH_DEPENDENCIES "Fetch external dependencies automatically" ON)

# MSVC configuration
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /MP)
endif()

# ============================================================================
# Dependency Management
# ============================================================================
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# Pre-declare all dependencies to control versions
set(DEPS_GNS_VERSION "v1.4.1")
set(DEPS_HIREDIS_VERSION "v1.2.0")
set(DEPS_CASSANDRA_VERSION "2.17.1")
set(DEPS_FLATBUFFERS_VERSION "v24.3.25")
set(DEPS_JSON_VERSION "v3.11.3")
set(DEPS_CATCH2_VERSION "v3.5.0")

# ---------------------------------------------------------------------------
# Local Dependencies (from deps/ subdirectory)
# ---------------------------------------------------------------------------
include(cmake/LocalDependencies.cmake)

# ---------------------------------------------------------------------------
# External Dependencies (via FetchContent)
# ---------------------------------------------------------------------------
if(FETCH_DEPENDENCIES)
    include(cmake/ExternalDependencies.cmake)
endif()

# ============================================================================
# Protocol Generation
# ============================================================================
include(cmake/FlatBuffersCodegen.cmake)

# ============================================================================
# Main Library and Executable
# ============================================================================
add_subdirectory(src/server)

# ============================================================================
# Testing
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### 5.2 ExternalDependencies.cmake

```cmake
# cmake/ExternalDependencies.cmake

# Set common build options for all dependencies
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# nlohmann/json (header-only, fetch always)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG ${DEPS_JSON_VERSION}
    GIT_SHALLOW TRUE
)

# ---------------------------------------------------------------------------
# FlatBuffers (try find_package first)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG ${DEPS_FLATBUFFERS_VERSION}
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES Flatbuffers flatbuffers
)

# Configure FlatBuffers build
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# GameNetworkingSockets
# ---------------------------------------------------------------------------
FetchContent_Declare(
    GameNetworkingSockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG ${DEPS_GNS_VERSION}
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES GameNetworkingSockets
)

# Configure GNS build
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)

# Check for crypto backend
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    set(USE_CRYPTO "OpenSSL" CACHE STRING "" FORCE)
elseif(WIN32)
    set(USE_CRYPTO "BCrypt" CACHE STRING "" FORCE)
else()
    message(FATAL_ERROR "No crypto backend found for GameNetworkingSockets")
endif()

# ---------------------------------------------------------------------------
# hiredis
# ---------------------------------------------------------------------------
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG ${DEPS_HIREDIS_VERSION}
    GIT_SHALLOW TRUE
)

set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# cassandra-cpp-driver (complex - may want to skip on Windows)
# ---------------------------------------------------------------------------
if(NOT WIN32 OR DEFINED FORCE_CASSANDRA)
    FetchContent_Declare(
        cassandra
        GIT_REPOSITORY https://github.com/datastax/cpp-driver.git
        GIT_TAG ${DEPS_CASSANDRA_VERSION}
        GIT_SHALLOW TRUE
    )
    
    set(CASS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(CASS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(CASS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CASS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CASS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    
    list(APPEND EXTERNAL_DEPS cassandra)
    set(ENABLE_CASSANDRA TRUE)
else()
    message(STATUS "Skipping cassandra-cpp-driver on Windows (use vcpkg or disable)")
    set(ENABLE_CASSANDRA FALSE)
endif()

# ---------------------------------------------------------------------------
# Catch2 (testing only)
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG ${DEPS_CATCH2_VERSION}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES Catch2
    )
    list(APPEND EXTERNAL_DEPS Catch2)
endif()

# ---------------------------------------------------------------------------
# Make all dependencies available
# ---------------------------------------------------------------------------
list(APPEND EXTERNAL_DEPS json flatbuffers GameNetworkingSockets hiredis)
FetchContent_MakeAvailable(${EXTERNAL_DEPS})

# ---------------------------------------------------------------------------
# Create consistent target aliases
# ---------------------------------------------------------------------------
if(TARGET hiredis AND NOT TARGET hiredis::hiredis)
    add_library(hiredis::hiredis ALIAS hiredis)
endif()

if(TARGET cassandra_static AND NOT TARGET cassandra::cassandra)
    add_library(cassandra::cassandra ALIAS cassandra_static)
endif()
```

### 5.3 Dependency Testing CMake Module

```cmake
# cmake/TestDependencies.cmake

function(test_dependency DEP_NAME TARGET_NAME)
    if(TARGET ${TARGET_NAME})
        message(STATUS "✓ ${DEP_NAME}: Target ${TARGET_NAME} available")
        
        # Try to get target properties
        get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
        get_target_property(INCLUDE_DIRS ${TARGET_NAME} INTERFACE_INCLUDE_DIRECTORIES)
        
        message(STATUS "  Type: ${TARGET_TYPE}")
        if(INCLUDE_DIRS)
            message(STATUS "  Include dirs: ${INCLUDE_DIRS}")
        endif()
    else()
        message(WARNING "✗ ${DEP_NAME}: Target ${TARGET_NAME} NOT found")
    endif()
endfunction()

message(STATUS "")
message(STATUS "=== Dependency Availability Check ===")

test_dependency("nlohmann/json" "nlohmann_json::nlohmann_json")
test_dependency("FlatBuffers" "flatbuffers::flatbuffers")
test_dependency("FlatBuffers compiler" "flatbuffers::flatc")
test_dependency("GameNetworkingSockets" "GameNetworkingSockets::GameNetworkingSockets")
test_dependency("hiredis" "hiredis::hiredis")
if(ENABLE_CASSANDRA)
    test_dependency("cassandra-cpp-driver" "cassandra::cassandra")
endif()
if(BUILD_TESTS)
    test_dependency("Catch2" "Catch2::Catch2WithMain")
endif()

message(STATUS "=====================================")
message(STATUS "")
```

---

## 6. Troubleshooting Guide

### 6.1 Common Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| `Could not find Git` | Git not in PATH | Install Git and add to PATH |
| `Failed to clone` | Network issue or invalid URL | Check URL, proxy settings |
| `Target already defined` | Declaring same dependency twice | Use unique names per dependency |
| `find_package not found` | Package not installed | Set `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER` |
| `LNK2038: RuntimeLibrary mismatch` | /MT vs /MD conflict | Set `CMAKE_MSVC_RUNTIME_LIBRARY` |
| `undefined reference to...` | Missing link library or wrong order | Add missing lib, reorder link command |
| `cannot find -l<lib>` | Library not built or wrong path | Check library was built, verify paths |

### 6.2 Debug Commands

```bash
# Verbose CMake configuration
cmake -B build --log-level=VERBOSE

# See all targets
cmake --build build --target help

# Check what FetchContent populated
cat build/_deps/*-subbuild/*.cmake 2>/dev/null || echo "Check _deps folder"
ls -la build/_deps/

# Verify FindPackage integration
cmake -B build --debug-find-pkg=Flatbuffers

# Generate compile_commands.json for analysis
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### 6.3 Windows-Specific Debugging

```powershell
# Check Visual Studio environment
cl.exe  # Should show compiler version

# Check for OpenSSL
where openssl
openssl version

# List vcpkg packages (if using vcpkg)
vcpkg list

# Verify runtime library settings
dumpbin /DIRECTIVES your_lib.lib | findstr "DEFAULTLIB"
```

---

## 7. References

### Official Documentation

1. **CMake FetchContent**: https://cmake.org/cmake/help/latest/module/FetchContent.html
2. **Using Dependencies Guide**: https://cmake.org/cmake/help/latest/guide/using-dependencies/index.html
3. **GameNetworkingSockets Build Guide**: https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/BUILDING.md
4. **DataStax C++ Driver**: https://docs.datastax.com/en/developer/cpp-driver/latest/
5. **hiredis**: https://github.com/redis/hiredis/blob/master/README.md
6. **FlatBuffers**: https://google.github.io/flatbuffers/

### Best Practice Articles

1. CMake Discourse - FetchContent patterns: https://discourse.cmake.org/t/how-to-use-fetchcontent-correctly/3686
2. CMake FetchContent vs ExternalProject: https://www.studyplan.dev/cmake/fetching-external-dependencies
3. FetchContent vs vcpkg comparison: https://discourse.cmake.org/t/fetchcontent-vs-vcpkg-conan/6578

---

## 8. Summary and Recommendations

### For DarkAges Project

1. **Immediate Actions**:
   - Refactor CMakeLists.txt to separate dependency management into modules
   - Implement the `FIND_PACKAGE_ARGS` pattern for faster rebuilds
   - Add dependency testing module for CI verification

2. **Windows Considerations**:
   - Set `CMAKE_MSVC_RUNTIME_LIBRARY` before any FetchContent calls
   - Consider using vcpkg for cassandra-cpp-driver on Windows
   - Ensure OpenSSL is installed and properly configured for GNS

3. **Build Optimization**:
   - Use `GIT_SHALLOW TRUE` for all dependencies (CMake 3.22+)
   - Disable all unnecessary build targets (tests, examples)
   - Set `FETCHCONTENT_QUIET ON` for cleaner CI logs

4. **Version Management**:
   - Pin to specific versions using GIT_TAG
   - Document version requirements in a central location
   - Test upgrades in isolation before committing

5. **Troubleshooting Preparedness**:
   - Keep the dependency testing module available
   - Document workarounds for known library issues
   - Use `FETCHCONTENT_SOURCE_DIR_<NAME>` for local development overrides

---

*End of Research Report*
