// [ZONE_AGENT] Entity Migration System Tests
// Tests for seamless entity migration between zones

#include <catch2/catch_test_macros.hpp>
#include "zones/EntityMigration.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace DarkAges;

// ============================================================================
// EntitySnapshot Serialization Tests
// ============================================================================

TEST_CASE("EntitySnapshot serialization and deserialization", "[migration]") {
    EntitySnapshot original;
    original.entity = static_cast<entt::entity>(42);
    original.playerId = 12345;
    original.position = Position{1000, 2000, 3000, 1000};
    original.velocity = Velocity{100, 200, 300};
    original.rotation = Rotation{1.57f, 0.5f};
    original.combat = CombatState{8000, 10000, 1, 2, entt::null, 0, false};
    original.network = NetworkState{100, 1000, 50, 0.0f, 1};
    original.lastInput = InputState{};
    original.lastInput.forward = 1;
    original.lastInput.sequence = 999;
    original.antiCheat = AntiCheatState{};
    original.antiCheat.suspiciousMovements = 2;
    original.sourceZoneId = 1;
    original.targetZoneId = 2;
    original.timestamp = 5000;
    original.sequence = 7;
    original.connectionId = 42;
    
    SECTION("Serialize and deserialize preserves all data") {
        auto data = original.serialize();
        REQUIRE(!data.empty());
        
        auto result = EntitySnapshot::deserialize(data);
        REQUIRE(result.has_value());
        
        EntitySnapshot& restored = *result;
        REQUIRE(restored.entity == original.entity);
        REQUIRE(restored.playerId == original.playerId);
        REQUIRE(restored.position.x == original.position.x);
        REQUIRE(restored.position.y == original.position.y);
        REQUIRE(restored.position.z == original.position.z);
        REQUIRE(restored.velocity.dx == original.velocity.dx);
        REQUIRE(restored.rotation.yaw == original.rotation.yaw);
        REQUIRE(restored.combat.health == original.combat.health);
        REQUIRE(restored.network.lastInputSequence == original.network.lastInputSequence);
        REQUIRE(restored.lastInput.sequence == original.lastInput.sequence);
        REQUIRE(restored.antiCheat.suspiciousMovements == original.antiCheat.suspiciousMovements);
        REQUIRE(restored.sourceZoneId == original.sourceZoneId);
        REQUIRE(restored.targetZoneId == original.targetZoneId);
        REQUIRE(restored.sequence == original.sequence);
        REQUIRE(restored.connectionId == original.connectionId);
    }
    
    SECTION("Deserialize rejects invalid data") {
        std::vector<uint8_t> invalidData = {0x00, 0x01, 0x02, 0x03};
        auto result = EntitySnapshot::deserialize(invalidData);
        REQUIRE(!result.has_value());
    }
    
    SECTION("Deserialize rejects data with wrong magic") {
        auto data = original.serialize();
        // Corrupt magic
        data[0] = 0xFF;
        data[1] = 0xFF;
        data[2] = 0xFF;
        data[3] = 0xFF;
        
        auto result = EntitySnapshot::deserialize(data);
        REQUIRE(!result.has_value());
    }
}

// ============================================================================
// EntityMigrationManager Tests
// ============================================================================

TEST_CASE("EntityMigrationManager basic operations", "[migration]") {
    Registry registry;
    
    // Create a mock Redis connection (nullptr for testing)
    RedisManager* redis = nullptr;
    
    EntityMigrationManager manager(1, redis);  // Zone 1
    
    SECTION("Initial state is empty") {
        REQUIRE(manager.getActiveMigrationCount() == 0);
        REQUIRE(!manager.isMigrating(static_cast<entt::entity>(1)));
        REQUIRE(manager.getMigrationState(static_cast<entt::entity>(1)) == MigrationState::NONE);
    }
    
    SECTION("Can set migration timeout") {
        manager.setMigrationTimeout(10000);  // 10 seconds
        // No direct way to verify, but it shouldn't crash
        SUCCEED();
    }
    
    SECTION("Cannot initiate migration for non-existent entity") {
        bool result = manager.initiateMigration(static_cast<entt::entity>(999), 2, registry);
        REQUIRE(!result);
        REQUIRE(manager.getActiveMigrationCount() == 0);
    }
}

