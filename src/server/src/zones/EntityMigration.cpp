#include "zones/EntityMigration.hpp"
#include "ecs/Components.hpp"
#include "Constants.hpp"
#include <iostream>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DarkAges {

// ============================================================================
// EntitySnapshot Serialization
// ============================================================================

// Binary layout:
// Header (8 bytes):
//   - entity (4 bytes)
//   - playerId (8 bytes) - 0 for NPCs
//   - sourceZoneId (4 bytes)
//   - targetZoneId (4 bytes)
//   - timestamp (4 bytes)
//   - sequence (4 bytes)
//   - connectionId (4 bytes)
// Position (16 bytes):
//   - x, y, z (4 bytes each)
//   - timestamp_ms (4 bytes)
// Velocity (12 bytes):
//   - dx, dy, dz (4 bytes each)
// Rotation (8 bytes):
//   - yaw, pitch (4 bytes each)
// CombatState (16 bytes):
//   - health, maxHealth (2 bytes each)
//   - teamId, classType (1 byte each)
//   - lastAttacker (4 bytes)
//   - lastAttackTime (4 bytes)
//   - isDead (1 byte) + padding (1 byte)
// NetworkState (20 bytes):
//   - lastInputSequence (4 bytes)
//   - lastInputTime (4 bytes)
//   - rttMs (4 bytes)
//   - packetLoss (4 bytes)
//   - snapshotSequence (4 bytes)
// InputState (12 bytes):
//   - flags (1 byte)
//   - yaw, pitch (4 bytes each)
//   - sequence (4 bytes)
//   - timestamp_ms (4 bytes)
// AntiCheatState (28 bytes):
//   - lastValidPosition (16 bytes)
//   - lastValidationTime (4 bytes)
//   - suspiciousMovements (4 bytes)
//   - maxRecordedSpeed (4 bytes)
//   - inputCount (4 bytes)
//   - inputWindowStart (4 bytes)
// Total: ~140 bytes per entity snapshot

