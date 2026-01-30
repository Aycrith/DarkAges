// [NETWORK_AGENT] Network Protocol unit tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include "netcode/NetworkManager.hpp"
#include "shared/proto/game_protocol_generated.h"
#include <vector>
#include <cstring>

using namespace DarkAges;

TEST_CASE("Protocol serialization - InputState", "[network]") {
    SECTION("Serialize and deserialize input") {
        InputState input;
        input.forward = 1;
        input.left = 1;
        input.jump = 1;
        input.yaw = 1.57f;  // ~90 degrees
        input.pitch = 0.5f;
        input.sequence = 12345;
        input.timestamp_ms = 1000000;
        
        auto data = Protocol::serializeInput(input);
        REQUIRE_FALSE(data.empty());
        
        InputState deserialized;
        bool success = Protocol::deserializeInput(data, deserialized);
        
        REQUIRE(success);
        REQUIRE(deserialized.forward == input.forward);
        REQUIRE(deserialized.left == input.left);
        REQUIRE(deserialized.jump == input.jump);
        REQUIRE(deserialized.yaw == Catch::Approx(input.yaw).margin(0.001f));
        REQUIRE(deserialized.pitch == Catch::Approx(input.pitch).margin(0.001f));
        REQUIRE(deserialized.sequence == input.sequence);
        REQUIRE(deserialized.timestamp_ms == input.timestamp_ms);
    }
    
    SECTION("Serialize and deserialize all input flags") {
        InputState input;
        input.forward = 1;
        input.backward = 1;
        input.left = 1;
        input.right = 1;
        input.jump = 1;
        input.attack = 1;
        input.block = 1;
        input.sprint = 1;
        input.yaw = -2.5f;
        input.pitch = -0.75f;
        input.sequence = 99999;
        input.timestamp_ms = 5000000;
        
        auto data = Protocol::serializeInput(input);
        InputState deserialized;
        bool success = Protocol::deserializeInput(data, deserialized);
        
        REQUIRE(success);
        REQUIRE(deserialized.forward == 1);
        REQUIRE(deserialized.backward == 1);
        REQUIRE(deserialized.left == 1);
        REQUIRE(deserialized.right == 1);
        REQUIRE(deserialized.jump == 1);
        REQUIRE(deserialized.attack == 1);
        REQUIRE(deserialized.block == 1);
        REQUIRE(deserialized.sprint == 1);
    }
    
    SECTION("Deserialize with insufficient data fails") {
        std::vector<uint8_t> smallData = {0x01, 0x02, 0x03};
        InputState deserialized;
        
        bool success = Protocol::deserializeInput(smallData, deserialized);
        
        REQUIRE_FALSE(success);
    }
    
    SECTION("Deserialize with invalid data fails") {
        std::vector<uint8_t> invalidData(100, 0xFF);
        InputState deserialized;
        
        bool success = Protocol::deserializeInput(invalidData, deserialized);
        
        REQUIRE_FALSE(success);
    }
}

TEST_CASE("Protocol versioning", "[network]") {
    SECTION("Get protocol version") {
        uint32_t version = Protocol::getProtocolVersion();
        REQUIRE(version != 0);
        
        // Should be 1.0 = 0x00010000
        uint16_t major = static_cast<uint16_t>(version >> 16);
        uint16_t minor = static_cast<uint16_t>(version & 0xFFFF);
        REQUIRE(major == 1);
        REQUIRE(minor == 0);
    }
    
    SECTION("Version compatibility - same version") {
        uint32_t version = Protocol::getProtocolVersion();
        REQUIRE(Protocol::isVersionCompatible(version));
    }
    
    SECTION("Version compatibility - different minor version") {
        uint32_t serverVersion = Protocol::getProtocolVersion();
        uint32_t clientVersion = (serverVersion & 0xFFFF0000) | 0x0001;  // Same major, different minor
        REQUIRE(Protocol::isVersionCompatible(clientVersion));
    }
    
    SECTION("Version incompatibility - different major version") {
        uint32_t serverVersion = Protocol::getProtocolVersion();
        uint32_t clientVersion = ((serverVersion >> 16) + 1) << 16;  // Different major
        REQUIRE_FALSE(Protocol::isVersionCompatible(clientVersion));
    }
}