TEST_CASE("EntityMigrationManager migration lifecycle", "[migration]") {
    Registry registry;
    RedisManager* redis = nullptr;
    EntityMigrationManager manager(1, redis);
    
    // Create a test entity
    EntityID entity = registry.create();
    registry.emplace<Position>(entity, Position{1000, 0, 1000, 0});
    registry.emplace<Velocity>(entity);
    registry.emplace<Rotation>(entity);
    registry.emplace<CombatState>(entity);
    registry.emplace<NetworkState>(entity);
    registry.emplace<InputState>(entity);
    registry.emplace<AntiCheatState>(entity);
    PlayerInfo& info = registry.emplace<PlayerInfo>(entity);
    info.playerId = 12345;
    info.connectionId = 42;
    
    SECTION("Can initiate migration for valid entity") {
        bool result = manager.initiateMigration(entity, 2, registry);
        REQUIRE(result);
        REQUIRE(manager.getActiveMigrationCount() == 1);
        REQUIRE(manager.isMigrating(entity));
        REQUIRE(manager.getMigrationState(entity) == MigrationState::PREPARING);
    }
    
    SECTION("Cannot start duplicate migration") {
        bool result1 = manager.initiateMigration(entity, 2, registry);
        REQUIRE(result1);
        
        bool result2 = manager.initiateMigration(entity, 3, registry);
        REQUIRE(!result2);
        
        REQUIRE(manager.getActiveMigrationCount() == 1);
    }
    
    SECTION("Can cancel migration") {
        manager.initiateMigration(entity, 2, registry);
        REQUIRE(manager.isMigrating(entity));
        
        bool cancelled = manager.cancelMigration(entity);
        REQUIRE(cancelled);
        
        // Note: migration record stays until update() processes it
        REQUIRE(manager.getActiveMigrationCount() == 1);
        
        // Update to process the cancelled state
        manager.update(registry, 1000);
        REQUIRE(manager.getActiveMigrationCount() == 0);
    }
    
    SECTION("Cancel non-existent migration returns false") {
        bool cancelled = manager.cancelMigration(static_cast<entt::entity>(999));
        REQUIRE(!cancelled);
    }
}

TEST_CASE("EntityMigrationManager state machine transitions", "[migration]") {
    Registry registry;
    RedisManager* redis = nullptr;
    EntityMigrationManager manager(1, redis);
    
    // Create a test entity with all components
    EntityID entity = registry.create();
    registry.emplace<Position>(entity, Position{1000, 0, 1000, 0});
    registry.emplace<Velocity>(entity, Velocity{100, 0, 100});
    registry.emplace<Rotation>(entity, Rotation{3.14f, 0.0f});
    registry.emplace<CombatState>(entity, CombatState{7500, 10000, 1, 1, entt::null, 0, false});
    registry.emplace<NetworkState>(entity, NetworkState{50, 500, 25, 0.0f, 5});
    registry.emplace<InputState>(entity);
    registry.emplace<AntiCheatState>(entity);
    PlayerInfo& info = registry.emplace<PlayerInfo>(entity);
    info.playerId = 99999;
    info.connectionId = 100;
    
    bool callbackCalled = false;
    bool callbackSuccess = false;
    
    auto callback = [&callbackCalled, &callbackSuccess](EntityID, bool success) {
        callbackCalled = true;
        callbackSuccess = success;
    };
    
    SECTION("Migration progresses through state machine") {
        // Initiate migration
        bool started = manager.initiateMigration(entity, 2, registry, callback);
        REQUIRE(started);
        REQUIRE(manager.getMigrationState(entity) == MigrationState::PREPARING);
        
        // First update: PREPARING -> TRANSFERRING
        manager.update(registry, 0);
        REQUIRE(manager.getMigrationState(entity) == MigrationState::TRANSFERRING);
        
        // Entity should still exist in registry (source zone)
        REQUIRE(registry.valid(entity));
    }
    
    SECTION("Migration stats are tracked") {
        REQUIRE(manager.getStats().totalMigrations == 0);
        
        // Start and process a migration
        manager.initiateMigration(entity, 2, registry);
        manager.update(registry, 0);  // PREPARING
        
        // Stats only updated on completion/failure
        REQUIRE(manager.getStats().totalMigrations == 0);
    }
}