std::vector<uint8_t> EntitySnapshot::serialize() const {
    std::vector<uint8_t> data;
    data.reserve(160);  // Pre-allocate approximate size
    
    // Helper lambda for writing values
    auto writeBytes = [&data](const void* src, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(src);
        data.insert(data.end(), bytes, bytes + size);
    };
    
    // Header section
    writeBytes(&entity, sizeof(EntityID));
    writeBytes(&playerId, sizeof(uint64_t));
    writeBytes(&sourceZoneId, sizeof(uint32_t));
    writeBytes(&targetZoneId, sizeof(uint32_t));
    writeBytes(&timestamp, sizeof(uint32_t));
    writeBytes(&sequence, sizeof(uint32_t));
    writeBytes(&connectionId, sizeof(uint32_t));
    
    // Position
    writeBytes(&position.x, sizeof(Constants::Fixed));
    writeBytes(&position.y, sizeof(Constants::Fixed));
    writeBytes(&position.z, sizeof(Constants::Fixed));
    writeBytes(&position.timestamp_ms, sizeof(uint32_t));
    
    // Velocity
    writeBytes(&velocity.dx, sizeof(Constants::Fixed));
    writeBytes(&velocity.dy, sizeof(Constants::Fixed));
    writeBytes(&velocity.dz, sizeof(Constants::Fixed));
    
    // Rotation
    writeBytes(&rotation.yaw, sizeof(float));
    writeBytes(&rotation.pitch, sizeof(float));
    
    // CombatState
    writeBytes(&combat.health, sizeof(int16_t));
    writeBytes(&combat.maxHealth, sizeof(int16_t));
    writeBytes(&combat.teamId, sizeof(uint8_t));
    writeBytes(&combat.classType, sizeof(uint8_t));
    writeBytes(&combat.lastAttacker, sizeof(EntityID));
    writeBytes(&combat.lastAttackTime, sizeof(uint32_t));
    writeBytes(&combat.isDead, sizeof(bool));
    data.push_back(0);  // padding
    
    // NetworkState
    writeBytes(&network.lastInputSequence, sizeof(uint32_t));
    writeBytes(&network.lastInputTime, sizeof(uint32_t));
    writeBytes(&network.rttMs, sizeof(uint32_t));
    writeBytes(&network.packetLoss, sizeof(float));
    writeBytes(&network.snapshotSequence, sizeof(uint32_t));
    
    // InputState (as bytes for packing)
    uint8_t inputFlags = (lastInput.forward << 0) |
                         (lastInput.backward << 1) |
                         (lastInput.left << 2) |
                         (lastInput.right << 3) |
                         (lastInput.jump << 4) |
                         (lastInput.attack << 5) |
                         (lastInput.block << 6) |
                         (lastInput.sprint << 7);
    data.push_back(inputFlags);
    writeBytes(&lastInput.yaw, sizeof(float));
    writeBytes(&lastInput.pitch, sizeof(float));
    writeBytes(&lastInput.sequence, sizeof(uint32_t));
    writeBytes(&lastInput.timestamp_ms, sizeof(uint32_t));
    
    // AntiCheatState
    writeBytes(&antiCheat.lastValidPosition.x, sizeof(Constants::Fixed));
    writeBytes(&antiCheat.lastValidPosition.y, sizeof(Constants::Fixed));
    writeBytes(&antiCheat.lastValidPosition.z, sizeof(Constants::Fixed));
    writeBytes(&antiCheat.lastValidPosition.timestamp_ms, sizeof(uint32_t));
    writeBytes(&antiCheat.lastValidationTime, sizeof(uint32_t));
    writeBytes(&antiCheat.suspiciousMovements, sizeof(uint32_t));
    writeBytes(&antiCheat.maxRecordedSpeed, sizeof(float));
    writeBytes(&antiCheat.inputCount, sizeof(uint32_t));
    writeBytes(&antiCheat.inputWindowStart, sizeof(uint32_t));
    
    return data;
}

