// [ZONE_AGENT] Area of Interest system unit tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "zones/AreaOfInterest.hpp"
#include "physics/SpatialHash.hpp"
#include <entt/entt.hpp>
#include <chrono>
#include <glm/glm.hpp>
#include <vector>
#include <random>

using namespace DarkAges;

TEST_CASE("Area of Interest basic visibility", "[aoi]") {
    Registry registry;
    SpatialHash spatialHash;
    AreaOfInterestSystem aoiSystem;
    
    // Create a viewer entity at position (0, 0, 0)
    EntityID viewer = registry.create();
    Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<Position>(viewer, viewerPos);
    
    SECTION("Entity within NEAR radius is visible") {
        EntityID target = registry.create();
        Position targetPos = Position::fromVec3(glm::vec3(30.0f, 0.0f, 0.0f));  // 30m away
        registry.emplace<Position>(target, targetPos);
        spatialHash.insert(target, targetPos);
        
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        REQUIRE(result.visibleEntities.size() == 1);
        REQUIRE(result.visibleEntities[0] == target);
    }
    
    SECTION("Entity at MID radius is visible") {
        EntityID target = registry.create();
        Position targetPos = Position::fromVec3(glm::vec3(75.0f, 0.0f, 0.0f));  // 75m away
        registry.emplace<Position>(target, targetPos);
        spatialHash.insert(target, targetPos);
        
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        REQUIRE(result.visibleEntities.size() == 1);
    }
    
    SECTION("Entity at FAR radius is visible") {
        EntityID target = registry.create();
        Position targetPos = Position::fromVec3(glm::vec3(150.0f, 0.0f, 0.0f));  // 150m away
        registry.emplace<Position>(target, targetPos);
        spatialHash.insert(target, targetPos);
        
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        REQUIRE(result.visibleEntities.size() == 1);
    }
    
    SECTION("Entity beyond FAR radius is not visible") {
        EntityID target = registry.create();
        Position targetPos = Position::fromVec3(glm::vec3(250.0f, 0.0f, 0.0f));  // 250m away
        registry.emplace<Position>(target, targetPos);
        spatialHash.insert(target, targetPos);
        
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        REQUIRE(result.visibleEntities.size() == 0);
    }
    
    SECTION("Viewer entity is not in own AOI") {
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        REQUIRE(result.visibleEntities.size() == 0);
    }
}

TEST_CASE("Area of Interest multiple entities", "[aoi]") {
    Registry registry;
    SpatialHash spatialHash;
    AreaOfInterestSystem aoiSystem;
    
    EntityID viewer = registry.create();
    Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<Position>(viewer, viewerPos);
    
    SECTION("Multiple entities at different distances") {
        // Create entities at various distances
        EntityID near = registry.create();
        Position nearPos = Position::fromVec3(glm::vec3(25.0f, 0.0f, 0.0f));
        registry.emplace<Position>(near, nearPos);
        spatialHash.insert(near, nearPos);
        
        EntityID mid = registry.create();
        Position midPos = Position::fromVec3(glm::vec3(80.0f, 0.0f, 0.0f));
        registry.emplace<Position>(mid, midPos);
        spatialHash.insert(mid, midPos);
        
        EntityID far = registry.create();
        Position farPos = Position::fromVec3(glm::vec3(150.0f, 0.0f, 0.0f));
        registry.emplace<Position>(far, farPos);
        spatialHash.insert(far, farPos);
        
        EntityID tooFar = registry.create();
        Position tooFarPos = Position::fromVec3(glm::vec3(300.0f, 0.0f, 0.0f));
        registry.emplace<Position>(tooFar, tooFarPos);
        spatialHash.insert(tooFar, tooFarPos);
        
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        REQUIRE(result.visibleEntities.size() == 3);  // near, mid, far
        
        // Verify all expected entities are visible
        bool foundNear = false, foundMid = false, foundFar = false;
        for (EntityID e : result.visibleEntities) {
            if (e == near) foundNear = true;
            if (e == mid) foundMid = true;
            if (e == far) foundFar = true;
        }
        REQUIRE(foundNear);
        REQUIRE(foundMid);
        REQUIRE(foundFar);
    }
}

