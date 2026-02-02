cmake_minimum_required(VERSION 3.20)

# ============================================================================
# CRITICAL: MSVC Runtime Library - Must be set before ANY FetchContent_Declare
# ============================================================================
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(DarkAgesServer VERSION 0.7.0 LANGUAGES CXX)

# C++20 is required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build tests" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_PROFILING "Enable Perfetto profiling" OFF)

# Phase 6 External Integration Options
option(ENABLE_GNS "Enable GameNetworkingSockets" ON)
option(ENABLE_REDIS "Enable Redis integration" ON)
option(ENABLE_SCYLLA "Enable ScyllaDB integration" ON)
option(ENABLE_FLATBUFFERS "Enable FlatBuffers protocol" ON)
option(FETCH_DEPENDENCIES "Fetch external dependencies automatically" ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
    add_compile_options(/permissive-)
    add_compile_options(/Zc:__cplusplus)
    add_compile_options(/MP)  # Multi-processor compilation
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-Wno-unused-parameter -Wno-unused-variable)
    add_compile_options(-Wno-missing-field-initializers)
endif()

# AddressSanitizer for debug builds
if(ENABLE_ASAN AND NOT MSVC)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Find required packages
find_package(Threads REQUIRED)

# ============================================================================
# Local Dependencies (EnTT, GLM)
# ============================================================================