std::optional<EntitySnapshot> EntitySnapshot::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 100) {  // Minimum expected size
        return std::nullopt;
    }
    
    EntitySnapshot snapshot;
    size_t offset = 0;
    
    auto readBytes = [&data, &offset](void* dest, size_t size) -> bool {
        if (offset + size > data.size()) {
            return false;
        }
        std::memcpy(dest, &data[offset], size);
        offset += size;
        return true;
    };
    
    // Header section
    if (!readBytes(&snapshot.entity, sizeof(EntityID))) return std::nullopt;
    if (!readBytes(&snapshot.playerId, sizeof(uint64_t))) return std::nullopt;
    if (!readBytes(&snapshot.sourceZoneId, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.targetZoneId, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.timestamp, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.sequence, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.connectionId, sizeof(uint32_t))) return std::nullopt;
    
    // Position
    if (!readBytes(&snapshot.position.x, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.position.y, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.position.z, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.position.timestamp_ms, sizeof(uint32_t))) return std::nullopt;
    
    // Velocity
    if (!readBytes(&snapshot.velocity.dx, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.velocity.dy, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.velocity.dz, sizeof(Constants::Fixed))) return std::nullopt;
    
    // Rotation
    if (!readBytes(&snapshot.rotation.yaw, sizeof(float))) return std::nullopt;
    if (!readBytes(&snapshot.rotation.pitch, sizeof(float))) return std::nullopt;
    
    // CombatState
    if (!readBytes(&snapshot.combat.health, sizeof(int16_t))) return std::nullopt;
    if (!readBytes(&snapshot.combat.maxHealth, sizeof(int16_t))) return std::nullopt;
    if (!readBytes(&snapshot.combat.teamId, sizeof(uint8_t))) return std::nullopt;
    if (!readBytes(&snapshot.combat.classType, sizeof(uint8_t))) return std::nullopt;
    if (!readBytes(&snapshot.combat.lastAttacker, sizeof(EntityID))) return std::nullopt;
    if (!readBytes(&snapshot.combat.lastAttackTime, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.combat.isDead, sizeof(bool))) return std::nullopt;
    offset++;  // Skip padding
    
    // NetworkState
    if (!readBytes(&snapshot.network.lastInputSequence, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.network.lastInputTime, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.network.rttMs, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.network.packetLoss, sizeof(float))) return std::nullopt;
    if (!readBytes(&snapshot.network.snapshotSequence, sizeof(uint32_t))) return std::nullopt;
    
    // InputState
    if (offset >= data.size()) return std::nullopt;
    uint8_t inputFlags = data[offset++];
    snapshot.lastInput.forward = (inputFlags >> 0) & 1;
    snapshot.lastInput.backward = (inputFlags >> 1) & 1;
    snapshot.lastInput.left = (inputFlags >> 2) & 1;
    snapshot.lastInput.right = (inputFlags >> 3) & 1;
    snapshot.lastInput.jump = (inputFlags >> 4) & 1;
    snapshot.lastInput.attack = (inputFlags >> 5) & 1;
    snapshot.lastInput.block = (inputFlags >> 6) & 1;
    snapshot.lastInput.sprint = (inputFlags >> 7) & 1;
    
    if (!readBytes(&snapshot.lastInput.yaw, sizeof(float))) return std::nullopt;
    if (!readBytes(&snapshot.lastInput.pitch, sizeof(float))) return std::nullopt;
    if (!readBytes(&snapshot.lastInput.sequence, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.lastInput.timestamp_ms, sizeof(uint32_t))) return std::nullopt;
    
    // AntiCheatState
    if (!readBytes(&snapshot.antiCheat.lastValidPosition.x, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.lastValidPosition.y, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.lastValidPosition.z, sizeof(Constants::Fixed))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.lastValidPosition.timestamp_ms, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.lastValidationTime, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.suspiciousMovements, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.maxRecordedSpeed, sizeof(float))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.inputCount, sizeof(uint32_t))) return std::nullopt;
    if (!readBytes(&snapshot.antiCheat.inputWindowStart, sizeof(uint32_t))) return std::nullopt;
    
    return snapshot;
}

// ============================================================================
// EntityMigrationManager Implementation
// ============================================================================

EntityMigrationManager::EntityMigrationManager(uint32_t myZoneId, RedisManager* redis)
    : myZoneId_(myZoneId), redis_(redis) {}

bool EntityMigrationManager::initiateMigration(EntityID entity, uint32_t targetZoneId,
                                               Registry& registry,
                                               MigrationCallback callback) {
    // Check if already migrating
    if (isMigrating(entity)) {
        std::cerr << "[MIGRATION] Entity " << static_cast<uint32_t>(entity) 
                  << " is already migrating" << std::endl;
        return false;
    }
    
    // Capture entity state
    auto snapshot = captureEntityState(entity, registry, targetZoneId);
    if (snapshot.playerId == 0) {
        // Could be an NPC - check if entity exists
        if (!registry.valid(entity)) {
            std::cerr << "[MIGRATION] Entity " << static_cast<uint32_t>(entity) 
                      << " does not exist" << std::endl;
            return false;
        }
    }
    
    // Create active migration record
    ActiveMigration migration;
    migration.entity = entity;
    migration.playerId = snapshot.playerId;
    migration.sourceZoneId = myZoneId_;
    migration.targetZoneId = targetZoneId;
    migration.state = MigrationState::PREPARING;
    migration.startTime = 0;  // Will be set in update
    migration.timeoutMs = defaultTimeoutMs_;
    migration.sequence = ++migrationSequence_;
    migration.snapshot = std::move(snapshot);
    migration.onSuccess = nullptr;
    migration.onFailure = nullptr;
    
    if (callback) {
        migration.onSuccess = [callback, entity]() { callback(entity, true); };
        migration.onFailure = [callback, entity](const std::string&) { callback(entity, false); };
    }
    
    activeMigrations_[entity] = std::move(migration);
    
    // Send migration request to target zone
    sendMigrationData(targetZoneId, activeMigrations_[entity].snapshot);
    
    stats_.totalMigrations++;
    
    std::cout << "[MIGRATION] Started migration of entity " << static_cast<uint32_t>(entity)
              << " to zone " << targetZoneId << std::endl;
    
    return true;
}

