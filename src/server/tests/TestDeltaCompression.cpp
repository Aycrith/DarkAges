// [NETWORK_AGENT] Delta Compression unit tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "netcode/NetworkManager.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

using namespace DarkAges;

TEST_CASE("Delta compression - Full snapshot (no baseline)", "[network][delta]") {
    SECTION("Empty snapshot") {
        std::vector<Protocol::EntityStateData> entities;
        std::vector<EntityID> removed;
        
        auto data = Protocol::createDeltaSnapshot(
            100,  // server tick
            0,    // baseline tick = 0 means full snapshot
            entities,
            removed,
            {}    // no baseline
        );
        
        REQUIRE_FALSE(data.empty());
        
        // Verify we can parse it
        std::vector<Protocol::EntityStateData> outEntities;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outTick == 100);
        REQUIRE(outBaseline == 0);
        REQUIRE(outEntities.empty());
        REQUIRE(outRemoved.empty());
    }
    
    SECTION("Single entity full snapshot") {
        std::vector<Protocol::EntityStateData> entities;
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(42);
        entity.position = Position::fromVec3(glm::vec3(10.0f, 5.0f, 20.0f));
        entity.velocity = Velocity{100, 0, 0};  // 0.1 m/s in X
        entity.rotation = Rotation{1.57f, 0.0f};
        entity.healthPercent = 85;
        entity.animState = 1;  // ANIM_WALK
        entity.entityType = 0; // Player
        entities.push_back(entity);
        
        std::vector<EntityID> removed;
        
        auto data = Protocol::createDeltaSnapshot(100, 0, entities, removed, {});
        REQUIRE_FALSE(data.empty());
        
        // Verify parsing
        std::vector<Protocol::EntityStateData> outEntities;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outEntities.size() == 1);
        REQUIRE(outEntities[0].entity == entity.entity);
        
        // Position should be close (some quantization loss is expected)
        auto pos = outEntities[0].position.toVec3();
        REQUIRE(std::abs(pos.x - 10.0f) < 0.1f);
        REQUIRE(std::abs(pos.y - 5.0f) < 0.1f);
        REQUIRE(std::abs(pos.z - 20.0f) < 0.1f);
        
        REQUIRE(outEntities[0].healthPercent == 85);
        REQUIRE(outEntities[0].animState == 1);
        REQUIRE(outEntities[0].entityType == 0);
    }
}

TEST_CASE("Delta compression - Delta against baseline", "[network][delta]") {
    SECTION("No changes - empty delta") {
        // Create baseline
        std::vector<Protocol::EntityStateData> baseline;
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(1);
        entity.position = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        entity.velocity = Velocity{0, 0, 0};
        entity.rotation = Rotation{0.0f, 0.0f};
        entity.healthPercent = 100;
        entity.animState = 0;
        entity.entityType = 0;
        baseline.push_back(entity);
        
        // Same entity, no changes
        std::vector<Protocol::EntityStateData> current = baseline;
        std::vector<EntityID> removed;
        
        auto data = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
        
        // Should still have header but no entity data
        std::vector<Protocol::EntityStateData> outEntities;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outEntities.empty());  // No changes sent
    }
    
    SECTION("Position change only") {
        // Create baseline
        std::vector<Protocol::EntityStateData> baseline;
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(1);
        entity.position = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        entity.velocity = Velocity{0, 0, 0};
        entity.rotation = Rotation{0.0f, 0.0f};
        entity.healthPercent = 100;
        entity.animState = 0;
        entity.entityType = 0;
        baseline.push_back(entity);
        
        // Entity moved slightly
        std::vector<Protocol::EntityStateData> current = baseline;
        current[0].position = Position::fromVec3(glm::vec3(1.0f, 0.0f, 0.5f));
        std::vector<EntityID> removed;
        
        auto data = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
        
        // Verify delta is smaller than full snapshot
        auto fullData = Protocol::createDeltaSnapshot(101, 0, current, removed, {});
        REQUIRE(data.size() < fullData.size());
        
        // Apply delta and verify
        std::vector<Protocol::EntityStateData> outEntities = baseline;  // Start with baseline
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outEntities.size() == 1);
        
        auto pos = outEntities[0].position.toVec3();
        REQUIRE(std::abs(pos.x - 1.0f) < 0.1f);
        REQUIRE(std::abs(pos.y - 0.0f) < 0.1f);
        REQUIRE(std::abs(pos.z - 0.5f) < 0.1f);
    }
    
    SECTION("New entity added") {
        std::vector<Protocol::EntityStateData> baseline;
        std::vector<EntityID> removed;
        
        // Add a new entity
        std::vector<Protocol::EntityStateData> current;
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(99);
        entity.position = Position::fromVec3(glm::vec3(50.0f, 10.0f, 30.0f));
        entity.velocity = Velocity{0, 0, 0};
        entity.rotation = Rotation{0.0f, 0.0f};
        entity.healthPercent = 100;
        entity.animState = 0;
        entity.entityType = 0;
        current.push_back(entity);
        
        auto data = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
        
        // Should contain full entity data
        std::vector<Protocol::EntityStateData> outEntities;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outEntities.size() == 1);
        REQUIRE(outEntities[0].entity == static_cast<EntityID>(99));
    }
    
    SECTION("Entity removed") {
        // Create baseline with entity
        std::vector<Protocol::EntityStateData> baseline;
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(5);
        entity.position = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        entity.velocity = Velocity{0, 0, 0};
        entity.rotation = Rotation{0.0f, 0.0f};
        entity.healthPercent = 100;
        entity.animState = 0;
        entity.entityType = 0;
        baseline.push_back(entity);
        
        // Empty current (entity removed)
        std::vector<Protocol::EntityStateData> current;
        std::vector<EntityID> removed;
        removed.push_back(static_cast<EntityID>(5));
        
        auto data = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
        
        std::vector<Protocol::EntityStateData> outEntities = baseline;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outRemoved.size() == 1);
        REQUIRE(outRemoved[0] == static_cast<EntityID>(5));
    }
    
    SECTION("Multiple field changes") {
        // Create baseline
        std::vector<Protocol::EntityStateData> baseline;
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(1);
        entity.position = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        entity.velocity = Velocity{0, 0, 0};
        entity.rotation = Rotation{0.0f, 0.0f};
        entity.healthPercent = 100;
        entity.animState = 0;
        entity.entityType = 0;
        baseline.push_back(entity);
        
        // Multiple changes
        std::vector<Protocol::EntityStateData> current = baseline;
        current[0].position = Position::fromVec3(glm::vec3(5.0f, 0.0f, 0.0f));
        current[0].healthPercent = 75;
        current[0].animState = 2;  // ANIM_RUN
        std::vector<EntityID> removed;
        
        auto data = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
        
        std::vector<Protocol::EntityStateData> outEntities = baseline;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outEntities.size() == 1);
        REQUIRE(outEntities[0].healthPercent == 75);
        REQUIRE(outEntities[0].animState == 2);
    }
}