TEST_CASE("EntityStateData comparison", "[network]") {
    SECTION("Position equality") {
        Protocol::EntityStateData a;
        a.position = Position::fromVec3(glm::vec3(100.0f, 50.0f, 200.0f));
        
        Protocol::EntityStateData b;
        b.position = Position::fromVec3(glm::vec3(100.0f, 50.0f, 200.0f));
        
        Protocol::EntityStateData c;
        c.position = Position::fromVec3(glm::vec3(100.5f, 50.0f, 200.0f));  // Different by 50cm
        
        REQUIRE(a.equalsPosition(b));
        REQUIRE_FALSE(a.equalsPosition(c));
    }
    
    SECTION("Rotation equality") {
        Protocol::EntityStateData a;
        a.rotation.yaw = 1.5f;
        a.rotation.pitch = 0.3f;
        
        Protocol::EntityStateData b;
        b.rotation.yaw = 1.5f;
        b.rotation.pitch = 0.3f;
        
        Protocol::EntityStateData c;
        c.rotation.yaw = 1.6f;  // Different
        c.rotation.pitch = 0.3f;
        
        REQUIRE(a.equalsRotation(b));
        REQUIRE_FALSE(a.equalsRotation(c));
    }
}

TEST_CASE("Delta compression - Full snapshot", "[network]") {
    SECTION("Create full snapshot with no baseline") {
        std::vector<Protocol::EntityStateData> entities;
        
        Protocol::EntityStateData entity1;
        entity1.entity = static_cast<EntityID>(1);
        entity1.entityType = 0;  // Player
        entity1.position = Position::fromVec3(glm::vec3(100.0f, 0.0f, 200.0f));
        entity1.rotation.yaw = 1.57f;
        entity1.velocity.dx = Constants::Fixed(100);
        entity1.healthPercent = 100;
        entity1.animState = 1;  // Walk
        entities.push_back(entity1);
        
        Protocol::EntityStateData entity2;
        entity2.entity = static_cast<EntityID>(2);
        entity2.entityType = 0;
        entity2.position = Position::fromVec3(glm::vec3(150.0f, 0.0f, 250.0f));
        entity2.rotation.yaw = 3.14f;
        entity2.velocity.dx = Constants::Fixed(-50);
        entity2.healthPercent = 75;
        entity2.animState = 0;  // Idle
        entities.push_back(entity2);
        
        std::vector<EntityID> removed;
        
        // Create full snapshot (no baseline)
        auto snapshot = Protocol::createDeltaSnapshot(
            1000,  // serverTick
            1000,  // baselineTick (same = full)
            entities,
            removed,
            {}     // no baseline
        );
        
        REQUIRE_FALSE(snapshot.empty());
        
        // Verify and deserialize
        std::vector<Protocol::EntityStateData> outEntities;
        uint32_t outTick, outBaselineTick;
        std::vector<EntityID> outRemoved;
        
        bool success = Protocol::applyDeltaSnapshot(
            snapshot, outEntities, outTick, outBaselineTick, outRemoved
        );
        
        REQUIRE(success);
        REQUIRE(outTick == 1000);
        REQUIRE(outBaselineTick == 1000);
        REQUIRE(outEntities.size() == 2);
        REQUIRE(outRemoved.empty());
    }
}

