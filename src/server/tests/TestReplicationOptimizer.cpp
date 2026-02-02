// [ZONE_AGENT] Unit tests for Replication Optimizer
// Tests spatial replication, distance-based prioritization, and bandwidth optimization

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zones/ReplicationOptimizer.hpp"
#include "physics/SpatialHash.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

using namespace DarkAges;
using Catch::Approx;

TEST_CASE("ReplicationOptimizer - Priority Calculation", "[replication]") {
    ReplicationOptimizer optimizer;
    
    SECTION("Near entities get high priority (0)") {
        // Entity at 30m should be near (0-50m)
        float dist = 30.0f;
        // Test priority calculation indirectly through update interval
        uint8_t priority = optimizer.calculatePriority(dist);
        REQUIRE(priority == 0);  // Near priority
        (void)dist;
    }
}

TEST_CASE("ReplicationOptimizer - Config Defaults", "[replication]") {
    ReplicationOptimizer optimizer;
    const auto& config = optimizer.getConfig();
    
    SECTION("Default config matches constants") {
        REQUIRE(config.nearRadius == Constants::AOI_RADIUS_NEAR);  // 50m
        REQUIRE(config.midRadius == Constants::AOI_RADIUS_MID);    // 100m
        REQUIRE(config.farRadius == Constants::AOI_RADIUS_FAR);    // 200m
        REQUIRE(config.nearRate == 20);  // Hz
        REQUIRE(config.midRate == 10);   // Hz
        REQUIRE(config.farRate == 5);    // Hz
    }
}

TEST_CASE("ReplicationOptimizer - Config Update", "[replication]") {
    ReplicationOptimizer optimizer;
    
    SECTION("Can update config at runtime") {
        ReplicationOptimizer::Config newConfig;
        newConfig.nearRate = 10;
        newConfig.midRate = 5;
        newConfig.farRate = 2;
        newConfig.maxEntitiesPerSnapshot = 50;
        
        optimizer.setConfig(newConfig);
        
        const auto& config = optimizer.getConfig();
        REQUIRE(config.nearRate == 10);
        REQUIRE(config.midRate == 5);
        REQUIRE(config.farRate == 2);
        REQUIRE(config.maxEntitiesPerSnapshot == 50);
    }
}

TEST_CASE("ReplicationOptimizer - Entity Update Tracking", "[replication]") {
    ReplicationOptimizer optimizer;
    
    SECTION("Mark entity as updated") {
        ConnectionID connId = 1;
        EntityID entity = static_cast<entt::entity>(42);
        uint32_t tick = 100;
        
        optimizer.markEntityUpdated(connId, entity, tick);
        
        // Should need update after enough ticks pass
        // Interval of 3 means: update every 3 ticks (at 100, 103, 106, etc.)
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 2, 0) == false);  // 2 < 3, not yet
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 3, 0) == true);   // 3 >= 3, time to update
    }
    
    SECTION("Different priorities have different intervals") {
        ConnectionID connId = 1;
        EntityID entity = static_cast<entt::entity>(42);
        uint32_t tick = 100;
        
        optimizer.markEntityUpdated(connId, entity, tick);
        
        // Priority 0 (near): 3 tick interval at 20Hz
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 2, 0) == false);
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 3, 0) == true);
        
        // Priority 1 (mid): 6 tick interval at 10Hz
        optimizer.markEntityUpdated(connId, entity, tick);
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 5, 1) == false);
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 6, 1) == true);
        
        // Priority 2 (far): 12 tick interval at 5Hz
        optimizer.markEntityUpdated(connId, entity, tick);
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 11, 2) == false);
        REQUIRE(optimizer.needsUpdate(connId, entity, tick + 12, 2) == true);
    }
    
    SECTION("Unknown client always needs update") {
        REQUIRE(optimizer.needsUpdate(999, static_cast<entt::entity>(42), 100, 0) == true);
    }
    
    SECTION("Remove client tracking") {
        ConnectionID connId = 1;
        EntityID entity = static_cast<entt::entity>(42);
        
        optimizer.markEntityUpdated(connId, entity, 100);
        REQUIRE(optimizer.getTrackedClientCount() == 1);
        
        optimizer.removeClientTracking(connId);
        REQUIRE(optimizer.getTrackedClientCount() == 0);
        REQUIRE(optimizer.needsUpdate(connId, entity, 200, 0) == true);
    }
}

