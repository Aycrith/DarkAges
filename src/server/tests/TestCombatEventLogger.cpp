// CombatEventLogger unit tests - covers combat event logging and player stats
// Tests stub behavior when ScyllaDB is not available

#include <catch2/catch_test_macros.hpp>
#include "db/CombatEventLogger.hpp"
#include "db/ScyllaManager.hpp"  // For CombatEvent, PlayerCombatStats structs

using namespace DarkAges;

// Helper to create a test CombatEvent
static CombatEvent makeCombatEvent(uint64_t eventId, uint64_t attackerId,
                                    uint64_t targetId, const char* eventType = "damage") {
    CombatEvent event{};
    event.eventId = eventId;
    event.timestamp = 1000;
    event.zoneId = 1;
    event.attackerId = attackerId;
    event.targetId = targetId;
    event.eventType = eventType;
    event.damageAmount = 100;
    event.isCritical = false;
    event.weaponType = "sword";
    event.position.x = 100;
    event.position.y = 0;
    event.position.z = 200;
    event.position.timestamp_ms = 1000;
    event.serverTick = 1;
    return event;
}

// Helper to create a test PlayerCombatStats
static PlayerCombatStats makePlayerStats(uint64_t playerId, uint32_t sessionDate) {
    PlayerCombatStats stats{};
    stats.playerId = playerId;
    stats.sessionDate = sessionDate;
    stats.kills = 5;
    stats.deaths = 2;
    stats.damageDealt = 10000;
    stats.damageTaken = 5000;
    stats.longestKillStreak = 10;
    stats.currentKillStreak = 3;
    return stats;
}

// ============================================================================
// CombatEventLogger construction
// ============================================================================

TEST_CASE("CombatEventLogger construction", "[foundation][database]") {
    CombatEventLogger logger;

    // Construction should not crash
    REQUIRE(true);
}

TEST_CASE("CombatEventLogger destruction", "[foundation][database]") {
    {
        CombatEventLogger logger;
        // Do some operations
        (void)logger.isReady();
    }
    // Destruction should not crash
    REQUIRE(true);
}

// ============================================================================
// prepareStatements - stub returns true
// ============================================================================

TEST_CASE("CombatEventLogger prepareStatements returns true", "[foundation][database]") {
    CombatEventLogger logger;

    // Stub always returns true (no actual database connection)
    bool result = logger.prepareStatements(nullptr);

    REQUIRE(result == true);
}

// ============================================================================
// isReady - stub returns true
// ============================================================================

TEST_CASE("CombatEventLogger isReady returns true", "[foundation][database]") {
    CombatEventLogger logger;

    // Stub always returns true when no real DB is available
    bool ready = logger.isReady();

    REQUIRE(ready == true);
}

// ============================================================================
// Metrics getters - stub returns 0
// ============================================================================

TEST_CASE("CombatEventLogger metrics start at zero", "[foundation][database]") {
    CombatEventLogger logger;

    REQUIRE(logger.getWritesQueued() == 0);
    REQUIRE(logger.getWritesCompleted() == 0);
    REQUIRE(logger.getWritesFailed() == 0);
}

TEST_CASE("CombatEventLogger metrics remain zero after operations", "[foundation][database]") {
    CombatEventLogger logger;

    // Perform various operations - metrics should remain at 0 in stub mode
    logger.logCombatEvent(nullptr, makeCombatEvent(1, 100, 200));

    std::vector<CombatEvent> events;
    events.push_back(makeCombatEvent(2, 100, 200));
    events.push_back(makeCombatEvent(3, 100, 201));
    logger.logCombatEventsBatch(nullptr, events);

    logger.updatePlayerStats(nullptr, makePlayerStats(100, 20260418));

    logger.savePlayerState(nullptr, 100, 1, 1000);

    // Metrics should still be 0 in stub mode
    REQUIRE(logger.getWritesQueued() == 0);
    REQUIRE(logger.getWritesCompleted() == 0);
    REQUIRE(logger.getWritesFailed() == 0);
}

// ============================================================================
// logCombatEvent stub behavior
// ============================================================================

TEST_CASE("CombatEventLogger logCombatEvent with null callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;
    auto event = makeCombatEvent(1, 100, 200, "damage");

    REQUIRE_NOTHROW(logger.logCombatEvent(nullptr, event, nullptr));
}

