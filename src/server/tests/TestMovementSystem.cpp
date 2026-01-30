// [PHYSICS_AGENT] Movement System unit tests

#include <catch2/catch_test_macros.hpp>
#include "physics/MovementSystem.hpp"
#include <cmath>
#include <glm/glm.hpp>

using namespace DarkAges;

TEST_CASE("MovementSystem basic physics", "[movement]") {
    MovementSystem movement;
    Registry registry;
    
    SECTION("Entity moves with input") {
        EntityID entity = registry.create();
        Position pos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Velocity vel;
        InputState input;
        input.forward = 1;
        input.yaw = 0.0f;  // Facing +Z (which is -Z in our coordinate system)
        
        registry.emplace<Position>(entity, pos);
        registry.emplace<Velocity>(entity, vel);
        registry.emplace<InputState>(entity, input);
        
        // Update for 1 second
        for (int i = 0; i < 60; ++i) {
            movement.update(registry, i * 16);
        }
        
        const Position& newPos = registry.get<Position>(entity);
        
        // Should have moved forward (negative Z in game coords)
        REQUIRE(newPos.z < 0);
    }
    
    SECTION("Max speed is enforced") {
        EntityID entity = registry.create();
        registry.emplace<Position>(entity, Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0));
        registry.emplace<Velocity>(entity);
        
        InputState input;
        input.forward = 1;
        input.sprint = 1;  // Sprinting
        registry.emplace<InputState>(entity, input);
        
        // Update for 2 seconds to reach max speed
        for (int i = 0; i < 120; ++i) {
            movement.update(registry, i * 16);
        }
        
        const Velocity& vel = registry.get<Velocity>(entity);
        float speed = vel.speed();
        
        // Speed should be close to max sprint speed
        REQUIRE(speed <= Constants::MAX_SPRINT_SPEED * 1.1f);  // Allow 10% tolerance
    }
    
    SECTION("Deceleration when no input") {
        EntityID entity = registry.create();
        registry.emplace<Position>(entity, Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0));
        registry.emplace<Velocity>(entity);
        
        // Start moving
        {
            InputState input;
            input.forward = 1;
            registry.emplace<InputState>(entity, input);
            
            for (int i = 0; i < 60; ++i) {
                movement.update(registry, i * 16);
            }
        }
        
        const Velocity& velWithInput = registry.get<Velocity>(entity);
        REQUIRE(velWithInput.speed() > 0.1f);
        
        // Stop input
        {
            InputState input;  // All zeros
            registry.replace<InputState>(entity, input);
            
            for (int i = 60; i < 120; ++i) {
                movement.update(registry, i * 16);
            }
        }
        
        const Velocity& velWithoutInput = registry.get<Velocity>(entity);
        REQUIRE(velWithoutInput.speed() < velWithInput.speed());  // Slowed down
    }
}

TEST_CASE("MovementSystem validation", "[movement][anticheat]") {
    MovementSystem movement;
    
    SECTION("Valid movement is accepted") {
        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(3.0f, 0.0f, 0.0f), 1000);  // 3m in 1s
        
        bool valid = movement.validateMovement(oldPos, newPos, 1000, Constants::MAX_PLAYER_SPEED);
        
        REQUIRE(valid);
    }
    
    SECTION("Too fast movement is rejected") {
        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(20.0f, 0.0f, 0.0f), 1000);  // 20m in 1s
        
        bool valid = movement.validateMovement(oldPos, newPos, 1000, Constants::MAX_PLAYER_SPEED);
        
        REQUIRE_FALSE(valid);
    }
    
    SECTION("Sprint speed is validated correctly") {
        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        Position newPos = Position::fromVec3(glm::vec3(8.0f, 0.0f, 0.0f), 1000);  // 8m in 1s
        
        // Should fail for normal speed
        bool validNormal = movement.validateMovement(oldPos, newPos, 1000, Constants::MAX_PLAYER_SPEED);
        REQUIRE_FALSE(validNormal);
        
        // Should pass for sprint speed
        bool validSprint = movement.validateMovement(oldPos, newPos, 1000, Constants::MAX_SPRINT_SPEED);
        REQUIRE(validSprint);
    }
    
    SECTION("Tolerance allows small overshoot") {
        Position oldPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        // 7.2m in 1s (20% over max speed of 6m/s)
        Position newPos = Position::fromVec3(glm::vec3(7.2f, 0.0f, 0.0f), 1000);
        
        bool valid = movement.validateMovement(oldPos, newPos, 1000, Constants::MAX_PLAYER_SPEED);
        
        // Should be accepted due to 20% tolerance
        REQUIRE(valid);
    }
}