TEST_CASE("EntityMigrationManager handles multiple entities", "[migration]") {
    Registry registry;
    RedisManager* redis = nullptr;
    EntityMigrationManager manager(1, redis);
    
    // Create multiple entities
    std::vector<EntityID> entities;
    for (int i = 0; i < 5; ++i) {
        EntityID entity = registry.create();
        registry.emplace<Position>(entity);
        registry.emplace<Velocity>(entity);
        registry.emplace<Rotation>(entity);
        registry.emplace<CombatState>(entity);
        registry.emplace<NetworkState>(entity);
        registry.emplace<InputState>(entity);
        registry.emplace<AntiCheatState>(entity);
        entities.push_back(entity);
    }
    
    SECTION("Can track multiple concurrent migrations") {
        for (size_t i = 0; i < entities.size(); ++i) {
            bool result = manager.initiateMigration(entities[i], 
                                                     static_cast<uint32_t>(i + 2), 
                                                     registry);
            REQUIRE(result);
        }
        
        REQUIRE(manager.getActiveMigrationCount() == 5);
        
        // Verify each entity is tracked
        for (auto entity : entities) {
            REQUIRE(manager.isMigrating(entity));
        }
    }
    
    SECTION("Non-migrating entities return NONE state") {
        manager.initiateMigration(entities[0], 2, registry);
        
        // First entity is migrating
        REQUIRE(manager.isMigrating(entities[0]));
        
        // Others are not
        for (size_t i = 1; i < entities.size(); ++i) {
            REQUIRE(!manager.isMigrating(entities[i]));
            REQUIRE(manager.getMigrationState(entities[i]) == MigrationState::NONE);
        }
    }
}

TEST_CASE("EntitySnapshot preserves all component types", "[migration]") {
    EntitySnapshot snapshot;
    snapshot.entity = static_cast<entt::entity>(1);
    snapshot.playerId = 77777;
    
    // Set position with fixed-point values
    snapshot.position = Position{
        static_cast<Constants::Fixed>(123.456f * Constants::FLOAT_TO_FIXED),
        static_cast<Constants::Fixed>(50.0f * Constants::FLOAT_TO_FIXED),
        static_cast<Constants::Fixed>(-789.123f * Constants::FLOAT_TO_FIXED),
        1000
    };
    
    // Set velocity
    snapshot.velocity = Velocity{
        static_cast<Constants::Fixed>(5.5f * Constants::FLOAT_TO_FIXED),
        static_cast<Constants::Fixed>(0.0f),
        static_cast<Constants::Fixed>(-3.2f * Constants::FLOAT_TO_FIXED)
    };
    
    // Set rotation
    snapshot.rotation = Rotation{1.5708f, 0.0f};  // 90 degrees yaw
    
    // Set combat state
    snapshot.combat = CombatState{
        5000,    // health
        10000,   // maxHealth
        2,       // teamId
        3,       // classType
        static_cast<entt::entity>(99),  // lastAttacker
        500,     // lastAttackTime
        false    // isDead
    };
    
    // Set network state
    snapshot.network = NetworkState{
        123,     // lastInputSequence
        10000,   // lastInputTime
        45,      // rttMs
        0.01f,   // packetLoss
        10       // snapshotSequence
    };
    
    // Set input state
    snapshot.lastInput = InputState{};
    snapshot.lastInput.forward = 1;
    snapshot.lastInput.right = 1;
    snapshot.lastInput.yaw = 0.785f;  // 45 degrees
    snapshot.lastInput.sequence = 456;
    
    // Set anti-cheat state
    snapshot.antiCheat = AntiCheatState{};
    snapshot.antiCheat.suspiciousMovements = 1;
    snapshot.antiCheat.maxRecordedSpeed = 8.5f;
    snapshot.antiCheat.inputCount = 1000;
    
    // Metadata
    snapshot.sourceZoneId = 5;
    snapshot.targetZoneId = 6;
    snapshot.timestamp = 12345678;
    snapshot.sequence = 42;
    snapshot.connectionId = 999;
    
    SECTION("All fields survive serialization round-trip") {
        auto data = snapshot.serialize();
        auto result = EntitySnapshot::deserialize(data);
        REQUIRE(result.has_value());
        
        EntitySnapshot& restored = *result;
        
        // Identity
        REQUIRE(restored.entity == snapshot.entity);
        REQUIRE(restored.playerId == snapshot.playerId);
        
        // Transform
        REQUIRE(restored.position.x == snapshot.position.x);
        REQUIRE(restored.position.y == snapshot.position.y);
        REQUIRE(restored.position.z == snapshot.position.z);
        REQUIRE(restored.position.timestamp_ms == snapshot.position.timestamp_ms);
        REQUIRE(restored.velocity.dx == snapshot.velocity.dx);
        REQUIRE(restored.velocity.dy == snapshot.velocity.dy);
        REQUIRE(restored.velocity.dz == snapshot.velocity.dz);
        REQUIRE(restored.rotation.yaw == snapshot.rotation.yaw);
        REQUIRE(restored.rotation.pitch == snapshot.rotation.pitch);
        
        // Combat
        REQUIRE(restored.combat.health == snapshot.combat.health);
        REQUIRE(restored.combat.maxHealth == snapshot.combat.maxHealth);
        REQUIRE(restored.combat.teamId == snapshot.combat.teamId);
        REQUIRE(restored.combat.classType == snapshot.combat.classType);
        REQUIRE(restored.combat.lastAttacker == snapshot.combat.lastAttacker);
        REQUIRE(restored.combat.lastAttackTime == snapshot.combat.lastAttackTime);
        REQUIRE(restored.combat.isDead == snapshot.combat.isDead);
        
        // Network
        REQUIRE(restored.network.lastInputSequence == snapshot.network.lastInputSequence);
        REQUIRE(restored.network.lastInputTime == snapshot.network.lastInputTime);
        REQUIRE(restored.network.rttMs == snapshot.network.rttMs);
        REQUIRE(restored.network.packetLoss == snapshot.network.packetLoss);
        REQUIRE(restored.network.snapshotSequence == snapshot.network.snapshotSequence);
        
        // Input
        REQUIRE(restored.lastInput.forward == snapshot.lastInput.forward);
        REQUIRE(restored.lastInput.right == snapshot.lastInput.right);
        REQUIRE(restored.lastInput.yaw == snapshot.lastInput.yaw);
        REQUIRE(restored.lastInput.sequence == snapshot.lastInput.sequence);
        
        // Anti-cheat
        REQUIRE(restored.antiCheat.suspiciousMovements == snapshot.antiCheat.suspiciousMovements);
        REQUIRE(restored.antiCheat.maxRecordedSpeed == snapshot.antiCheat.maxRecordedSpeed);
        REQUIRE(restored.antiCheat.inputCount == snapshot.antiCheat.inputCount);
        
        // Metadata
        REQUIRE(restored.sourceZoneId == snapshot.sourceZoneId);
        REQUIRE(restored.targetZoneId == snapshot.targetZoneId);
        REQUIRE(restored.timestamp == snapshot.timestamp);
        REQUIRE(restored.sequence == snapshot.sequence);
        REQUIRE(restored.connectionId == snapshot.connectionId);
    }
}