TEST_CASE("CombatEventLogger logCombatEvent with callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;
    auto event = makeCombatEvent(2, 101, 201, "kill");

    bool callbackCalled = false;

    REQUIRE_NOTHROW(logger.logCombatEvent(nullptr, event, [&](bool success) {
        callbackCalled = true;
        (void)success;
    }));

    // Stub: callback is not invoked (no-op behavior)
    // Real connected: callback fires with success=true
    // Just verify no crash occurred
    (void)callbackCalled;
}

TEST_CASE("CombatEventLogger logCombatEvent various event types", "[foundation][database]") {
    CombatEventLogger logger;

    // Test different event types
    auto damageEvent = makeCombatEvent(1, 100, 200, "damage");
    auto healEvent = makeCombatEvent(2, 100, 200, "heal");
    auto killEvent = makeCombatEvent(3, 100, 200, "kill");
    auto deathEvent = makeCombatEvent(4, 100, 200, "death");
    auto assistEvent = makeCombatEvent(5, 100, 200, "assist");

    REQUIRE_NOTHROW(logger.logCombatEvent(nullptr, damageEvent));
    REQUIRE_NOTHROW(logger.logCombatEvent(nullptr, healEvent));
    REQUIRE_NOTHROW(logger.logCombatEvent(nullptr, killEvent));
    REQUIRE_NOTHROW(logger.logCombatEvent(nullptr, deathEvent));
    REQUIRE_NOTHROW(logger.logCombatEvent(nullptr, assistEvent));
}

// ============================================================================
// logCombatEventsBatch stub behavior
// ============================================================================

TEST_CASE("CombatEventLogger logCombatEventsBatch with null callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;

    std::vector<CombatEvent> events;
    events.push_back(makeCombatEvent(1, 100, 200));
    events.push_back(makeCombatEvent(2, 101, 201));
    events.push_back(makeCombatEvent(3, 102, 202));

    REQUIRE_NOTHROW(logger.logCombatEventsBatch(nullptr, events, nullptr));
}

TEST_CASE("CombatEventLogger logCombatEventsBatch with callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;

    std::vector<CombatEvent> events;
    events.push_back(makeCombatEvent(4, 100, 200));

    bool callbackCalled = false;

    REQUIRE_NOTHROW(logger.logCombatEventsBatch(nullptr, events, [&](bool success) {
        callbackCalled = true;
        (void)success;
    }));

    // Stub: callback is not invoked
    (void)callbackCalled;
}

TEST_CASE("CombatEventLogger logCombatEventsBatch empty vector", "[foundation][database]") {
    CombatEventLogger logger;

    std::vector<CombatEvent> emptyEvents;

    REQUIRE_NOTHROW(logger.logCombatEventsBatch(nullptr, emptyEvents));
}

TEST_CASE("CombatEventLogger logCombatEventsBatch large batch", "[foundation][database]") {
    CombatEventLogger logger;

    std::vector<CombatEvent> largeBatch;
    for (uint64_t i = 0; i < 100; ++i) {
        largeBatch.push_back(makeCombatEvent(i, 100 + (i % 10), 200 + (i % 5)));
    }

    REQUIRE_NOTHROW(logger.logCombatEventsBatch(nullptr, largeBatch));
}

// ============================================================================
// updatePlayerStats stub behavior
// ============================================================================

TEST_CASE("CombatEventLogger updatePlayerStats with null callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;
    auto stats = makePlayerStats(100, 20260418);

    REQUIRE_NOTHROW(logger.updatePlayerStats(nullptr, stats, nullptr));
}

TEST_CASE("CombatEventLogger updatePlayerStats with callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;
    auto stats = makePlayerStats(101, 20260418);

    bool callbackCalled = false;

    REQUIRE_NOTHROW(logger.updatePlayerStats(nullptr, stats, [&](bool success) {
        callbackCalled = true;
        (void)success;
    }));

    // Stub: callback is not invoked
    (void)callbackCalled;
}

TEST_CASE("CombatEventLogger updatePlayerStats edge case values", "[foundation][database]") {
    CombatEventLogger logger;

    // Stats with max values
    PlayerCombatStats maxStats{};
    maxStats.playerId = UINT64_MAX;
    maxStats.sessionDate = 99991231;
    maxStats.kills = UINT32_MAX;
    maxStats.deaths = UINT32_MAX;
    maxStats.damageDealt = UINT64_MAX;
    maxStats.damageTaken = UINT64_MAX;
    maxStats.longestKillStreak = UINT32_MAX;
    maxStats.currentKillStreak = UINT32_MAX;

    REQUIRE_NOTHROW(logger.updatePlayerStats(nullptr, maxStats));

    // Stats with zero values
    PlayerCombatStats zeroStats{};
    REQUIRE_NOTHROW(logger.updatePlayerStats(nullptr, zeroStats));
}