TEST_CASE("ReplicationOptimizer - Filter By Update Rate", "[replication]") {
    ReplicationOptimizer optimizer;
    
    SECTION("Filter entities based on tick and priority") {
        std::vector<ReplicationPriority> priorities;
        
        // Add entities with different priorities
        priorities.push_back({static_cast<entt::entity>(1), 0, 100.0f, false});  // Near
        priorities.push_back({static_cast<entt::entity>(2), 1, 2500.0f, false}); // Mid
        priorities.push_back({static_cast<entt::entity>(3), 2, 10000.0f, false}); // Far
        
        // At tick 0 (divisible by all intervals)
        auto toUpdate0 = optimizer.filterByUpdateRate(priorities, 0);
        REQUIRE(toUpdate0.size() == 3);
        
        // At tick 3 (only near updates: 3 % 3 == 0)
        auto toUpdate3 = optimizer.filterByUpdateRate(priorities, 3);
        REQUIRE(toUpdate3.size() == 1);
        REQUIRE(toUpdate3[0] == static_cast<entt::entity>(1));
        
        // At tick 6 (near: 6 % 3 == 0, mid: 6 % 6 == 0)
        auto toUpdate6 = optimizer.filterByUpdateRate(priorities, 6);
        REQUIRE(toUpdate6.size() == 2);
        
        // At tick 12 (all update: 12 % 3 == 0, 12 % 6 == 0, 12 % 12 == 0)
        auto toUpdate12 = optimizer.filterByUpdateRate(priorities, 12);
        REQUIRE(toUpdate12.size() == 3);
    }
}

TEST_CASE("ReplicationOptimizer - Cull Non-Essential Fields", "[replication]") {
    SECTION("Far entities get minimal data") {
        Protocol::EntityStateData data{};
        data.position = Position{1000, 0, 2000};
        data.velocity = Velocity{100, 0, 50};
        data.rotation = Rotation{1.5f, 0.2f};
        data.animState = 5;
        data.healthPercent = 80;
        
        ReplicationOptimizer::cullNonEssentialFields(data, 2);  // Far priority
        
        // Position and health should remain
        REQUIRE(data.position.x == 1000);
        REQUIRE(data.healthPercent == 80);
        
        // Velocity, rotation, animState should be zeroed
        REQUIRE(data.velocity.dx == 0);
        REQUIRE(data.velocity.dy == 0);
        REQUIRE(data.velocity.dz == 0);
        REQUIRE(data.rotation.yaw == 0.0f);
        REQUIRE(data.rotation.pitch == 0.0f);
        REQUIRE(data.animState == 0);
    }
    
    SECTION("Mid entities get no animation state") {
        Protocol::EntityStateData data{};
        data.position = Position{1000, 0, 2000};
        data.velocity = Velocity{100, 0, 50};
        data.rotation = Rotation{1.5f, 0.2f};
        data.animState = 5;
        data.healthPercent = 80;
        
        ReplicationOptimizer::cullNonEssentialFields(data, 1);  // Mid priority
        
        // All fields except animState should remain
        REQUIRE(data.position.x == 1000);
        REQUIRE(data.velocity.dx == 100);
        REQUIRE(data.rotation.yaw == 1.5f);
        REQUIRE(data.healthPercent == 80);
        REQUIRE(data.animState == 0);  // Culled
    }
    
    SECTION("Near entities get full data") {
        Protocol::EntityStateData data{};
        data.position = Position{1000, 0, 2000};
        data.velocity = Velocity{100, 0, 50};
        data.rotation = Rotation{1.5f, 0.2f};
        data.animState = 5;
        data.healthPercent = 80;
        
        ReplicationOptimizer::cullNonEssentialFields(data, 0);  // Near priority
        
        // All fields should remain unchanged
        REQUIRE(data.position.x == 1000);
        REQUIRE(data.velocity.dx == 100);
        REQUIRE(data.rotation.yaw == 1.5f);
        REQUIRE(data.animState == 5);
        REQUIRE(data.healthPercent == 80);
    }
}