void EntityMigrationManager::onMigrationRequestReceived(const EntitySnapshot& snapshot) {
    std::cout << "[MIGRATION] Received migration request for entity " 
              << static_cast<uint32_t>(snapshot.entity) 
              << " from zone " << snapshot.sourceZoneId << std::endl;
    
    // This is called on the target zone when a migration request arrives
    // The target zone should create the entity and acknowledge
}

void EntityMigrationManager::onMigrationStateUpdate(EntityID entity, uint32_t fromZoneId,
                                                    MigrationState newState,
                                                    const std::vector<uint8_t>& data) {
    (void)fromZoneId;
    (void)data;
    
    auto it = activeMigrations_.find(entity);
    if (it == activeMigrations_.end()) {
        return;
    }
    
    transitionState(entity, newState);
}

void EntityMigrationManager::update(Registry& registry, uint32_t currentTimeMs) {
    // Process all active migrations
    std::vector<EntityID> completed;
    std::vector<EntityID> timedOut;
    
    for (auto& [entity, migration] : activeMigrations_) {
        // Check for timeout
        if (migration.startTime > 0 && 
            (currentTimeMs - migration.startTime) > migration.timeoutMs) {
            timedOut.push_back(entity);
            continue;
        }
        
        // Process based on state
        switch (migration.state) {
            case MigrationState::PREPARING:
                onPreparing(entity, registry, currentTimeMs);
                break;
            case MigrationState::TRANSFERRING:
                onTransferring(entity, registry, currentTimeMs);
                break;
            case MigrationState::SYNCING:
                onSyncing(entity, registry, currentTimeMs);
                break;
            case MigrationState::COMPLETING:
                onCompleting(entity, registry, currentTimeMs);
                break;
            case MigrationState::COMPLETED:
                completed.push_back(entity);
                break;
            case MigrationState::FAILED:
                timedOut.push_back(entity);
                break;
            default:
                break;
        }
    }
    
    // Clean up completed migrations
    for (EntityID entity : completed) {
        auto it = activeMigrations_.find(entity);
        if (it != activeMigrations_.end()) {
            if (it->second.onSuccess) {
                it->second.onSuccess();
            }
            cleanupMigration(entity);
        }
    }
    
    // Handle timeouts
    for (EntityID entity : timedOut) {
        onFailed(entity, "Migration timed out");
    }
}

bool EntityMigrationManager::isMigrating(EntityID entity) const {
    return activeMigrations_.find(entity) != activeMigrations_.end();
}

MigrationState EntityMigrationManager::getMigrationState(EntityID entity) const {
    auto it = activeMigrations_.find(entity);
    if (it != activeMigrations_.end()) {
        return it->second.state;
    }
    return MigrationState::NONE;
}

bool EntityMigrationManager::cancelMigration(EntityID entity) {
    auto it = activeMigrations_.find(entity);
    if (it == activeMigrations_.end()) {
        return false;
    }
    
    stats_.cancelledMigrations++;
    
    if (it->second.onFailure) {
        it->second.onFailure("Migration cancelled");
    }
    
    cleanupMigration(entity);
    return true;
}

void EntityMigrationManager::transitionState(EntityID entity, MigrationState newState) {
    auto it = activeMigrations_.find(entity);
    if (it == activeMigrations_.end()) {
        return;
    }
    
    it->second.state = newState;
    
    // Notify target zone of state change
    sendStateUpdate(it->second.targetZoneId, entity, newState, {});
}

