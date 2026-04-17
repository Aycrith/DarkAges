// PlayerSessionManager unit tests - covers session CRUD and player lookup
// Tests stub behavior when Redis is not available

#include <catch2/catch_test_macros.hpp>
#include "db/RedisManager.hpp"
#include "db/PlayerSessionManager.hpp"
#include <atomic>
#include <cstring>

using namespace DarkAges;

// Helper to populate a PlayerSession with test data
static PlayerSession makeSession(uint64_t playerId, uint32_t zoneId = 1,
                                  uint32_t connectionId = 42, int32_t health = 100) {
    PlayerSession session{};
    session.playerId = playerId;
    session.zoneId = zoneId;
    session.connectionId = connectionId;
    session.health = health;
    session.position.x = 100;
    session.position.y = 0;
    session.position.z = 200;
    session.position.timestamp_ms = 1000;
    session.lastActivity = 5000;
    std::strncpy(session.username, "TestPlayer", sizeof(session.username) - 1);
    return session;
}

// ============================================================================
// PlayerSession struct defaults
// ============================================================================

TEST_CASE("PlayerSession default construction", "[foundation][database]") {
    PlayerSession session{};

    REQUIRE(session.playerId == 0);
    REQUIRE(session.zoneId == 0);
    REQUIRE(session.connectionId == 0);
    REQUIRE(session.health == 0);
    REQUIRE(session.lastActivity == 0);
    REQUIRE(session.position.x == 0);
    REQUIRE(session.position.y == 0);
    REQUIRE(session.position.z == 0);
    REQUIRE(session.position.timestamp_ms == 0);
    REQUIRE(session.username[0] == '\0');
}

TEST_CASE("PlayerSession helper populates fields", "[foundation][database]") {
    auto session = makeSession(1001, 5, 99, 75);

    REQUIRE(session.playerId == 1001);
    REQUIRE(session.zoneId == 5);
    REQUIRE(session.connectionId == 99);
    REQUIRE(session.health == 75);
    REQUIRE(session.position.x == 100);
    REQUIRE(session.position.y == 0);
    REQUIRE(session.position.z == 200);
    REQUIRE(session.position.timestamp_ms == 1000);
    REQUIRE(session.lastActivity == 5000);
    REQUIRE(std::string(session.username) == "TestPlayer");
}

// ============================================================================
// PlayerSessionManager construction
// ============================================================================

TEST_CASE("PlayerSessionManager construction", "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    // Construction should not crash
    REQUIRE(true);
}

// ============================================================================
// Stub behavior - savePlayerSession
// ============================================================================

TEST_CASE("PlayerSessionManager savePlayerSession with null callback does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    auto session = makeSession(100);
    REQUIRE_NOTHROW(psm.savePlayerSession(session));
}

TEST_CASE("PlayerSessionManager savePlayerSession with callback does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    auto session = makeSession(200);
    bool callbackCalled = false;
    bool callbackResult = false;

    REQUIRE_NOTHROW(psm.savePlayerSession(session, [&](bool success) {
        callbackCalled = true;
        callbackResult = success;
    }));

    // Stub: callback is not invoked (no-op behavior)
    // Real connected: callback fires with success=true
    // Just verify no crash occurred
    (void)callbackCalled;
    (void)callbackResult;
}

// ============================================================================
// Stub behavior - loadPlayerSession
// ============================================================================

TEST_CASE("PlayerSessionManager loadPlayerSession with callback does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    bool callbackCalled = false;

    REQUIRE_NOTHROW(psm.loadPlayerSession(300, [&](const AsyncResult<PlayerSession>& result) {
        callbackCalled = true;
        (void)result.success;
        (void)result.value;
    }));

    // Stub: callback is not invoked
    (void)callbackCalled;
}

// ============================================================================
// Stub behavior - removePlayerSession
// ============================================================================

TEST_CASE("PlayerSessionManager removePlayerSession with null callback does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    REQUIRE_NOTHROW(psm.removePlayerSession(400));
}

TEST_CASE("PlayerSessionManager removePlayerSession with callback does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    bool callbackCalled = false;

    REQUIRE_NOTHROW(psm.removePlayerSession(500, [&](bool success) {
        callbackCalled = true;
        (void)success;
    }));

    // Stub: callback is not invoked
    (void)callbackCalled;
}

// ============================================================================
// Stub behavior - updatePlayerPosition
// ============================================================================

TEST_CASE("PlayerSessionManager updatePlayerPosition with null callback does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    Position pos{};
    pos.x = 500;
    pos.y = 0;
    pos.z = 300;
    pos.timestamp_ms = 2000;

    REQUIRE_NOTHROW(psm.updatePlayerPosition(600, pos, 2000));
}