TEST_CASE("Delta compression - Position delta encoding", "[network][delta]") {
    using namespace Protocol::DeltaEncoding;
    
    SECTION("Small delta fits in 1 byte") {
        Position baseline = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        Position current = Position::fromVec3(glm::vec3(0.05f, 0.0f, 0.0f));  // 5cm movement
        
        uint8_t buffer[32];
        size_t encoded = encodePositionDelta(buffer, sizeof(buffer), current, baseline);
        REQUIRE(encoded > 0);
        
        Position decoded;
        size_t decodedBytes = decodePositionDelta(buffer, sizeof(buffer), decoded, baseline);
        REQUIRE(decodedBytes == encoded);
        
        auto pos = decoded.toVec3();
        REQUIRE(std::abs(pos.x - 0.05f) < 0.01f);
    }
    
    SECTION("Medium delta fits in 3 bytes per component") {
        Position baseline = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f));
        Position current = Position::fromVec3(glm::vec3(5.0f, 3.0f, 2.0f));  // meters movement
        
        uint8_t buffer[32];
        size_t encoded = encodePositionDelta(buffer, sizeof(buffer), current, baseline);
        REQUIRE(encoded > 0);
        
        Position decoded;
        size_t decodedBytes = decodePositionDelta(buffer, sizeof(buffer), decoded, baseline);
        REQUIRE(decodedBytes == encoded);
        
        auto pos = decoded.toVec3();
        REQUIRE(std::abs(pos.x - 5.0f) < 0.1f);
        REQUIRE(std::abs(pos.y - 3.0f) < 0.1f);
        REQUIRE(std::abs(pos.z - 2.0f) < 0.1f);
    }
    
    SECTION("Position round-trip preserves values") {
        Position baseline = Position::fromVec3(glm::vec3(100.0f, 50.0f, -100.0f));
        Position current = Position::fromVec3(glm::vec3(105.5f, 52.3f, -98.0f));
        
        uint8_t buffer[32];
        size_t encoded = encodePositionDelta(buffer, sizeof(buffer), current, baseline);
        REQUIRE(encoded > 0);
        
        Position decoded;
        size_t decodedBytes = decodePositionDelta(buffer, sizeof(buffer), decoded, baseline);
        REQUIRE(decodedBytes == encoded);
        
        // Due to quantization, values should be close but not exact
        auto pos = decoded.toVec3();
        auto expected = current.toVec3();
        REQUIRE(std::abs(pos.x - expected.x) < 0.1f);
        REQUIRE(std::abs(pos.y - expected.y) < 0.1f);
        REQUIRE(std::abs(pos.z - expected.z) < 0.1f);
    }
}

