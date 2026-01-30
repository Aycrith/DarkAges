// [COMBAT_AGENT] Unit tests for lag compensation system

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "combat/PositionHistory.hpp"
#include <entt/entt.hpp>
#include <cstdint>
#include <glm/glm.hpp>

using namespace DarkAges;

TEST_CASE("Position History basic operations", "[combat][lag]") {
    PositionHistory history;
    
    SECTION("Record and retrieve exact timestamp") {
        for (uint32_t i = 0; i < 10; i++) {
            Position pos = Position::fromVec3(glm::vec3(i * 1.0f, 0.0f, 0.0f), i * 16);
            Velocity vel{100, 0, 0};
            Rotation rot{0, 0};
            history.record(i * 16, pos, vel, rot);
        }
        
        PositionHistoryEntry entry;
        REQUIRE(history.getPositionAtTime(80, entry));  // 5th entry
        
        auto pos = entry.position.toVec3();
        REQUIRE(pos.x == Catch::Approx(5.0f));
    }
    
    SECTION("Interpolation between timestamps") {
        // Record at t=0 and t=100
        history.record(0, Position::fromVec3(glm::vec3(0, 0, 0), 0), 
                      Velocity{0, 0, 0}, Rotation{0, 0});
        history.record(100, Position::fromVec3(glm::vec3(10, 0, 0), 100),
                      Velocity{100, 0, 0}, Rotation{0, 0});
        
        // Query at t=50 (should be at x=5)
        PositionHistoryEntry entry;
        REQUIRE(history.getInterpolatedPosition(50, entry));
        
        auto pos = entry.position.toVec3();
        REQUIRE(pos.x == Catch::Approx(5.0f).margin(0.1f));
    }
    
    SECTION("Old entries are purged after 2 seconds") {
        // Record 200 entries (more than 120 limit)
        for (uint32_t i = 0; i < 200; i++) {
            Position pos = Position::fromVec3(glm::vec3(0, 0, 0), i * 16);
            history.record(i * 16, pos, Velocity{0, 0, 0}, Rotation{0, 0});
        }
        
        // Should only keep last 120
        REQUIRE(history.size() <= 120);
    }
    
    SECTION("isTimeInHistory returns correct results") {
        // Record entries from t=100 to t=200
        for (uint32_t i = 5; i < 15; i++) {
            Position pos = Position::fromVec3(glm::vec3(0, 0, 0), i * 20);
            history.record(i * 20, pos, Velocity{0, 0, 0}, Rotation{0, 0});
        }
        
        REQUIRE(history.isTimeInHistory(120));   // Inside range
        REQUIRE(history.isTimeInHistory(280));   // Inside range
        REQUIRE_FALSE(history.isTimeInHistory(50));   // Before range
        REQUIRE_FALSE(history.isTimeInHistory(350));  // After range
    }
    
    SECTION("Clear removes all entries") {
        for (uint32_t i = 0; i < 10; i++) {
            Position pos = Position::fromVec3(glm::vec3(0, 0, 0), i * 16);
            history.record(i * 16, pos, Velocity{0, 0, 0}, Rotation{0, 0});
        }
        
        REQUIRE(history.size() == 10);
        history.clear();
        REQUIRE(history.size() == 0);
        
        PositionHistoryEntry entry;
        REQUIRE_FALSE(history.getPositionAtTime(0, entry));
    }
    
    SECTION("Oldest entry age calculation") {
        for (uint32_t i = 0; i < 10; i++) {
            Position pos = Position::fromVec3(glm::vec3(0, 0, 0), i * 16);
            history.record(i * 16, pos, Velocity{0, 0, 0}, Rotation{0, 0});
        }
        
        // Current time is 200, oldest entry is at 0
        uint32_t age = history.getOldestEntryAge(200);
        REQUIRE(age == 200);
    }
    
    SECTION("Velocity interpolation") {
        // Record at t=0 and t=100 with different velocities
        history.record(0, Position::fromVec3(glm::vec3(0, 0, 0), 0), 
                      Velocity{0, 0, 0}, Rotation{0, 0});
        history.record(100, Position::fromVec3(glm::vec3(10, 0, 0), 100),
                      Velocity{200, 0, 0}, Rotation{0, 0});
        
        // Query at t=50 (velocity should be 100)
        PositionHistoryEntry entry;
        REQUIRE(history.getInterpolatedPosition(50, entry));
        
        REQUIRE(entry.velocity.dx == Catch::Approx(100).margin(1));
    }
}

