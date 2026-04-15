// [SECURITY_AGENT] Unit tests for AntiCheat no-clip detection

#include <catch2/catch_test_macros.hpp>
#include "security/AntiCheat.hpp"
#include "physics/SpatialHash.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>

using namespace DarkAges;
using namespace DarkAges::Security;

// Helper: test no-clip detection through validateMovement (public API)
static CheatDetectionResult testNoClip(AntiCheatSystem& antiCheat, EntityID entity,
    const Position& oldPos, const Position& newPos, Registry& registry) {
    InputState input;  // Default input
    return antiCheat.validateMovement(entity, oldPos, newPos, 1000, input, registry);
}

TEST_CASE("AntiCheat no-clip graceful degradation", "[anticheat][noclip]") {
    SECTION("Returns clean when spatial hash is null") {
        AntiCheatSystem antiCheat;
        antiCheat.initialize();
        Registry registry;

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});
        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(5.0f, 0.0f, 0.0f), 0);

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        // No spatial hash set, so no-clip detection is skipped
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Returns clean with spatial hash but no static geometry") {
        AntiCheatSystem antiCheat;
        antiCheat.initialize();
        Registry registry;
        SpatialHash spatialHash;

        antiCheat.setSpatialHash(&spatialHash);

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});
        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(5.0f, 0.0f, 0.0f), 0);

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        REQUIRE_FALSE(result.detected);
    }
}

TEST_CASE("AntiCheat no-clip with static geometry", "[anticheat][noclip]") {
    AntiCheatSystem antiCheat;
    antiCheat.initialize();
    Registry registry;
    SpatialHash spatialHash;
    antiCheat.setSpatialHash(&spatialHash);

    SECTION("Clean movement when far from static geometry") {
        // Create a wall at x=10, z=0
        EntityID wall = registry.create();
        Position wallPos = Position::fromVec3(glm::vec3(10.0f, 0.0f, 0.0f), 0);
        registry.emplace<Position>(wall, wallPos);
        registry.emplace<BoundingVolume>(wall, 2.0f, 3.0f);
        registry.emplace<StaticTag>(wall);
        spatialHash.insert(wall, wallPos);

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});

        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(5.0f, 0.0f, 0.0f), 0);

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Detects no-clip when entity overlaps static geometry") {
        // Create a wall at x=3, z=0 with radius=2
        EntityID wall = registry.create();
        Position wallPos = Position::fromVec3(glm::vec3(3.0f, 0.0f, 0.0f), 0);
        registry.emplace<Position>(wall, wallPos);
        registry.emplace<BoundingVolume>(wall, 2.0f, 3.0f);
        registry.emplace<StaticTag>(wall);
        spatialHash.insert(wall, wallPos);

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});
        registry.emplace<BoundingVolume>(player, 0.5f, 1.8f);

        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(3.0f, 0.0f, 0.0f), 0);  // Inside wall

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::NO_CLIP);
    }

    SECTION("No false positive when Y ranges don't overlap") {
        // Create a wall high up
        EntityID wall = registry.create();
        Position wallPos = Position::fromVec3(glm::vec3(2.5f, 10.0f, 0.0f), 0);
        registry.emplace<Position>(wall, wallPos);
        registry.emplace<BoundingVolume>(wall, 2.0f, 3.0f);
        registry.emplace<StaticTag>(wall);
        spatialHash.insert(wall, wallPos);

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});

        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(2.5f, 0.0f, 0.0f), 0);  // XZ overlap but Y separate

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        REQUIRE_FALSE(result.detected);
    }

    SECTION("Corrects position to oldPos on no-clip") {
        EntityID wall = registry.create();
        Position wallPos = Position::fromVec3(glm::vec3(2.5f, 0.0f, 0.0f), 0);
        registry.emplace<Position>(wall, wallPos);
        registry.emplace<BoundingVolume>(wall, 2.0f, 3.0f);
        registry.emplace<StaticTag>(wall);
        spatialHash.insert(wall, wallPos);

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});

        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(2.5f, 0.0f, 0.0f), 0);

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        REQUIRE(result.detected);
        REQUIRE(result.correctedPosition.x == oldPos.x);
        REQUIRE(result.correctedPosition.z == oldPos.z);
    }
}

TEST_CASE("AntiCheat no-clip multiple static entities", "[anticheat][noclip]") {
    AntiCheatSystem antiCheat;
    antiCheat.initialize();
    Registry registry;
    SpatialHash spatialHash;
    antiCheat.setSpatialHash(&spatialHash);

    SECTION("Detects collision with one of multiple static entities") {
        // Two walls
        EntityID wall1 = registry.create();
        registry.emplace<Position>(wall1, Position::fromVec3(glm::vec3(3.0f, 0.0f, 0.0f), 0));
        registry.emplace<BoundingVolume>(wall1, 1.0f, 3.0f);
        registry.emplace<StaticTag>(wall1);
        spatialHash.insert(wall1, Position::fromVec3(glm::vec3(3.0f, 0.0f, 0.0f), 0));

        EntityID wall2 = registry.create();
        registry.emplace<Position>(wall2, Position::fromVec3(glm::vec3(7.0f, 0.0f, 0.0f), 0));
        registry.emplace<BoundingVolume>(wall2, 1.0f, 3.0f);
        registry.emplace<StaticTag>(wall2);
        spatialHash.insert(wall2, Position::fromVec3(glm::vec3(7.0f, 0.0f, 0.0f), 0));

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});

        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(3.0f, 0.0f, 0.5f), 0);

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        REQUIRE(result.detected);
        REQUIRE(result.type == CheatType::NO_CLIP);
    }

    SECTION("Clean when between two walls") {
        EntityID wall1 = registry.create();
        registry.emplace<Position>(wall1, Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0));
        registry.emplace<BoundingVolume>(wall1, 1.0f, 3.0f);
        registry.emplace<StaticTag>(wall1);
        spatialHash.insert(wall1, Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0));

        EntityID wall2 = registry.create();
        registry.emplace<Position>(wall2, Position::fromVec3(glm::vec3(10.0f, 0.0f, 0.0f), 0));
        registry.emplace<BoundingVolume>(wall2, 1.0f, 3.0f);
        registry.emplace<StaticTag>(wall2);
        spatialHash.insert(wall2, Position::fromVec3(glm::vec3(10.0f, 0.0f, 0.0f), 0));

        EntityID player = registry.create();
        registry.emplace<PlayerInfo>(player, PlayerInfo{1, 0, "test", 0});

        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(5.0f, 0.0f, 0.0f), 0);  // Between walls

        auto result = testNoClip(antiCheat, player, oldPos, newPos, registry);
        REQUIRE_FALSE(result.detected);
    }
}
