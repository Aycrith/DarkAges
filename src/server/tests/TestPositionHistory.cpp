#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "combat/PositionHistory.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"

using namespace DarkAges;

namespace {
Position makePos(float x, float y, float z) {
    Position p;
    p.x = static_cast<Constants::Fixed>(x * Constants::FLOAT_TO_FIXED);
    p.y = static_cast<Constants::Fixed>(y * Constants::FLOAT_TO_FIXED);
    p.z = static_cast<Constants::Fixed>(z * Constants::FLOAT_TO_FIXED);
    return p;
}

Velocity makeVel(float dx, float dy, float dz) {
    Velocity v;
    v.dx = static_cast<Constants::Fixed>(dx * Constants::FLOAT_TO_FIXED);
    v.dy = static_cast<Constants::Fixed>(dy * Constants::FLOAT_TO_FIXED);
    v.dz = static_cast<Constants::Fixed>(dz * Constants::FLOAT_TO_FIXED);
    return v;
}

Rotation makeRot(float yaw, float pitch) {
    Rotation r;
    r.yaw = yaw;
    r.pitch = pitch;
    return r;
}
}  // namespace

// ============================================================================
// PositionHistory basics
// ============================================================================

TEST_CASE("[position-history] New history is empty", "[combat]") {
    PositionHistory history;
    REQUIRE(history.size() == 0);
}

TEST_CASE("[position-history] Record adds entries", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(10, 0, 20), makeVel(1, 0, 0), makeRot(0, 0));
    REQUIRE(history.size() == 1);

    history.record(2000, makePos(20, 0, 30), makeVel(1, 0, 0), makeRot(0, 0));
    REQUIRE(history.size() == 2);
}

TEST_CASE("[position-history] Clear removes all entries", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(10, 0, 20), makeVel(1, 0, 0), makeRot(0, 0));
    history.record(2000, makePos(20, 0, 30), makeVel(1, 0, 0), makeRot(0, 0));
    REQUIRE(history.size() == 2);

    history.clear();
    REQUIRE(history.size() == 0);
}

// ============================================================================
// getPositionAtTime
// ============================================================================

TEST_CASE("[position-history] Exact timestamp lookup", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(10, 0, 20), makeVel(1, 0, 0), makeRot(0, 0));
    history.record(2000, makePos(20, 0, 30), makeVel(1, 0, 0), makeRot(1.0f, 0));

    PositionHistoryEntry entry;
    REQUIRE(history.getPositionAtTime(1000, entry));
    REQUIRE(entry.position.x == 10 * Constants::FIXED_PRECISION);
    REQUIRE(entry.timestamp == 1000);

    REQUIRE(history.getPositionAtTime(2000, entry));
    REQUIRE(entry.position.x == 20 * Constants::FIXED_PRECISION);
}

TEST_CASE("[position-history] Empty history returns false", "[combat]") {
    PositionHistory history;
    PositionHistoryEntry entry;
    REQUIRE_FALSE(history.getPositionAtTime(1000, entry));
}

TEST_CASE("[position-history] Single entry requires exact match", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(10, 0, 20), makeVel(1, 0, 0), makeRot(0, 0));

    PositionHistoryEntry entry;
    REQUIRE(history.getPositionAtTime(1000, entry));
    // Interpolation requires 2+ entries
    REQUIRE_FALSE(history.getPositionAtTime(1500, entry));
}

// ============================================================================
// Interpolation
// ============================================================================

TEST_CASE("[position-history] Interpolates position between entries", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(0, 0, 0), makeVel(0, 0, 0), makeRot(0, 0));
    history.record(2000, makePos(100, 0, 0), makeVel(0, 0, 0), makeRot(0, 0));

    PositionHistoryEntry entry;
    // Midpoint
    REQUIRE(history.getInterpolatedPosition(1500, entry));
    REQUIRE(entry.position.x == 50 * Constants::FIXED_PRECISION);

    // Quarter point
    REQUIRE(history.getInterpolatedPosition(1250, entry));
    REQUIRE(entry.position.x == 25 * Constants::FIXED_PRECISION);

    // Three-quarter point
    REQUIRE(history.getInterpolatedPosition(1750, entry));
    REQUIRE(entry.position.x == 75 * Constants::FIXED_PRECISION);
}