void EntityMigrationManager::onPreparing(EntityID entity, Registry& registry, uint32_t currentTimeMs) {
    (void)registry;
    
    auto it = activeMigrations_.find(entity);
    if (it == activeMigrations_.end()) return;
    
    if (it->second.startTime == 0) {
        it->second.startTime = currentTimeMs;
    }
    
    // Move to transferring immediately (in real impl, could do validation here)
    transitionState(entity, MigrationState::TRANSFERRING);
}

void EntityMigrationManager::onTransferring(EntityID entity, Registry& registry, uint32_t currentTimeMs) {
    (void)registry;
    (void)currentTimeMs;
    
    // Data already sent in initiateMigration, wait for target zone acknowledgment
    // For now, simulate acknowledgment after one tick
    transitionState(entity, MigrationState::SYNCING);
}

void EntityMigrationManager::onSyncing(EntityID entity, Registry& registry, uint32_t currentTimeMs) {
    (void)registry;
    (void)currentTimeMs;
    
    // Both zones have the entity, syncing state
    // For now, complete immediately
    transitionState(entity, MigrationState::COMPLETING);
}

void EntityMigrationManager::onCompleting(EntityID entity, Registry& registry, uint32_t currentTimeMs) {
    (void)registry;
    (void)currentTimeMs;
    
    auto it = activeMigrations_.find(entity);
    if (it == activeMigrations_.end()) return;
    
    // Final handoff - notify target zone we're done
    broadcastMigrationComplete(myZoneId_, it->second.targetZoneId, entity, it->second.playerId);
    
    transitionState(entity, MigrationState::COMPLETED);
}

void EntityMigrationManager::onCompleted(EntityID entity, Registry& registry) {
    // Remove entity from this zone
    if (registry.valid(entity)) {
        registry.destroy(entity);
    }
    
    stats_.successfulMigrations++;
}

void EntityMigrationManager::onFailed(EntityID entity, const std::string& reason) {
    std::cerr << "[MIGRATION] Failed for entity " << static_cast<uint32_t>(entity) 
              << ": " << reason << std::endl;
    
    auto it = activeMigrations_.find(entity);
    if (it != activeMigrations_.end()) {
        if (it->second.onFailure) {
            it->second.onFailure(reason);
        }
    }
    
    stats_.failedMigrations++;
    cleanupMigration(entity);
}

EntitySnapshot EntityMigrationManager::captureEntityState(EntityID entity, Registry& registry,
                                                          uint32_t targetZoneId) const {
    EntitySnapshot snapshot;
    snapshot.entity = entity;
    snapshot.targetZoneId = targetZoneId;
    snapshot.sourceZoneId = myZoneId_;
    snapshot.timestamp = 0;  // Will be set by caller
    snapshot.sequence = migrationSequence_;
    
    if (!registry.valid(entity)) {
        return snapshot;
    }
    
    // Copy all component data
    if (registry.all_of<Position>(entity)) {
        snapshot.position = registry.get<Position>(entity);
    }
    if (registry.all_of<Velocity>(entity)) {
        snapshot.velocity = registry.get<Velocity>(entity);
    }
    if (registry.all_of<Rotation>(entity)) {
        snapshot.rotation = registry.get<Rotation>(entity);
    }
    if (registry.all_of<CombatState>(entity)) {
        snapshot.combat = registry.get<CombatState>(entity);
    }
    if (registry.all_of<NetworkState>(entity)) {
        snapshot.network = registry.get<NetworkState>(entity);
    }
    if (registry.all_of<InputState>(entity)) {
        snapshot.lastInput = registry.get<InputState>(entity);
    }
    if (registry.all_of<AntiCheatState>(entity)) {
        snapshot.antiCheat = registry.get<AntiCheatState>(entity);
    }
    if (registry.all_of<PlayerInfo>(entity)) {
        const auto& playerInfo = registry.get<PlayerInfo>(entity);
        snapshot.playerId = playerInfo.playerId;
        snapshot.connectionId = playerInfo.connectionId;
    }
    
    return snapshot;
}