TEST_CASE("EntityMigrationManager callbacks work correctly", "[migration]") {
    Registry registry;
    RedisManager* redis = nullptr;
    EntityMigrationManager manager(1, redis);
    
    EntityID entity = registry.create();
    registry.emplace<Position>(entity);
    registry.emplace<Velocity>(entity);
    registry.emplace<Rotation>(entity);
    registry.emplace<CombatState>(entity);
    registry.emplace<NetworkState>(entity);
    registry.emplace<InputState>(entity);
    registry.emplace<AntiCheatState>(entity);
    
    int successCount = 0;
    int failureCount = 0;
    EntityID callbackEntity = entt::null;
    
    auto callback = [&](EntityID e, bool success) {
        callbackEntity = e;
        if (success) {
            successCount++;
        } else {
            failureCount++;
        }
    };
    
    SECTION("Callback invoked on migration failure") {
        bool started = manager.initiateMigration(entity, 2, registry, callback);
        REQUIRE(started);
        
        // Cancel the migration
        manager.cancelMigration(entity);
        REQUIRE(failureCount == 0);  // Not called yet
        
        // Update to process cancellation
        manager.update(registry, 0);
        
        REQUIRE(failureCount == 1);
        REQUIRE(callbackEntity == entity);
    }
}

TEST_CASE("EntityMigrationManager timeout handling", "[migration]") {
    Registry registry;
    RedisManager* redis = nullptr;
    EntityMigrationManager manager(1, redis);
    manager.setMigrationTimeout(100);  // 100ms timeout for testing
    
    EntityID entity = registry.create();
    registry.emplace<Position>(entity);
    registry.emplace<Velocity>(entity);
    registry.emplace<Rotation>(entity);
    registry.emplace<CombatState>(entity);
    registry.emplace<NetworkState>(entity);
    registry.emplace<InputState>(entity);
    registry.emplace<AntiCheatState>(entity);
    
    bool failed = false;
    auto callback = [&failed](EntityID, bool success) {
        if (!success) failed = true;
    };
    
    SECTION("Migration times out after specified duration") {
        manager.initiateMigration(entity, 2, registry, callback);
        
        // Start the migration
        manager.update(registry, 0);  // PREPARING -> TRANSFERRING
        
        // Simulate time passing (before timeout)
        manager.update(registry, 50);  // 50ms elapsed
        REQUIRE(manager.isMigrating(entity));
        REQUIRE(!failed);
        
        // Simulate timeout
        manager.update(registry, 150);  // 150ms elapsed (past 100ms timeout)
        
        // Migration should be removed after update processes the failure
        REQUIRE(manager.getActiveMigrationCount() == 0);
        REQUIRE(failed);
        REQUIRE(manager.getStats().timeoutCount == 1);
    }
}