# Check for EnTT
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/entt/single_include/entt/entt.hpp")
    message(STATUS "EnTT found in deps/entt")
    set(ENTT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/entt/single_include")
    set(ENTT_FOUND TRUE)
else()
    if(FETCH_DEPENDENCIES)
        message(STATUS "Fetching EnTT...")
        include(FetchContent)
        FetchContent_Declare(
            EnTT
            GIT_REPOSITORY https://github.com/skypjack/entt.git
            GIT_TAG v3.13.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(EnTT)
        set(ENTT_INCLUDE_DIR "${entt_SOURCE_DIR}/single_include")
        set(ENTT_FOUND TRUE)
    else()
        message(FATAL_ERROR "EnTT not found. Run: git clone --branch v3.13.0 https://github.com/skypjack/entt.git deps/entt")
    endif()
endif()

# Check for GLM
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/glm/glm/glm.hpp")
    message(STATUS "GLM found in deps/glm")
    set(GLM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/glm")
    set(GLM_FOUND TRUE)
else()
    if(FETCH_DEPENDENCIES)
        message(STATUS "Fetching GLM...")
        include(FetchContent)
        FetchContent_Declare(
            glm
            GIT_REPOSITORY https://github.com/g-truc/glm.git
            GIT_TAG 0.9.9.8
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(glm)
        set(GLM_INCLUDE_DIR "${glm_SOURCE_DIR}")
        set(GLM_FOUND TRUE)
    else()
        message(FATAL_ERROR "GLM not found. Run: git clone --branch 0.9.9.8 https://github.com/g-truc/glm.git deps/glm")
    endif()
endif()

# ============================================================================
# External Dependencies (Phase 6) - ALL Declarations First
# ============================================================================

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---------------------------------------------------------------------------
# Declare ALL external dependencies BEFORE any MakeAvailable
# ---------------------------------------------------------------------------

# nlohmann/json (v3.11.3) - JSON serialization
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

# FlatBuffers (v24.3.25) - Protocol serialization
if(ENABLE_FLATBUFFERS)
    FetchContent_Declare(
        flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG v24.3.25
        GIT_SHALLOW TRUE
    )
endif()

# GameNetworkingSockets (v1.4.1) - UDP networking
# NOTE: GNS on Windows requires system-installed Protobuf. Using stub for now.
if(ENABLE_GNS AND NOT WIN32)
    FetchContent_Declare(
        GameNetworkingSockets
        GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
        GIT_TAG v1.4.1
        GIT_SHALLOW TRUE
    )
endif()

# hiredis (v1.2.0) - Redis client
# Using a fork with updated CMake for compatibility
if(ENABLE_REDIS)
    FetchContent_Declare(
        hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG v1.2.0
        GIT_SHALLOW TRUE
    )
endif()

# Workaround for old CMake in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# cassandra-cpp-driver (2.17.1) - ScyllaDB/Cassandra client
# NOTE: Disabled on Windows due to complex build requirements
if(ENABLE_SCYLLA AND NOT WIN32)
    FetchContent_Declare(
        cassandra
        GIT_REPOSITORY https://github.com/datastax/cpp-driver.git
        GIT_TAG 2.17.1
        GIT_SHALLOW TRUE
    )
endif()

# Protobuf for network protocol
FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v3.21.12
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_ABSL_PROVIDER "module" CACHE STRING "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

# Catch2 for testing
if(BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.0
        GIT_SHALLOW TRUE
    )
endif()

# ---------------------------------------------------------------------------
# Set dependency build options BEFORE MakeAvailable
# ---------------------------------------------------------------------------

# Common options for all dependencies
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Note: Don't override BUILD_TESTS here - it's controlled by the option at line 19
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)

# FlatBuffers options
if(ENABLE_FLATBUFFERS)
    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATBUFFERS OFF CACHE BOOL "" FORCE)
endif()

# GameNetworkingSockets options
if(ENABLE_GNS)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)
    
    # Tell GNS to use the protobuf we're building
    set(Protobuf_LITE_LIBRARY "protobuf::libprotobuf-lite" CACHE STRING "" FORCE)
    set(Protobuf_LIBRARY "protobuf::libprotobuf" CACHE STRING "" FORCE)
    set(Protobuf_PROTOC_LIBRARY "protobuf::protoc" CACHE STRING "" FORCE)
    set(Protobuf_PROTOC_EXECUTABLE "protoc" CACHE STRING "" FORCE)
    
    # Crypto library selection
    if(NOT WIN32)
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            set(USE_CRYPTO "OpenSSL" CACHE STRING "" FORCE)
        else()
            set(USE_CRYPTO "MBEDTLS" CACHE STRING "" FORCE)
        endif()
    else()
        # Windows uses built-in crypto
        set(USE_CRYPTO "BCrypt" CACHE STRING "" FORCE)
    endif()
endif()

# hiredis options
if(ENABLE_REDIS)
    set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
endif()

# cassandra options
if(ENABLE_SCYLLA AND NOT WIN32)
    set(CASS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(CASS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(CASS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CASS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CASS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Make ALL dependencies available in ONE call
# ---------------------------------------------------------------------------

set(EXTERNAL_DEPS json protobuf)

if(ENABLE_FLATBUFFERS)
    list(APPEND EXTERNAL_DEPS flatbuffers)
endif()

if(ENABLE_GNS AND NOT WIN32)
    list(APPEND EXTERNAL_DEPS GameNetworkingSockets)
endif()

if(ENABLE_REDIS)
    list(APPEND EXTERNAL_DEPS hiredis)
endif()

if(ENABLE_SCYLLA AND NOT WIN32)
    list(APPEND EXTERNAL_DEPS cassandra)
endif()

if(BUILD_TESTS)
    list(APPEND EXTERNAL_DEPS Catch2)
endif()

FetchContent_MakeAvailable(${EXTERNAL_DEPS})

# ============================================================================
# Create Library Target Aliases for Consistent Naming
# ============================================================================

# GameNetworkingSockets (Linux only - Windows uses stub)
if(ENABLE_GNS AND NOT WIN32)
    if(NOT TARGET GameNetworkingSockets::GameNetworkingSockets)
        if(TARGET GameNetworkingSockets)
            add_library(GameNetworkingSockets::GameNetworkingSockets ALIAS GameNetworkingSockets)
        endif()
    endif()
    if(TARGET GameNetworkingSockets::GameNetworkingSockets)
        set(GNS_FOUND TRUE)
        set(GameNetworkingSockets_LIBRARIES GameNetworkingSockets::GameNetworkingSockets)
        message(STATUS "GameNetworkingSockets: Target available")
    else()
        set(GNS_FOUND FALSE)
        message(WARNING "GameNetworkingSockets: Target not available, will use stub")
    endif()
else()
    set(GNS_FOUND FALSE)
    if(WIN32)
        message(STATUS "GameNetworkingSockets: Using stub on Windows")
    else()
        message(STATUS "GameNetworkingSockets: Disabled (ENABLE_GNS=OFF)")
    endif()
endif()

# hiredis - create proper target alias
if(ENABLE_REDIS)
    if(TARGET hiredis AND NOT TARGET hiredis::hiredis)
        add_library(hiredis::hiredis ALIAS hiredis)
    endif()
    if(TARGET hiredis::hiredis)
        set(HIREDIS_FOUND TRUE)
        set(hiredis_LIBRARIES hiredis::hiredis)
        message(STATUS "hiredis: Target available")
    else()
        set(HIREDIS_FOUND FALSE)
        message(WARNING "hiredis: Target not available, will use stub")
    endif()
else()
    set(HIREDIS_FOUND FALSE)
endif()

# FlatBuffers
if(ENABLE_FLATBUFFERS)
    if(TARGET flatbuffers AND NOT TARGET flatbuffers::flatbuffers)
        add_library(flatbuffers::flatbuffers ALIAS flatbuffers)
    endif()
    if(TARGET flatbuffers::flatbuffers)
        set(FLATBUFFERS_FOUND TRUE)
        set(FlatBuffers_LIBRARIES flatbuffers::flatbuffers)
        message(STATUS "FlatBuffers: Target available")
    else()
        set(FLATBUFFERS_FOUND FALSE)
        message(WARNING "FlatBuffers: Target not available")
    endif()
else()
    set(FLATBUFFERS_FOUND FALSE)
endif()

# nlohmann/json
if(TARGET nlohmann_json AND NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    set(NLOHMANN_JSON_FOUND TRUE)
    message(STATUS "nlohmann/json: Target available")
else()
    set(NLOHMANN_JSON_FOUND FALSE)
endif()

# cassandra - only on non-Windows
if(ENABLE_SCYLLA AND NOT WIN32)
    if(TARGET cassandra_static AND NOT TARGET cassandra::cassandra)
        add_library(cassandra::cassandra ALIAS cassandra_static)
    endif()
    if(TARGET cassandra::cassandra)
        set(CASSANDRA_FOUND TRUE)
        set(cassandra_LIBRARIES cassandra::cassandra)
        message(STATUS "cassandra-cpp-driver: Target available")
    else()
        set(CASSANDRA_FOUND FALSE)
        message(WARNING "cassandra-cpp-driver: Target not available, will use stub")
    endif()
else()
    set(CASSANDRA_FOUND FALSE)
endif()

# ============================================================================
# FlatBuffers Code Generation
# ============================================================================

if(FLATBUFFERS_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/src/shared/proto/game_protocol.fbs")
    set(FBS_SCHEMA ${CMAKE_SOURCE_DIR}/src/shared/proto/game_protocol.fbs)
    set(FBS_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${FBS_GENERATED_DIR})
    
    # Determine flatc executable path - must use absolute path
    if(WIN32)
        set(FLATC_EXECUTABLE "${flatbuffers_BINARY_DIR}/Release/flatc.exe")
    else()
        set(FLATC_EXECUTABLE "${flatbuffers_BINARY_DIR}/flatc")
    endif()
    
    # First build flatc as a dependency
    add_custom_command(
        OUTPUT ${FLATC_EXECUTABLE}
        COMMAND ${CMAKE_COMMAND} --build ${flatbuffers_BINARY_DIR} --target flatc --config Release
        COMMENT "Building flatc compiler"
    )
    
    add_custom_command(
        OUTPUT 
            ${FBS_GENERATED_DIR}/game_protocol_generated.h
        COMMAND 
            ${FLATC_EXECUTABLE}
            --cpp
            --gen-object-api
            --gen-compare
            -o ${FBS_GENERATED_DIR}
            ${FBS_SCHEMA}
        DEPENDS ${FBS_SCHEMA} ${FLATC_EXECUTABLE}
        COMMENT "Generating FlatBuffers C++ code from ${FBS_SCHEMA}"
    )
    
    add_custom_target(fbs_generation ALL
        DEPENDS 
            ${FBS_GENERATED_DIR}/game_protocol_generated.h
    )
    
    set(FLATBUFFERS_GENERATED_DIR ${FBS_GENERATED_DIR})
    message(STATUS "FlatBuffers code generation configured")
endif()

# ============================================================================
# Core Library Source Files - Select Real vs Stub Implementations
# ============================================================================

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
    src/server/src/security/PacketValidator.cpp
    src/server/src/profiling/PerformanceMonitor.cpp
)

# Network layer - conditional on GameNetworkingSockets
if(GNS_FOUND)
    list(APPEND SERVER_SOURCES 
        src/server/src/netcode/NetworkManager.cpp
        src/server/src/netcode/GNSNetworkManager.cpp
    )
    message(STATUS "NetworkManager: Using GameNetworkingSockets implementation")
    message(STATUS "  - GNSNetworkManager: Full-featured implementation")
else()
    list(APPEND SERVER_SOURCES 
        src/server/src/netcode/NetworkManager_stub.cpp
        src/server/src/netcode/ProtobufProtocol.cpp
    )
    message(WARNING "NetworkManager: Using stub implementation with Protobuf")
endif()

# Protocol layer - conditional on FlatBuffers AND real NetworkManager
# (Protocol.cpp uses GNS types, so it must be skipped if using stub)
if(FLATBUFFERS_FOUND AND GNS_FOUND)
    list(APPEND SERVER_SOURCES src/server/src/netcode/Protocol.cpp)
    message(STATUS "Protocol: Using FlatBuffers implementation")
else()
    message(STATUS "Protocol: Using stub implementation (GNS not available)")
endif()

# Database layer - conditional on hiredis
if(HIREDIS_FOUND)
    list(APPEND SERVER_SOURCES src/server/src/db/RedisManager.cpp)
    message(STATUS "RedisManager: Using hiredis implementation")
else()
    list(APPEND SERVER_SOURCES src/server/src/db/RedisManager_stub.cpp)
    message(WARNING "RedisManager: Using stub implementation")
endif()

# ScyllaDB layer - conditional on cassandra-cpp-driver (stub on Windows)
if(CASSANDRA_FOUND)
    list(APPEND SERVER_SOURCES src/server/src/db/ScyllaManager.cpp)
    message(STATUS "ScyllaManager: Using cassandra-cpp-driver implementation")
else()
    list(APPEND SERVER_SOURCES src/server/src/db/ScyllaManager_stub.cpp)
    if(WIN32)
        message(STATUS "ScyllaManager: Using stub implementation (Windows build)")
    else()
        message(WARNING "ScyllaManager: Using stub implementation")
    endif()
endif()

# Profiling layer
if(ENABLE_PROFILING)
    list(APPEND SERVER_SOURCES src/server/src/profiling/PerfettoProfiler.cpp)
else()
    list(APPEND SERVER_SOURCES src/server/src/profiling/PerfettoProfiler_stub.cpp)
endif()

# Monitoring layer (Prometheus metrics)
list(APPEND SERVER_SOURCES src/server/src/monitoring/MetricsExporter.cpp)
message(STATUS "Monitoring: Prometheus metrics enabled")

# ============================================================================
# Protobuf Code Generation
# ============================================================================

# Generate C++ code from .proto files
set(PROTO_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/proto)
set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_proto)
file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

# Find protoc
if(TARGET protobuf::protoc)
    set(PROTOC_EXECUTABLE protobuf::protoc)
else()
    find_program(PROTOC_EXECUTABLE protoc REQUIRED)
endif()

# Generate protobuf code
add_custom_command(
    OUTPUT ${PROTO_OUTPUT_DIR}/network_protocol.pb.cc
           ${PROTO_OUTPUT_DIR}/network_protocol.pb.h
    COMMAND ${PROTOC_EXECUTABLE}
        --cpp_out=${PROTO_OUTPUT_DIR}
        -I ${PROTO_INPUT_DIR}
        ${PROTO_INPUT_DIR}/network_protocol.proto
    DEPENDS ${PROTO_INPUT_DIR}/network_protocol.proto
    COMMENT "Generating protobuf C++ code from network_protocol.proto"
)

# Create a custom target for protobuf generation
add_custom_target(proto_generation
    DEPENDS ${PROTO_OUTPUT_DIR}/network_protocol.pb.cc
            ${PROTO_OUTPUT_DIR}/network_protocol.pb.h
)

# Add generated files to server sources
list(APPEND SERVER_SOURCES ${PROTO_OUTPUT_DIR}/network_protocol.pb.cc)

# ============================================================================
# darkages_core Library
# ============================================================================

add_library(darkages_core OBJECT ${SERVER_SOURCES})
add_dependencies(darkages_core proto_generation)

# Core include directories
target_include_directories(darkages_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/proto
    ${ENTT_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
)

# FlatBuffers include directories - proper order: generated first, then library
if(FLATBUFFERS_FOUND)
    if(FLATBUFFERS_GENERATED_DIR)
        target_include_directories(darkages_core PUBLIC ${FLATBUFFERS_GENERATED_DIR})
    endif()
    target_include_directories(darkages_core PUBLIC 
        ${flatbuffers_SOURCE_DIR}/include
    )
endif()

# External dependency compile definitions
if(FLATBUFFERS_FOUND)
    if(FLATBUFFERS_GENERATED_DIR)
        add_dependencies(darkages_core fbs_generation)
    endif()
    target_compile_definitions(darkages_core PUBLIC ENABLE_FLATBUFFERS=1)
endif()

if(GNS_FOUND)
    target_include_directories(darkages_core PUBLIC 
        ${GameNetworkingSockets_SOURCE_DIR}/include
    )
    target_compile_definitions(darkages_core PUBLIC ENABLE_GNS=1)
endif()

if(HIREDIS_FOUND)
    target_include_directories(darkages_core PUBLIC 
        ${hiredis_SOURCE_DIR}
        ${hiredis_BINARY_DIR}
    )
    target_compile_definitions(darkages_core PUBLIC ENABLE_REDIS=1 REDIS_AVAILABLE=1)
endif()

if(CASSANDRA_FOUND)
    target_include_directories(darkages_core PUBLIC 
        ${cassandra_SOURCE_DIR}/include
    )
    target_compile_definitions(darkages_core PUBLIC ENABLE_SCYLLA=1)
endif()

# nlohmann/json is always included (header-only)
target_include_directories(darkages_core PUBLIC 
    ${json_SOURCE_DIR}/single_include
)
target_compile_definitions(darkages_core PUBLIC ENABLE_NLOHMANN_JSON=1)

# Protobuf includes and linking
target_include_directories(darkages_core PUBLIC 
    ${PROTO_OUTPUT_DIR}
    ${protobuf_SOURCE_DIR}/src
)
if(TARGET protobuf::libprotobuf-lite)
    target_link_libraries(darkages_core PUBLIC protobuf::libprotobuf-lite)
elseif(TARGET protobuf::libprotobuf)
    target_link_libraries(darkages_core PUBLIC protobuf::libprotobuf)
endif()
target_compile_definitions(darkages_core PUBLIC ENABLE_PROTOBUF=1)

# Core linking
target_link_libraries(darkages_core PUBLIC Threads::Threads)

# External library linking with proper target names
if(FLATBUFFERS_FOUND AND TARGET flatbuffers::flatbuffers)
    target_link_libraries(darkages_core PUBLIC flatbuffers::flatbuffers)
endif()

if(GNS_FOUND AND TARGET GameNetworkingSockets::GameNetworkingSockets)
    target_link_libraries(darkages_core PUBLIC GameNetworkingSockets::GameNetworkingSockets)
    
    # Windows-specific libraries for GameNetworkingSockets
    if(WIN32)
        target_link_libraries(darkages_core PUBLIC 
            ws2_32
            winmm
            crypt32
            iphlpapi
            bcrypt
        )
    endif()
endif()

if(HIREDIS_FOUND AND TARGET hiredis::hiredis)
    target_link_libraries(darkages_core PUBLIC hiredis::hiredis)
endif()

if(CASSANDRA_FOUND AND TARGET cassandra::cassandra)
    target_link_libraries(darkages_core PUBLIC cassandra::cassandra)
endif()

# ============================================================================
# darkages_server Executable
# ============================================================================

add_executable(darkages_server src/server/src/main.cpp)

target_link_libraries(darkages_server PRIVATE
    darkages_core
)

# Windows subsystem settings
if(WIN32)
    set_target_properties(darkages_server PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()

# ============================================================================
# Testing
# ============================================================================

if(BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    set(TEST_SOURCES
        src/server/tests/TestSpatialHash.cpp
        src/server/tests/TestMovementSystem.cpp
        src/server/tests/TestMemoryPool.cpp
        # src/server/tests/TestNetworkProtocol.cpp  # Temporarily disabled - missing protocol implementations
        src/server/tests/TestAreaOfInterest.cpp
        src/server/tests/TestReplicationOptimizer.cpp
        src/server/tests/TestCombatSystem.cpp
        src/server/tests/TestLagCompensator.cpp
        src/server/tests/TestLagCompensatedCombat.cpp
        src/server/tests/TestAuraProjection.cpp
        src/server/tests/TestEntityMigration.cpp
        src/server/tests/TestZoneOrchestrator.cpp
        src/server/tests/TestDDoSProtection.cpp
        src/server/tests/TestProfiling.cpp
        src/server/tests/TestGNSIntegration.cpp
        src/server/tests/TestRedisIntegration.cpp
        # src/server/tests/TestScyllaManager.cpp  # Temporarily disabled - ScyllaDB stub
        # tests/integration/gamestate/TestGamestateSynchronization.cpp  # Temporarily disabled - needs refactoring
    )
    
    # Filter out tests that don't exist yet
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
    target_compile_definitions(darkages_tests PRIVATE 
        TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/test_data"
    )
    
    enable_testing()
    add_test(NAME unit_tests COMMAND darkages_tests --reporter console)
    
    # Individual test suites for CI/CD reporting
    add_test(NAME test_spatial COMMAND darkages_tests "[spatial]")
    add_test(NAME test_movement COMMAND darkages_tests "[movement]")
    add_test(NAME test_memory COMMAND darkages_tests "[memory]")
    add_test(NAME test_combat COMMAND darkages_tests "[combat]")
    add_test(NAME test_aoi COMMAND darkages_tests "[aoi]")
    add_test(NAME test_network COMMAND darkages_tests "[network]")
    add_test(NAME test_zones COMMAND darkages_tests "[zones]")
    add_test(NAME test_security COMMAND darkages_tests "[security]")
    add_test(NAME test_database COMMAND darkages_tests "[database]")
endif()

# ============================================================================
# Install Targets
# ============================================================================

install(TARGETS darkages_server
    RUNTIME DESTINATION bin
)

install(DIRECTORY src/server/include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# ============================================================================
# Build Status Output
# ============================================================================

message("")
message("========================================")
message("DarkAges Server Build Configuration")
message("========================================")
message("C++ Standard: ${CMAKE_CXX_STANDARD}")
message("Build type: ${CMAKE_BUILD_TYPE}")
message("Build tests: ${BUILD_TESTS}")
message("AddressSanitizer: ${ENABLE_ASAN}")
message("")
message("Dependencies:")
message("  EnTT: ${ENTT_FOUND} - ${ENTT_INCLUDE_DIR}")
message("  GLM: ${GLM_FOUND} - ${GLM_INCLUDE_DIR}")
message("  FlatBuffers: ${FLATBUFFERS_FOUND}")
message("  GameNetworkingSockets: ${GNS_FOUND}")
message("  hiredis: ${HIREDIS_FOUND}")
message("  cassandra-cpp-driver: ${CASSANDRA_FOUND}")
message("  nlohmann/json: ${NLOHMANN_JSON_FOUND}")
message("========================================")
message("")
