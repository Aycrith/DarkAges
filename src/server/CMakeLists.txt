# DarkAges MMO - Zone Server CMake Configuration
# Phase 0: Foundation

cmake_minimum_required(VERSION 3.20)
project(DarkAgesServer VERSION 0.1.0 LANGUAGES CXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(ENABLE_PROFILING "Enable Perfetto tracing" ON)

# Find packages
find_package(Threads REQUIRED)

# EnTT ECS Library
include(FetchContent)
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(entt)

# GameNetworkingSockets (assumed to be in deps/)
set(GNS_DIR ${CMAKE_SOURCE_DIR}/../../deps/GameNetworkingSockets)
if(EXISTS ${GNS_DIR}/CMakeLists.txt)
    add_subdirectory(${GNS_DIR} ${CMAKE_BINARY_DIR}/gns)
else()
    message(WARNING "GameNetworkingSockets not found in deps/. Building without networking.")
    set(GNS_AVAILABLE FALSE)
endif()

# glm for math
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# hiredis for Redis
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HIREDIS hiredis)
endif()
if(NOT HIREDIS_FOUND)
    message(WARNING "hiredis not found. Building without Redis support.")
    set(REDIS_AVAILABLE FALSE)
endif()

# Source files
set(SERVER_SOURCES
    src/main.cpp
    src/ecs/CoreTypes.cpp
    src/physics/SpatialHash.cpp
    src/physics/MovementSystem.cpp
    src/netcode/NetworkManager.cpp
    src/db/RedisManager.cpp
    src/db/ScyllaManager.cpp
    src/zones/ZoneServer.cpp
    src/zones/ZoneOrchestrator.cpp
    src/zones/ReplicationOptimizer.cpp
    src/zones/AreaOfInterest.cpp
    src/zones/AuraProjection.cpp
    src/zones/EntityMigration.cpp
    src/zones/CrossZoneMessenger.cpp
    src/zones/ZoneHandoff.cpp
    src/combat/PositionHistory.cpp
    src/combat/CombatSystem.cpp
    src/combat/LagCompensatedCombat.cpp
    src/profiling/PerfettoProfiler.cpp
    src/profiling/PerformanceMonitor.cpp
    src/memory/MemoryPool.cpp
    src/memory/LeakDetector.cpp
    src/security/AntiCheat.cpp
    src/security/DDoSProtection.cpp
)

# Header files
set(SERVER_HEADERS
    include/ecs/CoreTypes.hpp
    include/ecs/Components.hpp
    include/physics/SpatialHash.hpp
    include/physics/MovementSystem.hpp
    include/netcode/NetworkManager.hpp
    include/netcode/Protocol.hpp
    include/db/RedisManager.hpp
    include/db/ScyllaManager.hpp
    include/zones/ZoneServer.hpp
    include/zones/ZoneDefinition.hpp
    include/zones/ZoneOrchestrator.hpp
    include/zones/ReplicationOptimizer.hpp
    include/zones/AreaOfInterest.hpp
    include/zones/AuraProjection.hpp
    include/zones/EntityMigration.hpp
    include/zones/CrossZoneMessenger.hpp
    include/zones/ZoneHandoff.hpp
    include/combat/PositionHistory.hpp
    include/combat/CombatSystem.hpp
    include/combat/LagCompensatedCombat.hpp
    include/profiling/PerfettoProfiler.hpp
    include/profiling/PerformanceMonitor.hpp
    include/memory/MemoryPool.hpp
    include/memory/MemoryPool.inl
    include/memory/FixedVector.hpp
    include/memory/LeakDetector.hpp
    include/memory/PooledAllocator.hpp
    include/Constants.hpp
    include/security/AntiCheat.hpp
    include/security/AntiCheatConfig.hpp
    include/security/DDoSProtection.hpp
    include/security/RateLimiter.hpp
)

# Main executable
add_executable(darkages-server ${SERVER_SOURCES} ${SERVER_HEADERS})

target_include_directories(darkages-server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/../shared
)

target_link_libraries(darkages-server PRIVATE
    Threads::Threads
    EnTT::EnTT
    glm::glm
)

if(GNS_AVAILABLE)
    target_link_libraries(darkages-server PRIVATE GameNetworkingSockets::GameNetworkingSockets)
    target_compile_definitions(darkages-server PRIVATE GNS_AVAILABLE=1)
endif()

if(REDIS_AVAILABLE)
    target_link_libraries(darkages-server PRIVATE ${HIREDIS_LIBRARIES})
    target_include_directories(darkages-server PRIVATE ${HIREDIS_INCLUDE_DIRS})
    target_compile_definitions(darkages-server PRIVATE REDIS_AVAILABLE=1)
endif()

# Enable profiling
if(ENABLE_PROFILING)
    target_compile_definitions(darkages-server PRIVATE ENABLE_PROFILING=1)
endif()

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(darkages-server PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wcast-align
        -Wformat=2
        -Wstrict-overflow=2
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(darkages-server PRIVATE
            -O3
            -march=x86-64
            -ffast-math
            -flto=auto
        )
        target_link_options(darkages-server PRIVATE
            -flto=auto
        )
    endif()
    
    if(ENABLE_SANITIZERS)
        target_compile_options(darkages-server PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(darkages-server PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endif()

if(MSVC)
    target_compile_options(darkages-server PRIVATE
        /W4
        /WX-
        /permissive-
        /Zc:__cplusplus
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(darkages-server PRIVATE
            /O2
            /GL
            /LTCG
        )
    endif()
endif()

# Testing
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration
message(STATUS "")
message(STATUS "DarkAges Server Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "  Profiling: ${ENABLE_PROFILING}")
message(STATUS "  GNS Available: ${GNS_AVAILABLE}")
message(STATUS "  Redis Available: ${REDIS_AVAILABLE}")
message(STATUS "")