TEST_CASE("Delta compression - Delta snapshot", "[network]") {
    SECTION("Delta with changed entities") {
        // Baseline entities
        std::vector<Protocol::EntityStateData> baseline;
        
        Protocol::EntityStateData baseEntity;
        baseEntity.entity = static_cast<EntityID>(1);
        baseEntity.entityType = 0;
        baseEntity.position = Position::fromVec3(glm::vec3(100.0f, 0.0f, 200.0f));
        baseEntity.rotation.yaw = 1.57f;
        baseEntity.velocity.dx = Constants::Fixed(100);
        baseEntity.healthPercent = 100;
        baseEntity.animState = 1;
        baseline.push_back(baseEntity);
        
        // Current entities - position changed
        std::vector<Protocol::EntityStateData> current;
        Protocol::EntityStateData currentEntity;
        currentEntity.entity = static_cast<EntityID>(1);
        currentEntity.entityType = 0;
        currentEntity.position = Position::fromVec3(glm::vec3(105.0f, 0.0f, 205.0f));  // Changed
        currentEntity.rotation.yaw = 1.57f;  // Same
        currentEntity.velocity.dx = Constants::Fixed(100);  // Same
        currentEntity.healthPercent = 100;  // Same
        currentEntity.animState = 1;  // Same
        current.push_back(currentEntity);
        
        std::vector<EntityID> removed;
        
        auto deltaSnapshot = Protocol::createDeltaSnapshot(
            1010,  // serverTick
            1000,  // baselineTick
            current,
            removed,
            baseline
        );
        
        REQUIRE_FALSE(deltaSnapshot.empty());
        
        // Deserialize and verify
        std::vector<Protocol::EntityStateData> outEntities;
        uint32_t outTick, outBaselineTick;
        std::vector<EntityID> outRemoved;
        
        bool success = Protocol::applyDeltaSnapshot(
            deltaSnapshot, outEntities, outTick, outBaselineTick, outRemoved
        );
        
        REQUIRE(success);
        REQUIRE(outTick == 1010);
        REQUIRE(outBaselineTick == 1000);
        REQUIRE(outEntities.size() == 1);
    }
    
    SECTION("Delta with removed entities") {
        std::vector<Protocol::EntityStateData> baseline;
        std::vector<Protocol::EntityStateData> current;
        
        // Baseline has one entity
        Protocol::EntityStateData baseEntity;
        baseEntity.entity = static_cast<EntityID>(1);
        baseline.push_back(baseEntity);
        
        // Current has no entities (removed)
        std::vector<EntityID> removed = {static_cast<EntityID>(1)};
        
        auto deltaSnapshot = Protocol::createDeltaSnapshot(
            1020,
            1010,
            current,
            removed,
            baseline
        );
        
        std::vector<Protocol::EntityStateData> outEntities;
        uint32_t outTick, outBaselineTick;
        std::vector<EntityID> outRemoved;
        
        bool success = Protocol::applyDeltaSnapshot(
            deltaSnapshot, outEntities, outTick, outBaselineTick, outRemoved
        );
        
        REQUIRE(success);
        REQUIRE(outEntities.empty());
        REQUIRE(outRemoved.size() == 1);
        REQUIRE(outRemoved[0] == static_cast<EntityID>(1));
    }
}

TEST_CASE("Server correction serialization", "[network]") {
    SECTION("Serialize and deserialize correction") {
        Position pos = Position::fromVec3(glm::vec3(100.0f, 50.0f, 200.0f));
        Velocity vel;
        vel.dx = Constants::Fixed(500);
        vel.dy = Constants::Fixed(0);
        vel.dz = Constants::Fixed(300);
        
        auto data = Protocol::serializeCorrection(1500, pos, vel, 1234);
        REQUIRE_FALSE(data.empty());
        
        // Verify with FlatBuffers
        flatbuffers::Verifier verifier(data.data(), data.size());
        REQUIRE(DarkAges::Protocol::VerifyServerCorrectionBuffer(verifier));
        
        auto correction = DarkAges::Protocol::GetServerCorrection(data.data());
        REQUIRE(correction->server_tick() == 1500);
        REQUIRE(correction->last_processed_input() == 1234);
        REQUIRE(correction->position() != nullptr);
        REQUIRE(correction->velocity() != nullptr);
    }
}

TEST_CASE("FlatBuffers schema verification", "[network]") {
    SECTION("ClientInput structure") {
        InputState input;
        input.forward = 1;
        input.yaw = 1.0f;
        input.pitch = 0.5f;
        input.sequence = 42;
        input.timestamp_ms = 1000;
        
        auto data = Protocol::serializeInput(input);
        
        flatbuffers::Verifier verifier(data.data(), data.size());
        REQUIRE(DarkAges::Protocol::VerifyClientInputBuffer(verifier));
        
        auto fbInput = DarkAges::Protocol::GetClientInput(data.data());
        REQUIRE(fbInput->sequence() == 42);
        REQUIRE(fbInput->timestamp() == 1000);
        REQUIRE((fbInput->input_flags() & 0x01) != 0);  // forward flag
    }
}