TEST_CASE("[position-history] Interpolates velocity", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(0, 0, 0), makeVel(10, 0, 0), makeRot(0, 0));
    history.record(2000, makePos(0, 0, 0), makeVel(20, 0, 0), makeRot(0, 0));

    PositionHistoryEntry entry;
    REQUIRE(history.getInterpolatedPosition(1500, entry));
    REQUIRE(entry.velocity.dx == 15 * Constants::FIXED_PRECISION);
}

TEST_CASE("[position-history] Interpolation with < 2 entries returns false", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(0, 0, 0), makeVel(0, 0, 0), makeRot(0, 0));

    PositionHistoryEntry entry;
    REQUIRE_FALSE(history.getInterpolatedPosition(1500, entry));
}

// ============================================================================
// isTimeInHistory
// ============================================================================

TEST_CASE("[position-history] isTimeInHistory checks window", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(0, 0, 0), makeVel(0, 0, 0), makeRot(0, 0));
    history.record(3000, makePos(100, 0, 0), makeVel(0, 0, 0), makeRot(0, 0));

    REQUIRE(history.isTimeInHistory(1000));
    REQUIRE(history.isTimeInHistory(2000));
    REQUIRE(history.isTimeInHistory(3000));
    REQUIRE_FALSE(history.isTimeInHistory(500));
    REQUIRE_FALSE(history.isTimeInHistory(4000));
}

TEST_CASE("[position-history] Empty history returns false for isTimeInHistory", "[combat]") {
    PositionHistory history;
    REQUIRE_FALSE(history.isTimeInHistory(1000));
}

// ============================================================================
// getOldestEntryAge
// ============================================================================

TEST_CASE("[position-history] getOldestEntryAge returns correct age", "[combat]") {
    PositionHistory history;
    history.record(1000, makePos(0, 0, 0), makeVel(0, 0, 0), makeRot(0, 0));
    history.record(2000, makePos(10, 0, 0), makeVel(0, 0, 0), makeRot(0, 0));

    REQUIRE(history.getOldestEntryAge(2500) == 1500);
}

TEST_CASE("[position-history] getOldestEntryAge returns 0 for empty", "[combat]") {
    PositionHistory history;
    REQUIRE(history.getOldestEntryAge(5000) == 0);
}

// ============================================================================
// Buffer eviction
// ============================================================================

TEST_CASE("[position-history] Evicts entries exceeding HISTORY_SIZE", "[combat]") {
    PositionHistory history;
    // Fill beyond the 120-entry limit
    for (uint32_t i = 0; i < 130; ++i) {
        history.record(i * 17, makePos(static_cast<float>(i), 0, 0), makeVel(0, 0, 0), makeRot(0, 0));
    }
    REQUIRE(history.size() <= PositionHistory::HISTORY_SIZE);
}

// ============================================================================
// LagCompensator basics
// ============================================================================

TEST_CASE("[lag-compensator] Record and retrieve historical position", "[combat]") {
    LagCompensator compensator;
    EntityID entity = static_cast<EntityID>(42);

    compensator.recordPosition(entity, 1000, makePos(10, 0, 20), makeVel(1, 0, 0), makeRot(0, 0));
    compensator.recordPosition(entity, 2000, makePos(20, 0, 30), makeVel(1, 0, 0), makeRot(0, 0));

    PositionHistoryEntry entry;
    REQUIRE(compensator.getHistoricalPosition(entity, 1000, entry));
    REQUIRE(entry.position.x == 10 * Constants::FIXED_PRECISION);
}

TEST_CASE("[lag-compensator] Unknown entity returns false", "[combat]") {
    LagCompensator compensator;
    EntityID entity = static_cast<EntityID>(99);

    PositionHistoryEntry entry;
    REQUIRE_FALSE(compensator.getHistoricalPosition(entity, 1000, entry));
}