TEST_CASE("Area of Interest enter/leave detection", "[aoi]") {
    Registry registry;
    SpatialHash spatialHash;
    AreaOfInterestSystem aoiSystem;
    ConnectionID connId = 42;
    
    EntityID viewer = registry.create();
    Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<Position>(viewer, viewerPos);
    
    EntityID target = registry.create();
    Position targetPos = Position::fromVec3(glm::vec3(30.0f, 0.0f, 0.0f));
    registry.emplace<Position>(target, targetPos);
    
    SECTION("First query - all entities marked as entered") {
        spatialHash.insert(target, targetPos);
        
        AOIResult visible = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        AOIResult changes = aoiSystem.getAOIChanges(connId, visible.visibleEntities);
        
        REQUIRE(changes.enteredEntities.size() == 1);
        REQUIRE(changes.enteredEntities[0] == target);
        REQUIRE(changes.leftEntities.size() == 0);
    }
    
    SECTION("Same entities - no changes") {
        spatialHash.insert(target, targetPos);
        
        // First query
        AOIResult visible1 = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        aoiSystem.getAOIChanges(connId, visible1.visibleEntities);
        
        // Second query with same entities
        AOIResult visible2 = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        AOIResult changes = aoiSystem.getAOIChanges(connId, visible2.visibleEntities);
        
        REQUIRE(changes.enteredEntities.size() == 0);
        REQUIRE(changes.leftEntities.size() == 0);
    }
    
    SECTION("Entity leaves AOI") {
        spatialHash.insert(target, targetPos);
        
        // First query - entity visible
        AOIResult visible1 = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        aoiSystem.getAOIChanges(connId, visible1.visibleEntities);
        
        // Simulate entity moving out of range
        // In real usage, we'd update spatial hash position, but here we just simulate by clearing
        spatialHash.clear();
        
        // Second query - no entities visible
        AOIResult visible2 = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        AOIResult changes = aoiSystem.getAOIChanges(connId, visible2.visibleEntities);
        
        REQUIRE(changes.enteredEntities.size() == 0);
        REQUIRE(changes.leftEntities.size() == 1);
        REQUIRE(changes.leftEntities[0] == target);
    }
    
    SECTION("New entity enters AOI") {
        // First query - no entities
        AOIResult visible1 = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        aoiSystem.getAOIChanges(connId, visible1.visibleEntities);
        
        // Entity appears
        spatialHash.insert(target, targetPos);
        
        // Second query - entity now visible
        AOIResult visible2 = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        AOIResult changes = aoiSystem.getAOIChanges(connId, visible2.visibleEntities);
        
        REQUIRE(changes.enteredEntities.size() == 1);
        REQUIRE(changes.enteredEntities[0] == target);
        REQUIRE(changes.leftEntities.size() == 0);
    }
}

TEST_CASE("Area of Interest update priority", "[aoi]") {
    AreaOfInterestSystem aoiSystem;
    
    Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
    
    SECTION("NEAR radius has priority 0 (full rate)") {
        Position targetPos = Position::fromVec3(glm::vec3(25.0f, 0.0f, 0.0f));  // 25m
        int priority = aoiSystem.getUpdatePriority(viewerPos, targetPos);
        REQUIRE(priority == 0);
    }
    
    SECTION("Exactly at NEAR boundary") {
        Position targetPos = Position::fromVec3(glm::vec3(50.0f, 0.0f, 0.0f));  // 50m
        int priority = aoiSystem.getUpdatePriority(viewerPos, targetPos);
        REQUIRE(priority == 0);  // Still full rate at boundary
    }
    
    SECTION("MID radius has priority 1 (half rate)") {
        Position targetPos = Position::fromVec3(glm::vec3(75.0f, 0.0f, 0.0f));  // 75m
        int priority = aoiSystem.getUpdatePriority(viewerPos, targetPos);
        REQUIRE(priority == 1);
    }
    
    SECTION("Exactly at MID boundary") {
        Position targetPos = Position::fromVec3(glm::vec3(100.0f, 0.0f, 0.0f));  // 100m
        int priority = aoiSystem.getUpdatePriority(viewerPos, targetPos);
        REQUIRE(priority == 1);  // Still half rate at boundary
    }
    
    SECTION("FAR radius has priority 2 (quarter rate)") {
        Position targetPos = Position::fromVec3(glm::vec3(150.0f, 0.0f, 0.0f));  // 150m
        int priority = aoiSystem.getUpdatePriority(viewerPos, targetPos);
        REQUIRE(priority == 2);
    }
    
    SECTION("At FAR boundary") {
        Position targetPos = Position::fromVec3(glm::vec3(200.0f, 0.0f, 0.0f));  // 200m
        int priority = aoiSystem.getUpdatePriority(viewerPos, targetPos);
        REQUIRE(priority == 2);  // Quarter rate at boundary (still visible)
    }
}