TEST_CASE("ReplicationOptimizer - Spatial Query Integration", "[replication]") {
    Registry registry;
    SpatialHash spatialHash;
    ReplicationOptimizer optimizer;
    
    SECTION("Calculate priorities with spatial hash") {
        // Create viewer entity
        EntityID viewer = registry.create();
        Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        registry.emplace<Position>(viewer, viewerPos);
        
        // Create nearby entities at different distances
        EntityID nearEntity = registry.create();
        registry.emplace<Position>(nearEntity, Position::fromVec3(glm::vec3(30.0f, 0.0f, 0.0f)));  // 30m
        registry.emplace<Velocity>(nearEntity);
        registry.emplace<Rotation>(nearEntity);
        spatialHash.insert(nearEntity, 30.0f, 0.0f);
        
        EntityID midEntity = registry.create();
        registry.emplace<Position>(midEntity, Position::fromVec3(glm::vec3(75.0f, 0.0f, 0.0f)));  // 75m
        registry.emplace<Velocity>(midEntity);
        registry.emplace<Rotation>(midEntity);
        spatialHash.insert(midEntity, 75.0f, 0.0f);
        
        EntityID farEntity = registry.create();
        registry.emplace<Position>(farEntity, Position::fromVec3(glm::vec3(150.0f, 0.0f, 0.0f)));  // 150m
        registry.emplace<Velocity>(farEntity);
        registry.emplace<Rotation>(farEntity);
        spatialHash.insert(farEntity, 150.0f, 0.0f);
        
        EntityID outOfRangeEntity = registry.create();
        registry.emplace<Position>(outOfRangeEntity, Position::fromVec3(glm::vec3(250.0f, 0.0f, 0.0f)));  // 250m
        spatialHash.insert(outOfRangeEntity, 250.0f, 0.0f);
        
        // Calculate priorities
        auto priorities = optimizer.calculatePriorities(
            registry, spatialHash, viewer, viewerPos, 0);
        
        // Should find 3 entities (near, mid, far) - outOfRange is beyond 200m
        REQUIRE(priorities.size() == 3);
        
        // Should be sorted by priority then distance
        // First should be near entity (priority 0)
        REQUIRE(priorities[0].entity == nearEntity);
        REQUIRE(priorities[0].priority == 0);  // Near
        
        // Second should be mid entity (priority 1)
        REQUIRE(priorities[1].entity == midEntity);
        REQUIRE(priorities[1].priority == 1);  // Mid
        
        // Third should be far entity (priority 2)
        REQUIRE(priorities[2].entity == farEntity);
        REQUIRE(priorities[2].priority == 2);  // Far
    }
    
    SECTION("Viewer is excluded from priorities") {
        EntityID viewer = registry.create();
        Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        registry.emplace<Position>(viewer, viewerPos);
        spatialHash.insert(viewer, 0.0f, 0.0f);
        
        auto priorities = optimizer.calculatePriorities(
            registry, spatialHash, viewer, viewerPos, 0);
        
        // Should be empty - viewer excluded itself
        REQUIRE(priorities.empty());
    }
    
    SECTION("Entities without Position component are skipped") {
        EntityID viewer = registry.create();
        Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        registry.emplace<Position>(viewer, viewerPos);
        
        // Entity without Position component
        EntityID noPosEntity = registry.create();
        spatialHash.insert(noPosEntity, 30.0f, 0.0f);
        
        auto priorities = optimizer.calculatePriorities(
            registry, spatialHash, viewer, viewerPos, 0);
        
        // Should be empty - noPosEntity has no Position component
        REQUIRE(priorities.empty());
    }
}