TEST_CASE("[lag-compensator] Remove entity clears history", "[combat]") {
    LagCompensator compensator;
    EntityID entity = static_cast<EntityID>(42);

    compensator.recordPosition(entity, 1000, makePos(10, 0, 20), makeVel(1, 0, 0), makeRot(0, 0));

    PositionHistoryEntry entry;
    REQUIRE(compensator.getHistoricalPosition(entity, 1000, entry));

    compensator.removeEntity(entity);
    REQUIRE_FALSE(compensator.getHistoricalPosition(entity, 1000, entry));
}

TEST_CASE("[lag-compensator] Multiple entities tracked independently", "[combat]") {
    LagCompensator compensator;
    EntityID e1 = static_cast<EntityID>(1);
    EntityID e2 = static_cast<EntityID>(2);

    compensator.recordPosition(e1, 1000, makePos(10, 0, 0), makeVel(1, 0, 0), makeRot(0, 0));
    compensator.recordPosition(e2, 1000, makePos(50, 0, 0), makeVel(2, 0, 0), makeRot(0, 0));

    PositionHistoryEntry entry;
    REQUIRE(compensator.getHistoricalPosition(e1, 1000, entry));
    REQUIRE(entry.position.x == 10 * Constants::FIXED_PRECISION);

    REQUIRE(compensator.getHistoricalPosition(e2, 1000, entry));
    REQUIRE(entry.position.x == 50 * Constants::FIXED_PRECISION);
}

// ============================================================================
// validateHit
// ============================================================================

TEST_CASE("[lag-compensator] validateHit succeeds when target near claimed position", "[combat]") {
    LagCompensator compensator;
    EntityID target = static_cast<EntityID>(2);

    // Target at (100, 0, 100) at time 1000
    compensator.recordPosition(target, 1000, makePos(100, 0, 100), makeVel(0, 0, 0), makeRot(0, 0));

    // Attack at time 1000, claim hit at target's exact position, 0 RTT
    Position hitPos = makePos(100, 0, 100);
    REQUIRE(compensator.validateHit(
        static_cast<EntityID>(1), target,
        1000, 0, hitPos, 1.0f));
}

TEST_CASE("[lag-compensator] validateHit fails when target far from claimed position", "[combat]") {
    LagCompensator compensator;
    EntityID target = static_cast<EntityID>(2);

    compensator.recordPosition(target, 1000, makePos(100, 0, 100), makeVel(0, 0, 0), makeRot(0, 0));

    // Claim hit far from target
    Position hitPos = makePos(200, 0, 200);
    REQUIRE_FALSE(compensator.validateHit(
        static_cast<EntityID>(1), target,
        1000, 0, hitPos, 1.0f));
}

TEST_CASE("[lag-compensator] validateHit fails for unknown target", "[combat]") {
    LagCompensator compensator;
    Position hitPos = makePos(100, 0, 100);
    REQUIRE_FALSE(compensator.validateHit(
        static_cast<EntityID>(1), static_cast<EntityID>(99),
        1000, 0, hitPos, 1.0f));
}

TEST_CASE("[lag-compensator] validateHit clamps rewind to MAX_REWIND_MS", "[combat]") {
    LagCompensator compensator;
    EntityID target = static_cast<EntityID>(2);

    // Record position at time 0
    compensator.recordPosition(target, 0, makePos(100, 0, 100), makeVel(0, 0, 0), makeRot(0, 0));

    // With very high RTT (should be clamped to MAX_REWIND_MS = 500ms one-way)
    Position hitPos = makePos(100, 0, 100);
    // RTT 10000ms -> oneWay = min(5000, 500) = 500, targetTime = 500
    // Target has no entry at time 500, so getHistoricalPosition returns false
    // This is acceptable behavior - too old to validate
    REQUIRE_FALSE(compensator.validateHit(
        static_cast<EntityID>(1), target,
        0, 10000, hitPos, 1.0f));
}