TEST_CASE("Protocol serialization performance", "[!benchmark][network]") {
    // Prepare test data
    InputState input;
    input.forward = 1;
    input.left = 1;
    input.yaw = 1.57f;
    input.pitch = 0.5f;
    input.sequence = 12345;
    input.timestamp_ms = 1000000;
    
    BENCHMARK("serializeInput") {
        return Protocol::serializeInput(input);
    };
    
    auto data = Protocol::serializeInput(input);
    InputState output;
    
    BENCHMARK("deserializeInput") {
        return Protocol::deserializeInput(data, output);
    };
}

TEST_CASE("Connection quality tracking", "[network]") {
    ConnectionQuality quality;
    
    SECTION("Default values") {
        REQUIRE(quality.rttMs == 0);
        REQUIRE(quality.packetLoss == 0.0f);
        REQUIRE(quality.packetsSent == 0);
        REQUIRE(quality.packetsReceived == 0);
    }
}

TEST_CASE("NetworkManager lifecycle", "[network]") {
    NetworkManager net;
    
    SECTION("Initialize and shutdown") {
        REQUIRE(net.getConnectionCount() == 0);
        REQUIRE(net.getTotalBytesSent() == 0);
        REQUIRE(net.getTotalBytesReceived() == 0);
        
        bool success = net.initialize(9999);  // Different port to avoid conflicts
        REQUIRE(success);
        
        net.shutdown();
    }
}

TEST_CASE("Packet type enumeration", "[network]") {
    // Ensure packet types have expected values for protocol compatibility
    REQUIRE(static_cast<uint8_t>(PacketType::ClientInput) == 1);
    REQUIRE(static_cast<uint8_t>(PacketType::ServerSnapshot) == 2);
    REQUIRE(static_cast<uint8_t>(PacketType::ReliableEvent) == 3);
    REQUIRE(static_cast<uint8_t>(PacketType::Ping) == 4);
    REQUIRE(static_cast<uint8_t>(PacketType::Handshake) == 5);
    REQUIRE(static_cast<uint8_t>(PacketType::Disconnect) == 6);
}

TEST_CASE("Delta compression bandwidth savings", "[network]") {
    // Create baseline with multiple entities
    std::vector<Protocol::EntityStateData> baseline;
    for (int i = 0; i < 10; i++) {
        Protocol::EntityStateData entity;
        entity.entity = static_cast<EntityID>(i + 1);
        entity.entityType = 0;
        entity.position = Position::fromVec3(glm::vec3(i * 10.0f, 0.0f, i * 10.0f));
        entity.rotation.yaw = static_cast<float>(i);
        entity.velocity.dx = Constants::Fixed(100);
        entity.healthPercent = 100;
        entity.animState = 1;
        baseline.push_back(entity);
    }
    
    // Create current - only first entity moved slightly
    std::vector<Protocol::EntityStateData> current = baseline;
    current[0].position.x += Constants::Fixed(10);  // Small position change
    
    std::vector<EntityID> removed;
    
    // Full snapshot size
    auto fullSnapshot = Protocol::createDeltaSnapshot(100, 100, baseline, removed, {});
    
    // Delta snapshot size
    auto deltaSnapshot = Protocol::createDeltaSnapshot(101, 100, current, removed, baseline);
    
    // Delta should be significantly smaller
    REQUIRE(deltaSnapshot.size() < fullSnapshot.size() / 2);
    
    // Verify we can decompress
    std::vector<Protocol::EntityStateData> outEntities;
    uint32_t outTick, outBaselineTick;
    std::vector<EntityID> outRemoved;
    
    bool success = Protocol::applyDeltaSnapshot(
        deltaSnapshot, outEntities, outTick, outBaselineTick, outRemoved
    );
    
    REQUIRE(success);
    REQUIRE(outEntities.size() == 1);  // Only changed entity
}
