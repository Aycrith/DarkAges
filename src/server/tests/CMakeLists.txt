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
    TestZoneOrchestrator.cpp
    TestEntityMigration.cpp
    TestMemoryPool.cpp
    TestProfiling.cpp
    TestDDoSProtection.cpp
)

# Server sources needed for tests (excluding main.cpp)
set(TEST_SERVER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ecs/CoreTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/SpatialHash.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/MovementSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/netcode/NetworkManager.cpp
    ${CMAKE_SOURCE_DIR}/src/db/RedisManager.cpp
    ${CMAKE_SOURCE_DIR}/src/db/ScyllaManager.cpp
    ${CMAKE_SOURCE_DIR}/src/zones/ZoneServer.cpp
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
    ${CMAKE_SOURCE_DIR}/src/security/DDoSProtection.cpp
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

# Register tests with CTest
include(Catch)
catch_discover_tests(darkages-tests)

# Copy test data if needed
# file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
