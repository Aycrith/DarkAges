cmake_minimum_required(VERSION 3.20)
project(DarkAgesServer VERSION 0.1.0 LANGUAGES CXX)

# C++20 is required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build tests" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_PROFILING "Enable Perfetto profiling" OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
    add_compile_options(/permissive-)
    add_compile_options(/Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-Wno-unused-parameter -Wno-unused-variable)
endif()

# AddressSanitizer for debug builds
if(ENABLE_ASAN AND NOT MSVC)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Check for EnTT
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/entt/single_include/entt/entt.hpp")
    message(STATUS "EnTT found in deps/entt")
    set(ENTT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/entt/single_include")
else()
    message(WARNING "EnTT not found in deps/entt - using FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.13.0
    )
    FetchContent_MakeAvailable(EnTT)
    set(ENTT_INCLUDE_DIR "${entt_SOURCE_DIR}/single_include")
endif()

# Check for GLM
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/glm/glm/glm.hpp")
    message(STATUS "GLM found in deps/glm")
    set(GLM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/glm")
else()
    message(STATUS "GLM not found locally - using FetchContent")
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 0.9.9.8
    )
    FetchContent_MakeAvailable(glm)
    set(GLM_INCLUDE_DIR "${glm_SOURCE_DIR}")
endif()

# FlatBuffers - we'll use a simple approach first
# In production, you'd use find_package or FetchContent
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/flatbuffers/include")
    message(STATUS "FlatBuffers found in deps/flatbuffers")
    set(FLATBUFFERS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/flatbuffers/include")
else()
    message(WARNING "FlatBuffers not found - protocol serialization may fail")
    set(FLATBUFFERS_INCLUDE_DIR "")
endif()

# GameNetworkingSockets - stub for now
# In production, this needs proper linking
message(STATUS "GameNetworkingSockets: Using stub implementation for initial build")
set(GNS_FOUND FALSE)

# Redis (hiredis) - stub for now
message(STATUS "hiredis: Using stub implementation for initial build")
set(HIREDIS_FOUND FALSE)

# ============================================================================
# Core library (header-only parts and compilable units)
# ============================================================================

# Collect source files
set(SERVER_SOURCES
    src/server/src/ecs/CoreTypes.cpp
    src/server/src/physics/SpatialHash.cpp
    src/server/src/physics/MovementSystem.cpp
    src/server/src/memory/MemoryPool.cpp
    src/server/src/memory/LeakDetector.cpp
    src/server/src/combat/CombatSystem.cpp
    src/server/src/combat/PositionHistory.cpp
    src/server/src/combat/LagCompensatedCombat.cpp
    src/server/src/zones/AreaOfInterest.cpp
    src/server/src/zones/AuraProjection.cpp
    src/server/src/zones/ReplicationOptimizer.cpp
    src/server/src/zones/EntityMigration.cpp
    src/server/src/zones/ZoneHandoff.cpp
    src/server/src/zones/ZoneOrchestrator.cpp
    src/server/src/zones/ZoneServer.cpp
    src/server/src/zones/CrossZoneMessenger.cpp
    src/server/src/security/AntiCheat.cpp
    src/server/src/security/DDoSProtection.cpp
    src/server/src/profiling/PerformanceMonitor.cpp
)

# Conditionally add network and DB sources if dependencies are available
if(GNS_FOUND)
    list(APPEND SERVER_SOURCES src/server/src/netcode/NetworkManager.cpp)
else()
    message(STATUS "Using NetworkManager stub - GNS not available")
    list(APPEND SERVER_SOURCES src/server/src/netcode/NetworkManager_stub.cpp)
endif()

if(HIREDIS_FOUND)
    list(APPEND SERVER_SOURCES src/server/src/db/RedisManager.cpp)
    list(APPEND SERVER_SOURCES src/server/src/db/ScyllaManager.cpp)
else()
    message(STATUS "Using DB manager stubs - hiredis not available")
    list(APPEND SERVER_SOURCES src/server/src/db/RedisManager_stub.cpp)
    list(APPEND SERVER_SOURCES src/server/src/db/ScyllaManager_stub.cpp)
endif()

if(ENABLE_PROFILING)
    list(APPEND SERVER_SOURCES src/server/src/profiling/PerfettoProfiler.cpp)
else()
    list(APPEND SERVER_SOURCES src/server/src/profiling/PerfettoProfiler_stub.cpp)
endif()

# Create object library for shared compilation
add_library(darkages_core OBJECT ${SERVER_SOURCES})

target_include_directories(darkages_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src/server/include
    ${ENTT_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
)

if(FLATBUFFERS_INCLUDE_DIR)
    target_include_directories(darkages_core PUBLIC ${FLATBUFFERS_INCLUDE_DIR})
endif()

target_link_libraries(darkages_core PUBLIC Threads::Threads)

# Main executable (minimal for now)
add_executable(darkages_server src/server/src/main.cpp)
target_link_libraries(darkages_server PRIVATE darkages_core)

# ============================================================================
# Testing
# ============================================================================

if(BUILD_TESTS)
    # Fetch Catch2
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.0
    )
    FetchContent_MakeAvailable(Catch2)
    
    # Test executables - Comprehensive test suite for all phases
    # Note: TestMain.cpp is excluded - it has its own main() for standalone testing
    set(TEST_SOURCES
        # Phase 0: Foundation Tests
        src/server/tests/TestSpatialHash.cpp
        src/server/tests/TestMovementSystem.cpp
        src/server/tests/TestMemoryPool.cpp
        
        # Phase 1: Prediction & Reconciliation
        src/server/tests/TestNetworkProtocol.cpp
        
        # Phase 2: Multi-Player Sync
        src/server/tests/TestAreaOfInterest.cpp
        src/server/tests/TestReplicationOptimizer.cpp
        
        # Phase 3: Combat & Lag Compensation
        src/server/tests/TestCombatSystem.cpp
        src/server/tests/TestLagCompensator.cpp
        src/server/tests/TestLagCompensatedCombat.cpp
        
        # Phase 4: Spatial Sharding
        src/server/tests/TestAuraProjection.cpp
        src/server/tests/TestEntityMigration.cpp
        src/server/tests/TestZoneOrchestrator.cpp
        
        # Phase 5: Optimization & Security
        src/server/tests/TestDDoSProtection.cpp
        src/server/tests/TestProfiling.cpp
    )
    
    # Filter out tests that don't exist yet (for incremental development)
    set(FINAL_TEST_SOURCES)
    foreach(test_file ${TEST_SOURCES})
        if(EXISTS "${CMAKE_SOURCE_DIR}/${test_file}")
            list(APPEND FINAL_TEST_SOURCES ${test_file})
        else()
            message(STATUS "Test file not found (skipping): ${test_file}")
        endif()
    endforeach()
    
    add_executable(darkages_tests ${FINAL_TEST_SOURCES})
    target_link_libraries(darkages_tests PRIVATE darkages_core Catch2::Catch2WithMain)
    
    # Add compile definition for test data path
    target_compile_definitions(darkages_tests PRIVATE 
        TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/test_data"
    )
    
    enable_testing()
    add_test(NAME unit_tests COMMAND darkages_tests --reporter console)
    
    # Add individual test suites for CI/CD reporting
    add_test(NAME test_spatial COMMAND darkages_tests "[spatial]")
    add_test(NAME test_movement COMMAND darkages_tests "[movement]")
    add_test(NAME test_memory COMMAND darkages_tests "[memory]")
    add_test(NAME test_combat COMMAND darkages_tests "[combat]")
    add_test(NAME test_aoi COMMAND darkages_tests "[aoi]")
    add_test(NAME test_anticheat COMMAND darkages_tests "[anticheat]")
    add_test(NAME test_network COMMAND darkages_tests "[network]")
    add_test(NAME test_zones COMMAND darkages_tests "[zones]")
    add_test(NAME test_security COMMAND darkages_tests "[security]")
endif()

# ============================================================================
# Status output
# ============================================================================

message("")
message("========================================")
message("DarkAges Server Build Configuration")
message("========================================")
message("C++ Standard: ${CMAKE_CXX_STANDARD}")
message("Build type: ${CMAKE_BUILD_TYPE}")
message("Build tests: ${BUILD_TESTS}")
message("AddressSanitizer: ${ENABLE_ASAN}")
message("EnTT: ${ENTT_INCLUDE_DIR}")
message("FlatBuffers: ${FLATBUFFERS_INCLUDE_DIR}")
message("GameNetworkingSockets: ${GNS_FOUND}")
message("hiredis: ${HIREDIS_FOUND}")
message("========================================")
message("")