// ============================================================================
// getPlayerStats stub behavior
// ============================================================================

TEST_CASE("CombatEventLogger getPlayerStats with callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;

    bool callbackCalled = false;
    bool successValue = true;
    PlayerCombatStats statsValue{};

    REQUIRE_NOTHROW(logger.getPlayerStats(nullptr, 100, 20260418,
        [&](bool success, const PlayerCombatStats& stats) {
            callbackCalled = true;
            successValue = success;
            statsValue = stats;
        }));

    // Stub: callback is not invoked
    // Real connected: callback fires with success=true and actual stats
    // Stub: callback never fires
    (void)callbackCalled;
    (void)successValue;
    (void)statsValue;
}

TEST_CASE("CombatEventLogger getPlayerStats various player IDs", "[foundation][database]") {
    CombatEventLogger logger;

    // Test various player IDs
    REQUIRE_NOTHROW(logger.getPlayerStats(nullptr, 0, 20260418,
        [](bool, const PlayerCombatStats&) {}));

    REQUIRE_NOTHROW(logger.getPlayerStats(nullptr, 1, 20260418,
        [](bool, const PlayerCombatStats&) {}));

    REQUIRE_NOTHROW(logger.getPlayerStats(nullptr, UINT64_MAX, 20260418,
        [](bool, const PlayerCombatStats&) {}));
}

// ============================================================================
// savePlayerState stub behavior
// ============================================================================

TEST_CASE("CombatEventLogger savePlayerState with null callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;

    REQUIRE_NOTHROW(logger.savePlayerState(nullptr, 100, 1, 1000, nullptr));
}

TEST_CASE("CombatEventLogger savePlayerState with callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;

    bool callbackCalled = false;

    REQUIRE_NOTHROW(logger.savePlayerState(nullptr, 101, 2, 2000, [&](bool success) {
        callbackCalled = true;
        (void)success;
    }));

    // Stub: callback is not invoked
    (void)callbackCalled;
}

TEST_CASE("CombatEventLogger savePlayerState various parameters", "[foundation][database]") {
    CombatEventLogger logger;

    // Test various zone IDs and timestamps
    REQUIRE_NOTHROW(logger.savePlayerState(nullptr, 100, 0, 0));
    REQUIRE_NOTHROW(logger.savePlayerState(nullptr, 100, UINT32_MAX, UINT64_MAX));
    REQUIRE_NOTHROW(logger.savePlayerState(nullptr, UINT64_MAX, 1, 1000));
}

// ============================================================================
// getTopKillers stub behavior
// ============================================================================

TEST_CASE("CombatEventLogger getTopKillers with callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;

    bool callbackCalled = false;
    bool successValue = true;
    std::vector<std::pair<uint64_t, uint32_t>> killersValue;

    REQUIRE_NOTHROW(logger.getTopKillers(nullptr, 1, 1000, 2000, 10,
        [&](bool success, const std::vector<std::pair<uint64_t, uint32_t>>& killers) {
            callbackCalled = true;
            successValue = success;
            killersValue = killers;
        }));

    // Stub: callback is not invoked
    // Real connected: callback fires with success=true and actual killers
    // Stub: callback never fires (no query happens)
    (void)callbackCalled;
    (void)successValue;
    (void)killersValue;
}

TEST_CASE("CombatEventLogger getTopKillers various parameters", "[foundation][database]") {
    CombatEventLogger logger;

    // Test various zone IDs and limits
    REQUIRE_NOTHROW(logger.getTopKillers(nullptr, 0, 0, 0, 0,
        [](bool, const std::vector<std::pair<uint64_t, uint32_t>>&) {}));

    REQUIRE_NOTHROW(logger.getTopKillers(nullptr, 1, 1000, 2000, 1,
        [](bool, const std::vector<std::pair<uint64_t, uint32_t>>&) {}));

    REQUIRE_NOTHROW(logger.getTopKillers(nullptr, UINT32_MAX, 0, UINT32_MAX, 100,
        [](bool, const std::vector<std::pair<uint64_t, uint32_t>>&) {}));
}