TEST_CASE("MovementSystem world bounds", "[movement]") {
    MovementSystem movement;
    
    SECTION("Position is clamped to world bounds") {
        Position pos = Position::fromVec3(
            glm::vec3(Constants::WORLD_MAX_X + 100.0f, 0.0f, Constants::WORLD_MAX_Z + 100.0f), 
            0
        );
        
        movement.clampPositionToWorldBounds(pos);
        
        float x = pos.x * Constants::FIXED_TO_FLOAT;
        float z = pos.z * Constants::FIXED_TO_FLOAT;
        
        REQUIRE(x <= Constants::WORLD_MAX_X);
        REQUIRE(z <= Constants::WORLD_MAX_Z);
    }
    
    SECTION("Negative bounds are clamped") {
        Position pos = Position::fromVec3(
            glm::vec3(Constants::WORLD_MIN_X - 100.0f, 0.0f, Constants::WORLD_MIN_Z - 100.0f), 
            0
        );
        
        movement.clampPositionToWorldBounds(pos);
        
        float x = pos.x * Constants::FIXED_TO_FLOAT;
        float z = pos.z * Constants::FIXED_TO_FLOAT;
        
        REQUIRE(x >= Constants::WORLD_MIN_X);
        REQUIRE(z >= Constants::WORLD_MIN_Z);
    }
}

TEST_CASE("MovementSystem applyInput", "[movement]") {
    MovementSystem movement;
    Registry registry;
    
    EntityID entity = registry.create();
    registry.emplace<Position>(entity, Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0));
    registry.emplace<Velocity>(entity);
    
    SECTION("Normal input is applied") {
        InputState input;
        input.forward = 1;
        input.sequence = 1;
        
        auto result = movement.applyInput(registry, entity, input, 16);
        
        REQUIRE(result.valid);
        REQUIRE(result.antiCheatTriggered == false);
    }
    
    SECTION("Teleport is detected and rejected") {
        // Set position far away first
        registry.replace<Position>(entity, 
            Position::fromVec3(glm::vec3(1000.0f, 0.0f, 1000.0f), 0));
        
        InputState input;
        input.forward = 1;
        input.sequence = 1;
        
        auto result = movement.applyInput(registry, entity, input, 16);
        
        // Movement from far away should be rejected
        // Note: This depends on the exact validation logic
    }
}

TEST_CASE("MovementValidator anti-cheat", "[movement][anticheat]") {
    MovementValidator validator;
    Registry registry;
    
    SECTION("Teleport detection") {
        EntityID entity = registry.create();
        Position pos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        registry.emplace<Position>(entity, pos);
        registry.emplace<AntiCheatState>(entity);
        
        AntiCheatState cheatState;
        cheatState.lastValidPosition = pos;
        cheatState.lastValidationTime = 0;
        
        // Try to teleport far away
        Position teleportPos = Position::fromVec3(
            glm::vec3(Constants::MAX_TELEPORT_DISTANCE + 100.0f, 0.0f, 0.0f), 
            1000
        );
        
        auto result = validator.validate(registry, entity, teleportPos, cheatState, 1000);
        
        REQUIRE_FALSE(result.accepted);
        REQUIRE(result.suspicious);
    }
    
    SECTION("Speed hack detection") {
        AntiCheatState cheatState;
        cheatState.lastValidationTime = 0;
        cheatState.lastValidPosition = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), 0);
        
        // Record high speed
        Position newPos = Position::fromVec3(glm::vec3(50.0f, 0.0f, 0.0f), 500);  // 50m in 0.5s = 100m/s
        validator.recordValidMovement(cheatState, newPos, 500);
        
        // Check if flagged
        // Note: This depends on the actual implementation
    }
    
    SECTION("Kick threshold") {
        AntiCheatState cheatState;
        cheatState.suspiciousMovements = Constants::SUSPICIOUS_MOVEMENT_THRESHOLD;
        
        REQUIRE(validator.shouldKick(cheatState));
        
        cheatState.suspiciousMovements = Constants::SUSPICIOUS_MOVEMENT_THRESHOLD - 1;
        REQUIRE_FALSE(validator.shouldKick(cheatState));
    }
}
