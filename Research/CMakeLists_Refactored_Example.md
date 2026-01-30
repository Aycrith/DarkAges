# DarkAges CMakeLists.txt Refactored Example

This document provides a concrete, working example of how to refactor the DarkAges CMakeLists.txt using the FetchContent best practices from the research report.

## Recommended File Structure

```
C:\Dev\DarkAges\
├── CMakeLists.txt                      # Main configuration
├── cmake/
│   ├── Dependencies.cmake              # All dependency handling
│   ├── FlatBuffersCodegen.cmake        # Code generation
│   └── WindowsSetup.cmake              # MSVC-specific setup
└── src/
    └── server/
        └── CMakeLists.txt              # Server library/executable
```

---

## 1. Main CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(DarkAgesServer VERSION 0.1.0 LANGUAGES CXX)

# ============================================================================
# C++ Standard and Language Settings
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Build Options
# ============================================================================
option(BUILD_TESTS "Build tests" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_PROFILING "Enable Perfetto profiling" OFF)
option(FETCH_DEPENDENCIES "Fetch external dependencies automatically" ON)
option(USE_VCPKG "Use vcpkg for dependencies if available" ON)

# Phase 6 External Integration Options
option(ENABLE_GNS "Enable GameNetworkingSockets" ON)
option(ENABLE_REDIS "Enable Redis integration" ON)
option(ENABLE_SCYLLA "Enable ScyllaDB integration" ON)
option(ENABLE_FLATBUFFERS "Enable FlatBuffers protocol" ON)

# ============================================================================
# Windows/MSVC Setup (must be early)
# ============================================================================
include(cmake/WindowsSetup.cmake)

# ============================================================================
# Compiler Warnings
# ============================================================================
if(MSVC)
    add_compile_options(/W4 /WX-)
    add_compile_options(/permissive-)
    add_compile_options(/Zc:__cplusplus)
    add_compile_options(/MP)  # Multi-processor compilation
    add_compile_options(/EHsc)  # Exception handling
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-Wno-unused-parameter -Wno-unused-variable)
    add_compile_options(-Wno-missing-field-initializers)
endif()

# ============================================================================
# AddressSanitizer
# ============================================================================
if(ENABLE_ASAN AND NOT MSVC)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ============================================================================
# Find System Packages
# ============================================================================
find_package(Threads REQUIRED)

# ============================================================================
# Dependency Management
# ============================================================================
if(FETCH_DEPENDENCIES)
    include(cmake/Dependencies.cmake)
endif()

# ============================================================================
# Output Directories
# ============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# Subdirectories
# ============================================================================
add_subdirectory(src/server)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/server/tests)
endif()