EntityID EntityMigrationManager::restoreEntityState(const EntitySnapshot& snapshot, Registry& registry) {
    // Create new entity
    EntityID newEntity = registry.create();
    
    // Restore all components
    registry.emplace<Position>(newEntity, snapshot.position);
    registry.emplace<Velocity>(newEntity, snapshot.velocity);
    registry.emplace<Rotation>(newEntity, snapshot.rotation);
    registry.emplace<CombatState>(newEntity, snapshot.combat);
    registry.emplace<NetworkState>(newEntity, snapshot.network);
    registry.emplace<InputState>(newEntity, snapshot.lastInput);
    registry.emplace<AntiCheatState>(newEntity, snapshot.antiCheat);
    
    if (snapshot.playerId != 0) {
        PlayerInfo info;
        info.playerId = snapshot.playerId;
        info.connectionId = snapshot.connectionId;
        registry.emplace<PlayerInfo>(newEntity, info);
        registry.emplace<PlayerTag>(newEntity);
    } else {
        registry.emplace<NPCTag>(newEntity);
    }
    
    std::cout << "[MIGRATION] Restored entity " << static_cast<uint32_t>(newEntity) 
              << " from zone " << snapshot.sourceZoneId << std::endl;
    
    return newEntity;
}

void EntityMigrationManager::sendMigrationData(uint32_t targetZoneId, const EntitySnapshot& snapshot) {
    (void)targetZoneId;
    (void)snapshot;
    
    // In real implementation with CrossZoneMessenger:
    // crossZoneMessenger_->sendMigrationRequest(targetZoneId, snapshot);
    
    std::cout << "[MIGRATION] Sending migration data to zone " << targetZoneId << std::endl;
}

void EntityMigrationManager::sendStateUpdate(uint32_t targetZoneId, EntityID entity,
                                             MigrationState state, const std::vector<uint8_t>& data) {
    (void)targetZoneId;
    (void)entity;
    (void)state;
    (void)data;
    
    // In real implementation with CrossZoneMessenger:
    // crossZoneMessenger_->sendMigrationState(targetZoneId, entity, state, data);
}

void EntityMigrationManager::broadcastMigrationComplete(uint32_t sourceZoneId, uint32_t targetZoneId,
                                                        EntityID entity, uint64_t playerId) {
    (void)sourceZoneId;
    (void)targetZoneId;
    (void)entity;
    (void)playerId;
    
    // In real implementation with CrossZoneMessenger:
    // crossZoneMessenger_->sendMigrationComplete(targetZoneId, entity, playerId);
}

void EntityMigrationManager::setupRedisSubscriptions() {
    // Subscribe to migration channels via CrossZoneMessenger
}

void EntityMigrationManager::handleRedisMessage(std::string_view channel, std::string_view message) {
    (void)channel;
    (void)message;
}

void EntityMigrationManager::cleanupMigration(EntityID entity) {
    activeMigrations_.erase(entity);
}

void EntityMigrationManager::updateStats(uint32_t migrationTimeMs, bool success) {
    if (success) {
        // Update average
        if (stats_.successfulMigrations > 0) {
            stats_.avgMigrationTimeMs = 
                (stats_.avgMigrationTimeMs * (stats_.successfulMigrations - 1) + migrationTimeMs) 
                / stats_.successfulMigrations;
        } else {
            stats_.avgMigrationTimeMs = migrationTimeMs;
        }
        
        if (migrationTimeMs > stats_.maxMigrationTimeMs) {
            stats_.maxMigrationTimeMs = migrationTimeMs;
        }
    }
}

} // namespace DarkAges
