// [PHYSICS_AGENT] Spatial Hash unit tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "physics/SpatialHash.hpp"
#include <random>
#include <algorithm>
#include <chrono>

using namespace DarkAges;

TEST_CASE("Spatial Hash basic operations", "[spatial]") {
    SpatialHash hash;
    
    SECTION("Insert and query single entity") {
        EntityID entity = static_cast<entt::entity>(1);
        hash.insert(entity, 5.0f, 5.0f);
        
        auto nearby = hash.query(5.0f, 5.0f, 1.0f);
        REQUIRE(nearby.size() == 1);
        REQUIRE(nearby[0] == entity);
    }
    
    SECTION("Insert and query multiple entities in same cell") {
        EntityID e1 = static_cast<entt::entity>(1);
        EntityID e2 = static_cast<entt::entity>(2);
        EntityID e3 = static_cast<entt::entity>(3);
        
        hash.insert(e1, 5.0f, 5.0f);
        hash.insert(e2, 5.1f, 5.1f);  // Same 10m cell
        hash.insert(e3, 100.0f, 100.0f);  // Different cell
        
        auto nearby = hash.query(5.0f, 5.0f, 1.0f);
        REQUIRE(nearby.size() == 2);
        
        // Verify both nearby entities are found
        bool foundE1 = false, foundE2 = false;
        for (EntityID e : nearby) {
            if (e == e1) foundE1 = true;
            if (e == e2) foundE2 = true;
        }
        REQUIRE(foundE1);
        REQUIRE(foundE2);
    }
    
    SECTION("Query radius filtering") {
        EntityID e1 = static_cast<entt::entity>(1);
        EntityID e2 = static_cast<entt::entity>(2);
        
        hash.insert(e1, 0.0f, 0.0f);
        hash.insert(e2, 50.0f, 0.0f);  // 50m away
        
        auto nearbyClose = hash.query(0.0f, 0.0f, 10.0f);
        REQUIRE(nearbyClose.size() == 1);
        
        auto nearbyFar = hash.query(0.0f, 0.0f, 100.0f);
        REQUIRE(nearbyFar.size() == 2);
    }
    
    SECTION("Clear removes all entities") {
        EntityID e1 = static_cast<entt::entity>(1);
        EntityID e2 = static_cast<entt::entity>(2);
        
        hash.insert(e1, 5.0f, 5.0f);
        hash.insert(e2, 5.0f, 5.0f);
        
        hash.clear();
        
        auto nearby = hash.query(5.0f, 5.0f, 10.0f);
        REQUIRE(nearby.size() == 0);
    }
    
    SECTION("Update moves entity between cells") {
        EntityID e1 = static_cast<entt::entity>(1);
        
        hash.insert(e1, 5.0f, 5.0f);
        
        // Move to different cell (10m cells, so 20m is definitely different cell)
        hash.update(e1, 5.0f, 5.0f, 25.0f, 25.0f);
        
        auto oldNearby = hash.query(5.0f, 5.0f, 1.0f);
        REQUIRE(oldNearby.size() == 0);
        
        auto newNearby = hash.query(25.0f, 25.0f, 1.0f);
        REQUIRE(newNearby.size() == 1);
    }
    
    SECTION("Entity count tracking") {
        REQUIRE(hash.getTotalEntityCount() == 0);
        
        hash.insert(static_cast<entt::entity>(1), 5.0f, 5.0f);
        hash.insert(static_cast<entt::entity>(2), 15.0f, 15.0f);
        hash.insert(static_cast<entt::entity>(3), 25.0f, 25.0f);
        
        REQUIRE(hash.getTotalEntityCount() == 3);
    }
}

TEST_CASE("Spatial Hash performance", "[spatial][performance]") {
    SpatialHash hash;
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dist(0.0f, 1000.0f);
    
    SECTION("Insert 1000 entities") {
        BENCHMARK("Insert 1000 entities") {
            hash.clear();
            for (int i = 0; i < 1000; ++i) {
                hash.insert(static_cast<entt::entity>(i), dist(rng), dist(rng));
            }
            return hash.getTotalEntityCount();
        };
    }
    
    SECTION("Query with 1000 entities") {
        // Setup
        for (int i = 0; i < 1000; ++i) {
            hash.insert(static_cast<entt::entity>(i), dist(rng), dist(rng));
        }
        
        BENCHMARK("Query 50m radius") {
            return hash.query(500.0f, 500.0f, 50.0f).size();
        };
        
        BENCHMARK("Query 100m radius") {
            return hash.query(500.0f, 500.0f, 100.0f).size();
        };
    }
    
    SECTION("Performance requirements") {
        // Insert 1000 random entities
        for (int i = 0; i < 1000; ++i) {
            hash.insert(static_cast<entt::entity>(i), dist(rng), dist(rng));
        }
        
        // Query 100 times and measure total time
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i) {
            hash.query(500.0f, 500.0f, 50.0f);
        }
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        auto avgUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() / 100.0;
        
        REQUIRE(avgUs < 100);  // Must average < 0.1ms per query
    }
}

TEST_CASE("Spatial Hash cell calculations", "[spatial]") {
    SpatialHash hash(10.0f);  // 10m cells
    
    SECTION("Cell coordinates") {
        auto c1 = hash.getCellCoord(5.0f, 5.0f);
        REQUIRE(c1.x == 0);
        REQUIRE(c1.z == 0);
        
        auto c2 = hash.getCellCoord(15.0f, 15.0f);
        REQUIRE(c2.x == 1);
        REQUIRE(c2.z == 1);
        
        auto c3 = hash.getCellCoord(-5.0f, -5.0f);
        REQUIRE(c3.x == -1);
        REQUIRE(c3.z == -1);
    }
    
    SECTION("Adjacent query") {
        EntityID center = static_cast<entt::entity>(1);
        hash.insert(center, 50.0f, 50.0f);
        
        // Insert entities in neighboring cells
        hash.insert(static_cast<entt::entity>(2), 55.0f, 55.0f);  // Same cell
        hash.insert(static_cast<entt::entity>(3), 65.0f, 50.0f);  // Adjacent cell (+1, 0)
        hash.insert(static_cast<entt::entity>(4), 35.0f, 50.0f);  // Adjacent cell (-1, 0)
        hash.insert(static_cast<entt::entity>(5), 150.0f, 150.0f);  // Far away
        
        auto adjacent = hash.queryAdjacent(50.0f, 50.0f);
        
        // Should find entities in 3x3 grid around center
        REQUIRE(adjacent.size() >= 4);
        
        // Far entity should not be included
        bool foundFar = false;
        for (EntityID e : adjacent) {
            if (e == static_cast<entt::entity>(5)) foundFar = true;
        }
        REQUIRE_FALSE(foundFar);
    }
}