TEST_CASE("PlayerSessionManager updatePlayerPosition with callback does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    Position pos{};
    pos.x = -100;
    pos.y = 50;
    pos.z = 750;
    pos.timestamp_ms = 3000;

    bool callbackCalled = false;

    REQUIRE_NOTHROW(psm.updatePlayerPosition(700, pos, 3000, [&](bool success) {
        callbackCalled = true;
        (void)success;
    }));

    // Stub: callback is not invoked
    (void)callbackCalled;
}

// ============================================================================
// Multiple operations don't interfere
// ============================================================================

TEST_CASE("PlayerSessionManager multiple operations on different players",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    auto session1 = makeSession(1001, 1, 10, 100);
    auto session2 = makeSession(1002, 2, 20, 80);
    auto session3 = makeSession(1003, 1, 30, 50);

    // Save multiple sessions - should not crash
    REQUIRE_NOTHROW(psm.savePlayerSession(session1));
    REQUIRE_NOTHROW(psm.savePlayerSession(session2));
    REQUIRE_NOTHROW(psm.savePlayerSession(session3));

    // Load each - should not crash
    REQUIRE_NOTHROW(psm.loadPlayerSession(1001, [](const AsyncResult<PlayerSession>&) {}));
    REQUIRE_NOTHROW(psm.loadPlayerSession(1002, [](const AsyncResult<PlayerSession>&) {}));
    REQUIRE_NOTHROW(psm.loadPlayerSession(1003, [](const AsyncResult<PlayerSession>&) {}));

    // Update positions - should not crash
    Position pos{};
    pos.x = 10;
    pos.z = 20;
    REQUIRE_NOTHROW(psm.updatePlayerPosition(1001, pos, 1000));
    REQUIRE_NOTHROW(psm.updatePlayerPosition(1002, pos, 1000));
    REQUIRE_NOTHROW(psm.updatePlayerPosition(1003, pos, 1000));

    // Remove sessions - should not crash
    REQUIRE_NOTHROW(psm.removePlayerSession(1001));
    REQUIRE_NOTHROW(psm.removePlayerSession(1002));
    REQUIRE_NOTHROW(psm.removePlayerSession(1003));
}

// ============================================================================
// Session with negative health and edge-case values
// ============================================================================

TEST_CASE("PlayerSessionManager handles edge-case session values",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    // Session with max values
    PlayerSession maxSession{};
    maxSession.playerId = UINT64_MAX;
    maxSession.zoneId = UINT32_MAX;
    maxSession.connectionId = UINT32_MAX;
    maxSession.health = INT32_MAX;
    maxSession.lastActivity = UINT64_MAX;
    maxSession.position.x = INT32_MIN;
    maxSession.position.y = INT32_MAX;
    maxSession.position.z = 0;
    maxSession.position.timestamp_ms = UINT32_MAX;
    std::strncpy(maxSession.username, "EdgeCase", sizeof(maxSession.username) - 1);

    REQUIRE_NOTHROW(psm.savePlayerSession(maxSession));
    REQUIRE_NOTHROW(psm.loadPlayerSession(UINT64_MAX, [](const AsyncResult<PlayerSession>&) {}));
    REQUIRE_NOTHROW(psm.removePlayerSession(UINT64_MAX));

    // Session with zero values
    PlayerSession zeroSession{};
    REQUIRE_NOTHROW(psm.savePlayerSession(zeroSession));
    REQUIRE_NOTHROW(psm.removePlayerSession(0));
}

// ============================================================================
// Session CRUD workflow
// ============================================================================

TEST_CASE("PlayerSessionManager CRUD workflow does not crash",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    const uint64_t playerId = 42;

    // Create
    auto session = makeSession(playerId, 3, 7, 100);
    REQUIRE_NOTHROW(psm.savePlayerSession(session));

    // Read
    REQUIRE_NOTHROW(psm.loadPlayerSession(playerId, [](const AsyncResult<PlayerSession>&) {}));

    // Update position
    Position newPos{};
    newPos.x = 500;
    newPos.y = 10;
    newPos.z = -200;
    newPos.timestamp_ms = 5000;
    REQUIRE_NOTHROW(psm.updatePlayerPosition(playerId, newPos, 5000));

    // Delete
    REQUIRE_NOTHROW(psm.removePlayerSession(playerId));
}

// ============================================================================
// Position update with various coordinates
// ============================================================================

TEST_CASE("PlayerSessionManager position update with various coordinates",
          "[foundation][database]") {
    RedisManager redis;
    PlayerSessionManager psm(redis);

    // Origin
    Position origin{};
    REQUIRE_NOTHROW(psm.updatePlayerPosition(1, origin, 0));

    // Negative coordinates
    Position negative{};
    negative.x = -5000;
    negative.y = -100;
    negative.z = -3000;
    negative.timestamp_ms = 100;
    REQUIRE_NOTHROW(psm.updatePlayerPosition(2, negative, 100));

    // Large coordinates (near world bounds)
    Position large{};
    large.x = 4999;
    large.y = 99;
    large.z = 4999;
    large.timestamp_ms = 200;
    REQUIRE_NOTHROW(psm.updatePlayerPosition(3, large, 200));
}
