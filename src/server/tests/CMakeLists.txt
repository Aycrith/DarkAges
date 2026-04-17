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

# Test sources
set(TEST_SOURCES
    TestMain.cpp
    TestSpatialHash.cpp
    TestMovementSystem.cpp
    TestNetworkProtocol.cpp
    TestDeltaCompression.cpp
    TestReplicationOptimizer.cpp
    TestAreaOfInterest.cpp
    TestAuraProjection.cpp
    TestLagCompensator.cpp
    TestLagCompensatedCombat.cpp
    TestCombatSystem.cpp
    TestAbilitySystem.cpp
    TestProjectileSystem.cpp
    TestZoneOrchestrator.cpp
    TestEntityMigration.cpp
    TestMemoryPool.cpp
    TestProfiling.cpp
    TestMetricsExporter.cpp
    TestDDoSProtection.cpp
    TestRedisIntegration.cpp
    TestCombatEventHandler.cpp
    TestAuraZoneHandler.cpp
    TestSecuritySubsystems.cpp
    TestPacketIntegrity.cpp
    TestRateLimiter.cpp
    TestZoneManager.cpp
    TestStreamManager.cpp
    TestPlayerSessionManager.cpp
    BenchmarkRedis.cpp
)

# Server sources needed for tests (excluding main.cpp)
set(TEST_SERVER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ecs/CoreTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/SpatialHash.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/MovementSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/netcode/NetworkManager.cpp
    ${CMAKE_SOURCE_DIR}/src/db/RedisManager.cpp
    ${CMAKE_SOURCE_DIR}/src/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/db/PlayerSessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/db/ScyllaManager.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ZoneServer.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ZoneOrchestrator.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ReplicationOptimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/AreaOfInterest.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/AuraProjection.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/EntityMigration.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/CrossZoneMessenger.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/PlayerManager.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/CombatEventHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/AuraZoneHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/InputHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/PerformanceHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/PositionHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/CombatSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/LagCompensatedCombat.cpp
    ${CMAKE_SOURCE_DIR}/src/combat/AbilitySystem.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/MemoryPool.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/LeakDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/security/AntiCheat.cpp
    ${CMAKE_SOURCE_DIR}/src/security/MovementValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/security/ViolationTracker.cpp
    ${CMAKE_SOURCE_DIR}/src/security/DDoSProtection.cpp
    ${CMAKE_SOURCE_DIR}/src/security/CircuitBreaker.cpp
    ${CMAKE_SOURCE_DIR}/src/security/InputValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/security/ConnectionThrottler.cpp
    ${CMAKE_SOURCE_DIR}/src/security/TrafficAnalyzer.cpp
    ${CMAKE_SOURCE_DIR}/src/security/PacketIntegrity.cpp
    ${CMAKE_SOURCE_DIR}/src/security/RateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/db/ZoneManager.cpp
    ${CMAKE_SOURCE_DIR}/src/monitoring/MetricsExporter.cpp
)

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