// ============================================================================
// getKillFeed stub behavior
// ============================================================================

TEST_CASE("CombatEventLogger getKillFeed with callback does not crash",
          "[foundation][database]") {
    CombatEventLogger logger;

    bool callbackCalled = false;
    bool successValue = true;
    std::vector<CombatEvent> feedValue;

    REQUIRE_NOTHROW(logger.getKillFeed(nullptr, 1, 50,
        [&](bool success, const std::vector<CombatEvent>& feed) {
            callbackCalled = true;
            successValue = success;
            feedValue = feed;
        }));

    // Stub: callback is not invoked
    // Real connected: callback fires with success=true and actual feed
    // Stub: callback never fires (no query happens)
    (void)callbackCalled;
    (void)successValue;
    (void)feedValue;
}

TEST_CASE("CombatEventLogger getKillFeed various parameters", "[foundation][database]") {
    CombatEventLogger logger;

    // Test various zone IDs and limits
    REQUIRE_NOTHROW(logger.getKillFeed(nullptr, 0, 0,
        [](bool, const std::vector<CombatEvent>&) {}));

    REQUIRE_NOTHROW(logger.getKillFeed(nullptr, 1, 1,
        [](bool, const std::vector<CombatEvent>&) {}));

    REQUIRE_NOTHROW(logger.getKillFeed(nullptr, UINT32_MAX, 1000,
        [](bool, const std::vector<CombatEvent>&) {}));
}

// ============================================================================
// Multiple operations don't interfere
// ============================================================================

TEST_CASE("CombatEventLogger multiple operations on different players",
          "[foundation][database]") {
    CombatEventLogger logger;

    // Mix of different operations on different players
    logger.logCombatEvent(nullptr, makeCombatEvent(1, 100, 200));
    logger.updatePlayerStats(nullptr, makePlayerStats(100, 20260418));
    logger.savePlayerState(nullptr, 100, 1, 1000);

    logger.logCombatEvent(nullptr, makeCombatEvent(2, 101, 201));
    logger.updatePlayerStats(nullptr, makePlayerStats(101, 20260418));
    logger.savePlayerState(nullptr, 101, 2, 2000);

    logger.logCombatEvent(nullptr, makeCombatEvent(3, 102, 202));
    logger.updatePlayerStats(nullptr, makePlayerStats(102, 20260418));
    logger.savePlayerState(nullptr, 102, 1, 3000);

    // Verify metrics are still 0
    REQUIRE(logger.getWritesQueued() == 0);
    REQUIRE(logger.getWritesCompleted() == 0);
    REQUIRE(logger.getWritesFailed() == 0);
}

// ============================================================================
// cleanupPreparedStatements
// ============================================================================

TEST_CASE("CombatEventLogger cleanupPreparedStatements does not crash", "[foundation][database]") {
    CombatEventLogger logger;

    // Prepare statements first
    logger.prepareStatements(nullptr);

    // Then cleanup - should not crash
    REQUIRE_NOTHROW(logger.cleanupPreparedStatements());
}

TEST_CASE("CombatEventLogger cleanupPreparedStatements multiple times", "[foundation][database]") {
    CombatEventLogger logger;

    // Multiple cleanups should not crash
    REQUIRE_NOTHROW(logger.cleanupPreparedStatements());
    REQUIRE_NOTHROW(logger.cleanupPreparedStatements());
    REQUIRE_NOTHROW(logger.cleanupPreparedStatements());
}

// ============================================================================
// Stress test - many operations
// ============================================================================

TEST_CASE("CombatEventLogger handles many operations", "[foundation][database]") {
    CombatEventLogger logger;

    // Perform many operations
    for (uint64_t i = 0; i < 1000; ++i) {
        logger.logCombatEvent(nullptr, makeCombatEvent(i, 100, 200));

        std::vector<CombatEvent> batch;
        for (uint32_t j = 0; j < 10; ++j) {
            batch.push_back(makeCombatEvent(i * 10 + j, 100, 200));
        }
        logger.logCombatEventsBatch(nullptr, batch);

        logger.updatePlayerStats(nullptr, makePlayerStats(100 + (i % 10), 20260418));
    }

    // Metrics should still be 0
    REQUIRE(logger.getWritesQueued() == 0);
    REQUIRE(logger.getWritesCompleted() == 0);
    REQUIRE(logger.getWritesFailed() == 0);
}