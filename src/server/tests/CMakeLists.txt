# DarkAges MMO - Unit Tests
# Uses Catch2 testing framework

cmake_minimum_required(VERSION 3.20)

# Fetch Catch2
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

# Test sources - base tests
set(TEST_SOURCES
    TestSpatialHash.cpp
    TestMovementSystem.cpp
    TestDeltaCompression.cpp
    TestReplicationOptimizer.cpp
    TestAreaOfInterest.cpp
    TestAuraProjection.cpp
    TestLagCompensator.cpp
    TestLagCompensatedCombat.cpp
    TestCombatSystem.cpp
    TestZoneOrchestrator.cpp
    TestEntityMigration.cpp
    TestMemoryPool.cpp
    TestProfiling.cpp
    TestDDoSProtection.cpp
    TestAntiCheat.cpp
    TestPacketValidator.cpp
    TestStatisticalDetector.cpp
    TestPacketIntegrity.cpp
    TestAuraZoneHandler.cpp
    TestCombatEventHandler.cpp
    TestRedisIntegration.cpp
    BenchmarkRedis.cpp
)

# Conditional test sources based on dependencies
if(GNS_AVAILABLE)
    list(APPEND TEST_SOURCES TestGNSIntegration.cpp)
    # TestNetworkProtocol requires protobuf - only include if protobuf generated
    if(EXISTS ${CMAKE_SOURCE_DIR}/../shared/game_protocol_generated.h)
        list(APPEND TEST_SOURCES TestNetworkProtocol.cpp)
    endif()
endif()

# Scylla tests - include if driver available (always off for now since we use stub)
# list(APPEND TEST_SOURCES TestScyllaManager.cpp)

# Server sources needed for tests - use stubs when dependencies unavailable
set(TEST_SERVER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ecs/CoreTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/SpatialHash.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/MovementSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ZoneServer.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/CombatEventHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/AuraZoneHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ZoneOrchestrator.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ReplicationOptimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/AreaOfInterest.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/AuraProjection.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/EntityMigration.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/PositionHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/CombatSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/LagCompensatedCombat.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/MemoryPool.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/LeakDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/security/AntiCheat.cpp
    ${CMAKE_SOURCE_DIR}/src/security/ViolationTracker.cpp
    ${CMAKE_SOURCE_DIR}/src/security/MovementValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/security/DDoSProtection.cpp
    ${CMAKE_SOURCE_DIR}/src/security/PacketValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/security/StatisticalDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/security/PacketIntegrity.cpp
    ${CMAKE_SOURCE_DIR}/src/security/CircuitBreaker.cpp
    ${CMAKE_SOURCE_DIR}/src/security/InputValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/security/ConnectionThrottler.cpp
    ${CMAKE_SOURCE_DIR}/src/security/TrafficAnalyzer.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/PlayerManager.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/AbilitySystem.cpp
    ${CMAKE_SOURCE_DIR}/src/monitoring/MetricsExporter.cpp
    ${CMAKE_SOURCE_DIR}/src/profiling/PerformanceMonitor.cpp
    ${CMAKE_SOURCE_DIR}/src/profiling/PerfettoProfiler_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ZoneHandoff.cpp
)

# Network manager: use stub if GNS not available
if(GNS_AVAILABLE)
    list(APPEND TEST_SERVER_SOURCES
        ${CMAKE_SOURCE_DIR}/src/netcode/NetworkManager.cpp
    )
else()
    list(APPEND TEST_SERVER_SOURCES
        ${CMAKE_SOURCE_DIR}/src/netcode/NetworkManager_stub.cpp
    )
endif()

# Redis: use stub if not available
if(REDIS_AVAILABLE)
    list(APPEND TEST_SERVER_SOURCES ${CMAKE_SOURCE_DIR}/src/db/RedisManager.cpp)
else()
    list(APPEND TEST_SERVER_SOURCES ${CMAKE_SOURCE_DIR}/src/db/RedisManager_stub.cpp)
endif()

# Scylla: always use stub
list(APPEND TEST_SERVER_SOURCES ${CMAKE_SOURCE_DIR}/src/db/ScyllaManager_stub.cpp)

add_executable(darkages-tests ${TEST_SOURCES} ${TEST_SERVER_SOURCES})

target_include_directories(darkages-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/../shared
)

target_link_libraries(darkages-tests PRIVATE
    Catch2::Catch2WithMain
    EnTT::EnTT
    glm::glm
    Threads::Threads
)

# Link Redis if available (same check as main CMakeLists.txt)
if(REDIS_AVAILABLE)
    target_link_libraries(darkages-tests PRIVATE ${HIREDIS_LIBRARIES})
    target_include_directories(darkages-tests PRIVATE ${HIREDIS_INCLUDE_DIRS})
    target_compile_definitions(darkages-tests PRIVATE REDIS_AVAILABLE=1)
endif()

# Register tests with CTest
include(Catch)
catch_discover_tests(darkages-tests)

# Copy test data if needed
# file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