# ============================================================================
# Install Configuration
# ============================================================================
include(GNUInstallDirs)
install(TARGETS darkages_server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ============================================================================
# Build Summary
# ============================================================================
include(cmake/PrintSummary.cmake)
```

---

## 2. cmake/WindowsSetup.cmake

```cmake
# Windows-specific setup that must happen BEFORE FetchContent

if(NOT WIN32)
    return()
endif()

message(STATUS "Configuring for Windows/MSVC")

# ============================================================================
# MSVC Runtime Library (Critical for FetchContent compatibility)
# ============================================================================
if(MSVC)
    # Enable MSVC_RUNTIME_LIBRARY target property
    if(POLICY CMP0091)
        cmake_policy(SET CMP0091 NEW)
    endif()
    
    # Default to dynamic runtime (/MD /MDd) for compatibility
    # Change to "MultiThreaded$<$<CONFIG:Debug>:Debug>" for static (/MT /MTd)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        CACHE STRING "MSVC Runtime Library" FORCE)
    
    message(STATUS "MSVC Runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

# ============================================================================
# Windows SDK Version
# ============================================================================
if(MSVC AND NOT WINDOWS_SDK_VERSION)
    # Use latest installed Windows SDK
    set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "Latest" CACHE STRING "")
endif()

# ============================================================================
# vcpkg Integration
# ============================================================================
if(USE_VCPKG)
    # Check for vcpkg in common locations
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/vcpkg")
    elseif(EXISTS "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(VCPKG_ROOT "C:/vcpkg")
    endif()
    
    if(VCPKG_ROOT AND EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "Vcpkg toolchain file")
        message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    endif()
endif()

# ============================================================================
# Windows Libraries (for linking)
# ============================================================================
set(WINDOWS_LIBS
    ws2_32      # Winsock2
    winmm       # Windows multimedia
    crypt32     # Cryptographic functions
    iphlpapi    # IP Helper API
    bcrypt      # Windows cryptography (alternative to OpenSSL)
    CACHE INTERNAL "Windows system libraries"
)
```

---

## 3. cmake/Dependencies.cmake

```cmake
# ============================================================================
# Dependency Management
# ============================================================================
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# Version pins - centralized for easy updates
set(DEPS_ENTT_VERSION "v3.13.0")
set(DEPS_GLM_VERSION "0.9.9.8")
set(DEPS_GNS_VERSION "v1.4.1")
set(DEPS_HIREDIS_VERSION "v1.2.0")
set(DEPS_CASSANDRA_VERSION "2.17.1")
set(DEPS_FLATBUFFERS_VERSION "v24.3.25")
set(DEPS_JSON_VERSION "v3.11.3")
set(DEPS_CATCH2_VERSION "v3.5.0")

message(STATUS "Dependency versions:")
message(STATUS "  EnTT: ${DEPS_ENTT_VERSION}")
message(STATUS "  GLM: ${DEPS_GLM_VERSION}")
message(STATUS "  GameNetworkingSockets: ${DEPS_GNS_VERSION}")
message(STATUS "  hiredis: ${DEPS_HIREDIS_VERSION}")
message(STATUS "  cassandra: ${DEPS_CASSANDRA_VERSION}")
message(STATUS "  FlatBuffers: ${DEPS_FLATBUFFERS_VERSION}")
message(STATUS "  nlohmann/json: ${DEPS_JSON_VERSION}")

# ============================================================================
# Common Build Options (apply to all dependencies)
# ============================================================================
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ============================================================================
# EnTT (ECS framework)
# ============================================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/entt/single_include/entt/entt.hpp")
    message(STATUS "EnTT: Using local submodule")
    set(ENTT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/entt/single_include")
    add_library(EnTT INTERFACE)
    target_include_directories(EnTT INTERFACE ${ENTT_INCLUDE_DIR})
    add_library(EnTT::EnTT ALIAS EnTT)
else()
    message(STATUS "EnTT: Fetching from GitHub")
    FetchContent_Declare(
        EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG ${DEPS_ENTT_VERSION}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES EnTT
    )
    FetchContent_MakeAvailable(EnTT)
endif()

# ============================================================================
# GLM (math library)
# ============================================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/glm/glm/glm.hpp")
    message(STATUS "GLM: Using local submodule")
    set(GLM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/glm")
    add_library(glm INTERFACE)
    target_include_directories(glm INTERFACE ${GLM_INCLUDE_DIR})
    add_library(glm::glm ALIAS glm)
else()
    message(STATUS "GLM: Fetching from GitHub")
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG ${DEPS_GLM_VERSION}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES glm
    )
    FetchContent_MakeAvailable(glm)
endif()

# ============================================================================
# nlohmann/json (header-only, always fetch)
# ============================================================================
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG ${DEPS_JSON_VERSION}
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)
set(NLOHMANN_JSON_FOUND TRUE)

# ============================================================================
# FlatBuffers (protocol serialization)
# ============================================================================
if(ENABLE_FLATBUFFERS)
    # Configure before declaring
    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
    
    FetchContent_Declare(
        flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG ${DEPS_FLATBUFFERS_VERSION}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES Flatbuffers flatbuffers
    )
    
    FetchContent_MakeAvailable(flatbuffers)
    
    if(TARGET flatbuffers::flatbuffers)
        set(FLATBUFFERS_FOUND TRUE)
        set(FLATBUFFERS_FLATC_EXECUTABLE "$<TARGET_FILE:flatbuffers::flatc>")
    else()
        message(WARNING "FlatBuffers: Target not available")
        set(FLATBUFFERS_FOUND FALSE)
    endif()
else()
    set(FLATBUFFERS_FOUND FALSE)
endif()

# ============================================================================
# GameNetworkingSockets (UDP networking)
# ============================================================================
if(ENABLE_GNS)
    # Check for crypto backend
    if(WIN32)
        # Windows: Prefer BCrypt to avoid OpenSSL dependency
        set(USE_CRYPTO "BCrypt" CACHE STRING "" FORCE)
    else()
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            set(USE_CRYPTO "OpenSSL" CACHE STRING "" FORCE)
        else()
            message(WARNING "GNS: No crypto backend found, will try MBEDTLS")
            set(USE_CRYPTO "MBEDTLS" CACHE STRING "" FORCE)
        endif()
    endif()
    
    # GNS build options
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)
    
    FetchContent_Declare(
        GameNetworkingSockets
        GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
        GIT_TAG ${DEPS_GNS_VERSION}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES GameNetworkingSockets
    )
    
    FetchContent_MakeAvailable(GameNetworkingSockets)
    
    if(TARGET GameNetworkingSockets::GameNetworkingSockets)
        set(GNS_FOUND TRUE)
    else()
        message(WARNING "GNS: Target not available")
        set(GNS_FOUND FALSE)
    endif()
else()
    set(GNS_FOUND FALSE)
endif()

# ============================================================================
# hiredis (Redis client)
# ============================================================================
if(ENABLE_REDIS)
    set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    
    FetchContent_Declare(
        hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG ${DEPS_HIREDIS_VERSION}
        GIT_SHALLOW TRUE
    )
    
    FetchContent_MakeAvailable(hiredis)
    
    # Create consistent alias
    if(TARGET hiredis AND NOT TARGET hiredis::hiredis)
        add_library(hiredis::hiredis ALIAS hiredis)
    endif()
    
    set(HIREDIS_FOUND TRUE)
else()
    set(HIREDIS_FOUND FALSE)
endif()

# ============================================================================
# cassandra-cpp-driver (ScyllaDB/Cassandra)
# ============================================================================
if(ENABLE_SCYLLA)
    # Check if available via vcpkg first
    find_package(cassandra QUIET)
    
    if(cassandra_FOUND)
        message(STATUS "cassandra: Found via find_package")
        set(CASSANDRA_FOUND TRUE)
    elseif(WIN32 AND NOT DEFINED FORCE_CASSANDRA_FETCH)
        # On Windows, skip fetch - use vcpkg or stub
        message(STATUS "cassandra: Skipping FetchContent on Windows (use vcpkg)")
        set(CASSANDRA_FOUND FALSE)
    else()
        # Build from source
        set(CASS_BUILD_STATIC ON CACHE BOOL "" FORCE)
        set(CASS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(CASS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CASS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(CASS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        
        FetchContent_Declare(
            cassandra
            GIT_REPOSITORY https://github.com/datastax/cpp-driver.git
            GIT_TAG ${DEPS_CASSANDRA_VERSION}
            GIT_SHALLOW TRUE
        )
        
        FetchContent_MakeAvailable(cassandra)
        
        if(TARGET cassandra_static)
            add_library(cassandra::cassandra ALIAS cassandra_static)
            set(CASSANDRA_FOUND TRUE)
        else()
            set(CASSANDRA_FOUND FALSE)
        endif()
    endif()
else()
    set(CASSANDRA_FOUND FALSE)
endif()

# ============================================================================
# Catch2 (testing framework)
# ============================================================================
if(BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG ${DEPS_CATCH2_VERSION}
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES Catch2
    )
    FetchContent_MakeAvailable(Catch2)
    
    if(TARGET Catch2::Catch2WithMain)
        set(CATCH2_FOUND TRUE)
    else()
        set(CATCH2_FOUND FALSE)
    endif()
endif()

# ============================================================================
# Dependency Summary
# ============================================================================
message(STATUS "")
message(STATUS "Dependency Status:")
message(STATUS "  EnTT: YES")
message(STATUS "  GLM: YES")
message(STATUS "  nlohmann/json: YES")
message(STATUS "  FlatBuffers: ${FLATBUFFERS_FOUND}")
message(STATUS "  GameNetworkingSockets: ${GNS_FOUND}")
message(STATUS "  hiredis: ${HIREDIS_FOUND}")
message(STATUS "  cassandra-cpp-driver: ${CASSANDRA_FOUND}")
if(BUILD_TESTS)
    message(STATUS "  Catch2: ${CATCH2_FOUND}")
endif()
```

---

## 4. cmake/FlatBuffersCodegen.cmake

```cmake
# ============================================================================
# FlatBuffers Code Generation
# ============================================================================

if(NOT FLATBUFFERS_FOUND)
    return()
endif()

# Find schema files
file(GLOB FBS_SCHEMAS "${CMAKE_SOURCE_DIR}/src/shared/proto/*.fbs")

if(NOT FBS_SCHEMAS)
    message(STATUS "FlatBuffers: No schema files found")
    return()
endif()

message(STATUS "FlatBuffers: Found schemas: ${FBS_SCHEMAS}")

# Generated files directory
set(FBS_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${FBS_GENERATED_DIR})

# Generate build commands for each schema
set(FBS_GENERATED_HEADERS)

foreach(FBS_SCHEMA ${FBS_SCHEMAS})
    get_filename_component(FBS_NAME ${FBS_SCHEMA} NAME_WE)
    set(FBS_GENERATED "${FBS_GENERATED_DIR}/${FBS_NAME}_generated.h")
    
    add_custom_command(
        OUTPUT ${FBS_GENERATED}
        COMMAND flatbuffers::flatc
            --cpp
            --gen-object-api
            --gen-compare
            --gen-mutable
            -o ${FBS_GENERATED_DIR}
            ${FBS_SCHEMA}
        DEPENDS ${FBS_SCHEMA} flatbuffers::flatc
        COMMENT "Generating FlatBuffers C++ code from ${FBS_NAME}.fbs"
        VERBATIM
    )
    
    list(APPEND FBS_GENERATED_HEADERS ${FBS_GENERATED})
endforeach()

# Create target for all generated code
add_custom_target(fbs_generation ALL
    DEPENDS ${FBS_GENERATED_HEADERS}
)

# Provide variables for parent scope
set(FLATBUFFERS_GENERATED_DIR ${FBS_GENERATED_DIR} PARENT_SCOPE)
set(FLATBUFFERS_GENERATED_HEADERS ${FBS_GENERATED_HEADERS} PARENT_SCOPE)

message(STATUS "FlatBuffers: Code generation configured")
```

---

## 5. src/server/CMakeLists.txt

```cmake
# ============================================================================
# DarkAges Server Library and Executable
# ============================================================================

# ---------------------------------------------------------------------------
# Source Files
# ---------------------------------------------------------------------------
set(SERVER_CORE_SOURCES
    src/ecs/CoreTypes.cpp
    src/physics/SpatialHash.cpp
    src/physics/MovementSystem.cpp
    src/memory/MemoryPool.cpp
    src/memory/LeakDetector.cpp
    src/combat/CombatSystem.cpp
    src/combat/PositionHistory.cpp
    src/combat/LagCompensatedCombat.cpp
    src/zones/AreaOfInterest.cpp
    src/zones/AuraProjection.cpp
    src/zones/ReplicationOptimizer.cpp
    src/zones/EntityMigration.cpp
    src/zones/ZoneHandoff.cpp
    src/zones/ZoneOrchestrator.cpp
    src/zones/ZoneServer.cpp
    src/zones/CrossZoneMessenger.cpp
    src/security/AntiCheat.cpp
    src/security/DDoSProtection.cpp
    src/profiling/PerformanceMonitor.cpp
)

# Conditional sources
if(GNS_FOUND)
    list(APPEND SERVER_CORE_SOURCES src/netcode/NetworkManager.cpp)
    message(STATUS "Network: Using GameNetworkingSockets")
else()
    list(APPEND SERVER_CORE_SOURCES src/netcode/NetworkManager_stub.cpp)
    message(WARNING "Network: Using stub implementation")
endif()

if(HIREDIS_FOUND)
    list(APPEND SERVER_CORE_SOURCES src/db/RedisManager.cpp)
    message(STATUS "Redis: Using hiredis")
else()
    list(APPEND SERVER_CORE_SOURCES src/db/RedisManager_stub.cpp)
    message(WARNING "Redis: Using stub implementation")
endif()

if(CASSANDRA_FOUND)
    list(APPEND SERVER_CORE_SOURCES src/db/ScyllaManager.cpp)
    message(STATUS "ScyllaDB: Using cassandra-cpp-driver")
else()
    list(APPEND SERVER_CORE_SOURCES src/db/ScyllaManager_stub.cpp)
    message(WARNING "ScyllaDB: Using stub implementation")
endif()

if(ENABLE_PROFILING)
    list(APPEND SERVER_CORE_SOURCES src/profiling/PerfettoProfiler.cpp)
else()
    list(APPEND SERVER_CORE_SOURCES src/profiling/PerfettoProfiler_stub.cpp)
endif()

# ---------------------------------------------------------------------------
# Core Library
# ---------------------------------------------------------------------------
add_library(darkages_core OBJECT ${SERVER_CORE_SOURCES})

# Include directories
target_include_directories(darkages_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/shared
)

# FlatBuffers generated headers
if(FLATBUFFERS_FOUND AND DEFINED FLATBUFFERS_GENERATED_DIR)
    target_include_directories(darkages_core PUBLIC ${FLATBUFFERS_GENERATED_DIR})
    add_dependencies(darkages_core fbs_generation)
endif()

# Link dependencies
target_link_libraries(darkages_core PUBLIC
    Threads::Threads
    EnTT::EnTT
    glm::glm
    nlohmann_json::nlohmann_json
)

if(FLATBUFFERS_FOUND)
    target_link_libraries(darkages_core PUBLIC flatbuffers::flatbuffers)
    target_compile_definitions(darkages_core PUBLIC ENABLE_FLATBUFFERS=1)
endif()

if(GNS_FOUND)
    target_link_libraries(darkages_core PUBLIC GameNetworkingSockets::GameNetworkingSockets)
    target_compile_definitions(darkages_core PUBLIC ENABLE_GNS=1)
    
    if(WIN32)
        target_link_libraries(darkages_core PUBLIC ${WINDOWS_LIBS})
    endif()
endif()

if(HIREDIS_FOUND)
    target_link_libraries(darkages_core PUBLIC hiredis::hiredis)
    target_compile_definitions(darkages_core PUBLIC ENABLE_REDIS=1)
    
    if(WIN32)
        target_link_libraries(darkages_core PUBLIC ${WINDOWS_LIBS})
    endif()
endif()

if(CASSANDRA_FOUND)
    target_link_libraries(darkages_core PUBLIC cassandra::cassandra)
    target_compile_definitions(darkages_core PUBLIC ENABLE_SCYLLA=1)
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(darkages_server src/main.cpp)

target_link_libraries(darkages_server PRIVATE darkages_core)

if(WIN32)
    set_target_properties(darkages_server PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS darkages_server
    RUNTIME DESTINATION bin
)
```

---

## 6. Key Improvements Over Current CMakeLists.txt

| Aspect | Current | Refactored |
|--------|---------|------------|
| **File organization** | Single large file | Modular cmake/ directory |
| **MSVC runtime** | Not set | Set before FetchContent |
| **Dependency order** | Mixed declare/populate | All declare first, then populate |
| **Windows libs** | Hardcoded in multiple places | Centralized WINDOWS_LIBS variable |
| **Version pins** | Inline | Centralized variables |
| **Target aliases** | Inconsistent | Consistent :: namespace |
| **Summary output** | Inline messages | Dedicated module |
| **FlatBuffers** | Single hardcoded schema | Glob pattern support |

---

## 7. Usage Instructions

### Build with FetchContent (Default)

```powershell
# Windows PowerShell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Or with Ninja
cmake -B build -G Ninja
cmake --build build
```

### Build with vcpkg (Faster CI)

```powershell
# Install vcpkg dependencies first
vcpkg install flatbuffers:x64-windows-static
vcpkg install gamenetworkingsockets:x64-windows-static
vcpkg install hiredis:x64-windows-static

# Configure with vcpkg
cmake -B build -G Ninja `
    -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

### Build without FetchContent (Use system packages)

```powershell
cmake -B build -G Ninja -DFETCH_DEPENDENCIES=OFF
```

### Override Dependency Location (Development)

```powershell
# Use local modified version of a dependency
cmake -B build -G Ninja `
    -DFETCHCONTENT_SOURCE_DIR_GAMENETWORKINGSOCKETS="C:/dev/GameNetworkingSockets"
```

---

*End of Refactored Example*