TEST_CASE("Delta compression - Bandwidth reduction", "[network][delta]") {
    SECTION("Typical game scenario") {
        // Simulate 10 players in the world
        std::vector<Protocol::EntityStateData> baseline;
        for (int i = 0; i < 10; ++i) {
            Protocol::EntityStateData entity;
            entity.entity = static_cast<EntityID>(i + 1);
            entity.position = Position::fromVec3(glm::vec3(i * 10.0f, 0.0f, i * 5.0f));
            entity.velocity = Velocity{0, 0, 0};
            entity.rotation = Rotation{0.0f, 0.0f};
            entity.healthPercent = 100;
            entity.animState = 0;
            entity.entityType = 0;
            baseline.push_back(entity);
        }
        
        // Next frame: players moved slightly, one took damage
        std::vector<Protocol::EntityStateData> current = baseline;
        for (auto& entity : current) {
            // Small position change (walking)
            auto pos = entity.position.toVec3();
            entity.position = Position::fromVec3(glm::vec3(
                pos.x + 0.1f, pos.y, pos.z + 0.05f));
        }
        current[3].healthPercent = 85;  // Player 4 took damage
        
        std::vector<EntityID> removed;
        
        // Measure full snapshot size
        auto fullData = Protocol::createDeltaSnapshot(101, 0, current, removed, {});
        
        // Measure delta snapshot size
        auto deltaData = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
        
        // Delta should be significantly smaller
        float compressionRatio = static_cast<float>(deltaData.size()) / fullData.size();
        REQUIRE(compressionRatio < 0.5f);  // At least 50% reduction
        
        // Verify delta can be correctly applied
        std::vector<Protocol::EntityStateData> outEntities = baseline;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            deltaData, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outEntities.size() == 10);
        REQUIRE(outEntities[3].healthPercent == 85);
    }
}

TEST_CASE("Delta compression - Edge cases", "[network][delta]") {
    SECTION("Large entity count") {
        std::vector<Protocol::EntityStateData> entities;
        for (int i = 0; i < 100; ++i) {
            Protocol::EntityStateData entity;
            entity.entity = static_cast<EntityID>(i + 1);
            entity.position = Position::fromVec3(glm::vec3(
                static_cast<float>(i), 0.0f, static_cast<float>(i * 2)));
            entity.velocity = Velocity{0, 0, 0};
            entity.rotation = Rotation{0.0f, 0.0f};
            entity.healthPercent = 100;
            entity.animState = 0;
            entity.entityType = 0;
            entities.push_back(entity);
        }
        
        std::vector<EntityID> removed;
        auto data = Protocol::createDeltaSnapshot(100, 0, entities, removed, {});
        
        std::vector<Protocol::EntityStateData> outEntities;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            data, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE(success);
        REQUIRE(outEntities.size() == 100);
    }
    
    SECTION("Corrupt data handling") {
        std::vector<uint8_t> corruptData = {0x00, 0x01, 0x02};  // Too small for header
        
        std::vector<Protocol::EntityStateData> outEntities;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            corruptData, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE_FALSE(success);
    }
    
    SECTION("Empty data") {
        std::vector<uint8_t> emptyData;
        
        std::vector<Protocol::EntityStateData> outEntities;
        std::vector<EntityID> outRemoved;
        uint32_t outTick, outBaseline;
        
        bool success = Protocol::applyDeltaSnapshot(
            emptyData, outEntities, outTick, outBaseline, outRemoved);
        
        REQUIRE_FALSE(success);
    }
}

TEST_CASE("Delta compression - Benchmark", "[network][delta][!benchmark]") {
    // Setup: 50 entities
    std::vector<Protocol::EntityStateData> baseline;
    for (int i = 0; i < 50; ++i) {
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(i + 1);
        entity.position = Position::fromVec3(glm::vec3(
            static_cast<float>(i * 2), 0.0f, static_cast<float>(i)));
        entity.velocity = Velocity{0, 0, 0};
        entity.rotation = Rotation{static_cast<float>(i), 0.0f};
        entity.healthPercent = 100;
        entity.animState = 0;
        entity.entityType = 0;
        baseline.push_back(entity);
    }
    
    std::vector<Protocol::EntityStateData> current = baseline;
    // Modify half the entities
    for (size_t i = 0; i < current.size(); i += 2) {
        auto pos = current[i].position.toVec3();
        current[i].position = Position::fromVec3(glm::vec3(pos.x + 0.5f, pos.y, pos.z));
    }
    std::vector<EntityID> removed;
    
    BENCHMARK("Create delta snapshot (50 entities, 50% changed)") {
        return Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
    };
    
    auto data = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
    std::vector<Protocol::EntityStateData> outEntities = baseline;
    std::vector<EntityID> outRemoved;
    uint32_t outTick, outBaseline;
    
    BENCHMARK("Apply delta snapshot (50 entities)") {
        std::vector<Protocol::EntityStateData> entities = baseline;
        Protocol::applyDeltaSnapshot(data, entities, outTick, outBaseline, outRemoved);
        return entities;
    };
}