TEST_CASE("ReplicationOptimizer - Build Entity States", "[replication]") {
    Registry registry;
    ReplicationOptimizer optimizer;
    
    SECTION("Build states for entities") {
        // Create entities with various components
        EntityID entity1 = registry.create();
        registry.emplace<Position>(entity1, Position::fromVec3(glm::vec3(10.0f, 0.0f, 20.0f)));
        registry.emplace<Velocity>(entity1, Velocity{100, 0, 200});
        registry.emplace<Rotation>(entity1, Rotation{1.0f, 0.5f});
        registry.emplace<CombatState>(entity1);
        
        EntityID entity2 = registry.create();
        registry.emplace<Position>(entity2, Position::fromVec3(glm::vec3(50.0f, 0.0f, 50.0f)));
        registry.emplace<Velocity>(entity2, Velocity{50, 0, 100});
        registry.emplace<Rotation>(entity2, Rotation{2.0f, 0.0f});
        // No CombatState
        
        std::vector<EntityID> entities = {entity1, entity2};
        auto states = optimizer.buildEntityStates(registry, entities, 100);
        
        REQUIRE(states.size() == 2);
        
        // First entity
        REQUIRE(states[0].entity == entity1);
        REQUIRE(states[0].position.x == Approx(10000).margin(1));  // 10.0f * 1000
        REQUIRE(states[0].velocity.dx == 100);
        REQUIRE(states[0].rotation.yaw == 1.0f);
        REQUIRE(states[0].healthPercent == 100);  // Default
        REQUIRE(states[0].timestamp == 100);
        
        // Second entity
        REQUIRE(states[1].entity == entity2);
        REQUIRE(states[1].position.x == Approx(50000).margin(1));  // 50.0f * 1000
        REQUIRE(states[1].healthPercent == 100);  // Default when no CombatState
    }
    
    SECTION("Entity type detection") {
        EntityID player = registry.create();
        registry.emplace<Position>(player);
        registry.emplace<Velocity>(player);
        registry.emplace<Rotation>(player);
        registry.emplace<PlayerTag>(player);
        
        EntityID npc = registry.create();
        registry.emplace<Position>(npc);
        registry.emplace<Velocity>(npc);
        registry.emplace<Rotation>(npc);
        registry.emplace<NPCTag>(npc);
        
        EntityID projectile = registry.create();
        registry.emplace<Position>(projectile);
        registry.emplace<Velocity>(projectile);
        registry.emplace<Rotation>(projectile);
        registry.emplace<ProjectileTag>(projectile);
        
        EntityID other = registry.create();
        registry.emplace<Position>(other);
        registry.emplace<Velocity>(other);
        registry.emplace<Rotation>(other);
        // No specific tag
        
        std::vector<EntityID> entities = {player, npc, projectile, other};
        auto states = optimizer.buildEntityStates(registry, entities, 0);
        
        REQUIRE(states[0].entityType == 0);  // Player
        REQUIRE(states[1].entityType == 3);  // NPC
        REQUIRE(states[2].entityType == 1);  // Projectile
        REQUIRE(states[3].entityType == 2);  // Other/Loot
    }
}

TEST_CASE("ReplicationOptimizer - Max Entities Limit", "[replication]") {
    Registry registry;
    SpatialHash spatialHash;
    
    // Create optimizer with low limit
    ReplicationOptimizer::Config config;
    config.maxEntitiesPerSnapshot = 5;
    ReplicationOptimizer optimizer(config);
    
    EntityID viewer = registry.create();
    Position viewerPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<Position>(viewer, viewerPos);
    
    // Create 10 entities around viewer
    for (int i = 0; i < 10; ++i) {
        EntityID entity = registry.create();
        float x = static_cast<float>(i * 10);
        registry.emplace<Position>(entity, Position::fromVec3(glm::vec3(x, 0.0f, 0.0f)));
        registry.emplace<Velocity>(entity);
        registry.emplace<Rotation>(entity);
        spatialHash.insert(entity, x, 0.0f);
    }
    
    auto priorities = optimizer.calculatePriorities(
        registry, spatialHash, viewer, viewerPos, 0);
    
    // Should be limited to maxEntitiesPerSnapshot
    REQUIRE(priorities.size() == 5);
}

TEST_CASE("ReplicationOptimizer - Remove Entity Tracking", "[replication]") {
    ReplicationOptimizer optimizer;
    
    EntityID entity1 = static_cast<entt::entity>(1);
    EntityID entity2 = static_cast<entt::entity>(2);
    ConnectionID conn1 = 1;
    ConnectionID conn2 = 2;
    
    // Mark entities as updated for multiple clients
    optimizer.markEntityUpdated(conn1, entity1, 100);
    optimizer.markEntityUpdated(conn1, entity2, 100);
    optimizer.markEntityUpdated(conn2, entity1, 100);
    optimizer.markEntityUpdated(conn2, entity2, 100);
    
    // Remove entity1 tracking
    optimizer.removeEntityTracking(entity1);
    
    // entity1 should need update again for all clients
    REQUIRE(optimizer.needsUpdate(conn1, entity1, 200, 0) == true);
    REQUIRE(optimizer.needsUpdate(conn2, entity1, 200, 0) == true);
    
    // entity2 should still be tracked (updated at 100, checking at 102: 2 ticks < 3 interval)
    REQUIRE(optimizer.needsUpdate(conn1, entity2, 102, 0) == false);
}