TEST_CASE("LagCompensator hit validation", "[combat][lag]") {
    LagCompensator compensator;
    entt::registry registry;
    
    // Create attacker and target entities
    EntityID attacker = registry.create();
    EntityID target = registry.create();
    
    // Add components to target so it can be processed
    registry.emplace<Position>(target);
    registry.emplace<Velocity>(target);
    registry.emplace<Rotation>(target);
    
    // Record target positions over time (target moving along X axis)
    for (uint32_t t = 0; t < 1000; t += 16) {
        Position pos = Position::fromVec3(glm::vec3(t / 100.0f, 0, 0), t);
        compensator.recordPosition(target, t, pos, Velocity{100, 0, 0}, Rotation{0, 0});
    }
    
    SECTION("Valid hit within tolerance") {
        // At t=500, target was at x=5.0
        Position claimedHit = Position::fromVec3(glm::vec3(5.0f, 0, 0), 500);
        
        // 100ms latency
        bool valid = compensator.validateHit(attacker, target, 450, 100, 
                                            claimedHit, 0.5f);
        REQUIRE(valid);
    }
    
    SECTION("Invalid hit - too far from historical position") {
        // Claim hit at x=10.0, but target was at x=5.0
        Position claimedHit = Position::fromVec3(glm::vec3(10.0f, 0, 0), 500);
        
        bool valid = compensator.validateHit(attacker, target, 450, 100,
                                            claimedHit, 0.5f);
        REQUIRE_FALSE(valid);
    }
    
    SECTION("Rewind limited to MAX_REWIND_MS") {
        // Try to rewind 1000ms, but max is 500ms
        Position claimedHit = Position::fromVec3(glm::vec3(5.0f, 0, 0), 500);
        
        bool valid = compensator.validateHit(attacker, target, 0, 2000,
                                            claimedHit, 0.5f);
        // Should still work, but with limited rewind
        REQUIRE(valid);
    }
    
    SECTION("Hit validation with interpolation") {
        // At t=500, target was at x=5.0 (recorded)
        // Test with odd timestamp that requires interpolation
        Position claimedHit = Position::fromVec3(glm::vec3(5.08f, 0, 0), 508);
        
        bool valid = compensator.validateHit(attacker, target, 458, 100,
                                            claimedHit, 0.5f);
        REQUIRE(valid);
    }
    
    SECTION("Remove entity clears history") {
        PositionHistoryEntry entry;
        
        // Verify we can get the position
        REQUIRE(compensator.getHistoricalPosition(target, 500, entry));
        
        // Remove entity
        compensator.removeEntity(target);
        
        // Should not find position anymore
        REQUIRE_FALSE(compensator.getHistoricalPosition(target, 500, entry));
    }
    
    SECTION("Update all entities from registry") {
        // Add more entities
        for (int i = 0; i < 5; i++) {
            EntityID e = registry.create();
            registry.emplace<Position>(e, Position::fromVec3(glm::vec3(i, 0, 0), 0));
            registry.emplace<Velocity>(e, Velocity{0, 0, 0});
            registry.emplace<Rotation>(e, Rotation{0, 0});
        }
        
        // Update all at once
        compensator.updateAllEntities(registry, 1000);
        
        // Verify all entities have history
        uint32_t count = 0;
        auto view = registry.view<Position, Velocity, Rotation>();
        view.each([&](EntityID entity, const Position&, const Velocity&, const Rotation&) {
            PositionHistoryEntry entry;
            if (compensator.getHistoricalPosition(entity, 1000, entry)) {
                count++;
            }
        });
        
        // Should include the 5 new entities plus the original target
        REQUIRE(count == 6);
    }
    
    SECTION("Hit with tolerance accounting") {
        // Position at t=500 is x=5.0
        // With 0.5m hit radius and 1m tolerance = 1.5m total tolerance
        // 1.4m should be valid
        Position claimedHit = Position::fromVec3(glm::vec3(6.4f, 0, 0), 500);
        
        bool valid = compensator.validateHit(attacker, target, 450, 100,
                                            claimedHit, 0.5f);
        REQUIRE(valid);
        
        // 1.6m should be invalid
        Position claimedHit2 = Position::fromVec3(glm::vec3(6.6f, 0, 0), 500);
        bool valid2 = compensator.validateHit(attacker, target, 450, 100,
                                             claimedHit2, 0.5f);
        REQUIRE_FALSE(valid2);
    }
    
    SECTION("Hit outside history window rejected") {
        // Try to validate hit at t=2000, but history only goes to t=1000
        Position claimedHit = Position::fromVec3(glm::vec3(0, 0, 0), 2000);
        
        bool valid = compensator.validateHit(attacker, target, 1950, 100,
                                            claimedHit, 0.5f);
        REQUIRE_FALSE(valid);
    }
    
    SECTION("Max rewind constant is 500ms") {
        REQUIRE(LagCompensator::getMaxRewindMs() == 500);
    }
}

TEST_CASE("LagCompensator edge cases", "[combat][lag]") {
    LagCompensator compensator;
    entt::registry registry;
    
    EntityID target = registry.create();
    registry.emplace<Position>(target);
    registry.emplace<Velocity>(target);
    registry.emplace<Rotation>(target);
    
    SECTION("Empty history returns false") {
        PositionHistoryEntry entry;
        bool found = compensator.getHistoricalPosition(target, 100, entry);
        REQUIRE_FALSE(found);
    }
    
    SECTION("Single entry interpolation fails") {
        compensator.recordPosition(target, 100, 
            Position::fromVec3(glm::vec3(5, 0, 0), 100),
            Velocity{0, 0, 0}, Rotation{0, 0});
        
        // Try to get position at time between entries (but only one exists)
        PositionHistoryEntry entry;
        // Exact timestamp should work
        REQUIRE(compensator.getHistoricalPosition(target, 100, entry));
        // Non-existent should fail (can't interpolate with only one point)
        REQUIRE_FALSE(compensator.getHistoricalPosition(target, 150, entry));
    }
}