TEST_CASE("Area of Interest player state management", "[aoi]") {
    AreaOfInterestSystem aoiSystem;
    ConnectionID connId1 = 1;
    ConnectionID connId2 = 2;
    
    SECTION("Track multiple players separately") {
        std::vector<EntityID> visible1 = {static_cast<entt::entity>(1), static_cast<entt::entity>(2)};
        std::vector<EntityID> visible2 = {static_cast<entt::entity>(3)};
        
        aoiSystem.updatePlayerAOI(connId1, visible1);
        aoiSystem.updatePlayerAOI(connId2, visible2);
        
        // Both players should be tracked
        REQUIRE(aoiSystem.getPlayerCount() == 2);
    }
    
    SECTION("Remove player clears state") {
        std::vector<EntityID> visible = {static_cast<entt::entity>(1)};
        aoiSystem.updatePlayerAOI(connId1, visible);
        
        REQUIRE(aoiSystem.getPlayerCount() == 1);
        
        aoiSystem.removePlayer(connId1);
        
        REQUIRE(aoiSystem.getPlayerCount() == 0);
    }
    
    SECTION("Remove non-existent player is safe") {
        REQUIRE_NOTHROW(aoiSystem.removePlayer(999));
        REQUIRE(aoiSystem.getPlayerCount() == 0);
    }
}

TEST_CASE("Area of Interest performance", "[aoi][performance]") {
    Registry registry;
    SpatialHash spatialHash;
    AreaOfInterestSystem aoiSystem;
    
    // Create viewer
    EntityID viewer = registry.create();
    Position viewerPos = Position::fromVec3(glm::vec3(500.0f, 0.0f, 500.0f));
    registry.emplace<Position>(viewer, viewerPos);
    
    // Setup: Create 1000 entities at random positions
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1000.0f);
    
    for (int i = 0; i < 1000; ++i) {
        EntityID entity = registry.create();
        Position pos = Position::fromVec3(glm::vec3(dist(rng), 0.0f, dist(rng)));
        registry.emplace<Position>(entity, pos);
        spatialHash.insert(entity, pos);
    }
    
    SECTION("AOI query performance") {
        BENCHMARK("Query AOI with 1000 entities") {
            return aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos).visibleEntities.size();
        };
    }
    
    SECTION("AOI query meets performance budget") {
        // Query 100 times and measure
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i) {
            aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        }
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        auto avgUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() / 100.0;
        
        // AOI query should be fast (< 2ms per query for Debug build, < 0.5ms for Release)
        // Note: Debug builds are significantly slower due to iterator debugging
        #ifdef _DEBUG
            REQUIRE(avgUs < 2000);  // Relaxed threshold for Debug builds
        #else
            REQUIRE(avgUs < 500);   // Strict threshold for Release builds
        #endif
    }
}

TEST_CASE("Area of Interest edge cases", "[aoi]") {
    Registry registry;
    SpatialHash spatialHash;
    AreaOfInterestSystem aoiSystem;
    
    EntityID viewer = registry.create();
    Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<Position>(viewer, viewerPos);
    
    SECTION("Entity without Position component") {
        EntityID noPosEntity = registry.create();
        // No Position component added
        spatialHash.insert(noPosEntity, 10.0f, 10.0f);  // Spatial hash doesn't check registry
        
        // Should not crash and should filter out entities without Position
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        // The entity is in spatial hash but has no Position component in registry
        // Implementation should handle this gracefully
        REQUIRE_NOTHROW(aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos));
    }
    
    SECTION("Entity exactly at viewer position") {
        EntityID target = registry.create();
        Position targetPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));  // Same as viewer
        registry.emplace<Position>(target, targetPos);
        spatialHash.insert(target, targetPos);
        
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        // Should be visible (distance 0 <= FAR radius)
        // But not the viewer itself
        REQUIRE(result.visibleEntities.size() == 1);
        REQUIRE(result.visibleEntities[0] == target);
    }
    
    SECTION("Empty spatial hash returns empty result") {
        AOIResult result = aoiSystem.queryVisibleEntities(registry, spatialHash, viewer, viewerPos);
        
        REQUIRE(result.visibleEntities.size() == 0);
        REQUIRE(result.enteredEntities.size() == 0);
        REQUIRE(result.leftEntities.size() == 0);
    }
}
