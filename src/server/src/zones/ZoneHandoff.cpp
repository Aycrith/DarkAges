// [ZONE_AGENT] Zone handoff controller implementation
// Handles seamless player transitions between zones

#include "zones/ZoneHandoff.hpp"
#include "Constants.hpp"
#include <iostream>
#include <cmath>
#include <random>
#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace DarkAges {

ZoneHandoffController::ZoneHandoffController(uint32_t myZoneId,
                                            EntityMigrationManager* migration,
                                            AuraProjectionManager* aura,
                                            NetworkManager* network)
    : myZoneId_(myZoneId)
    , migration_(migration)
    , aura_(aura)
    , network_(network) {}

bool ZoneHandoffController::initialize(const HandoffConfig& config) {
    config_ = config;
    
    // Validate configuration
    if (config_.preparationDistance <= config_.auraEnterDistance ||
        config_.auraEnterDistance <= config_.migrationDistance ||
        config_.migrationDistance <= config_.handoffDistance) {
        std::cerr << "[HANDOFF] Invalid configuration: distances must be decreasing" << std::endl;
        return false;
    }
    
    std::cout << "[HANDOFF] Initialized for zone " << myZoneId_ 
              << " (prep: " << config_.preparationDistance << "m, "
              << "aura: " << config_.auraEnterDistance << "m, "
              << "migrate: " << config_.migrationDistance << "m, "
              << "handoff: " << config_.handoffDistance << "m)" << std::endl;
    return true;
}

void ZoneHandoffController::setZoneLookupCallbacks(ZoneLookupCallback zoneLookup, 
                                                    ZoneByPositionCallback zoneByPos) {
    zoneLookup_ = zoneLookup;
    zoneByPosition_ = zoneByPos;
}

void ZoneHandoffController::checkPlayerPosition(uint64_t playerId, EntityID entity,
                                               ConnectionID conn, const Position& pos,
                                               const Velocity& vel,
                                               uint32_t currentTimeMs) {
    // Check if already in handoff
    auto it = activeHandoffs_.find(playerId);
    if (it != activeHandoffs_.end()) {
        // Update tracking
        it->second.distanceToEdge = calculateDistanceToEdge(pos);
        it->second.movementDirection = calculateMovementDirection(vel);
        return;
    }
    
    // Calculate distance to zone edge
    float distToEdge = calculateDistanceToEdge(pos);
    
    // Check if should start preparation
    if (distToEdge > 0 && distToEdge < config_.preparationDistance) {
        // Determine target zone based on movement direction
        uint32_t targetZone = determineTargetZone(pos, vel);
        
        if (targetZone != 0 && targetZone != myZoneId_) {
            // Start handoff preparation
            ActiveHandoff handoff;
            handoff.playerId = playerId;
            handoff.entity = entity;
            handoff.connection = conn;
            handoff.sourceZoneId = myZoneId_;
            handoff.targetZoneId = targetZone;
            handoff.phase = HandoffPhase::NONE;
            handoff.phaseStartTime = currentTimeMs;
            handoff.distanceToEdge = distToEdge;
            handoff.movementDirection = calculateMovementDirection(vel);
            
            activeHandoffs_[playerId] = handoff;
            
            enterPreparation(activeHandoffs_[playerId], targetZone, currentTimeMs);
        }
    }
}

void ZoneHandoffController::update(Registry& registry, uint32_t currentTimeMs) {
    std::vector<uint64_t> toRemove;
    
    for (auto& [playerId, handoff] : activeHandoffs_) {
        // Check timeouts
        uint32_t timeout = 0;
        switch (handoff.phase) {
            case HandoffPhase::PREPARING: timeout = config_.preparationTimeoutMs; break;
            case HandoffPhase::MIGRATING: timeout = config_.migrationTimeoutMs; break;
            case HandoffPhase::SWITCHING: timeout = config_.switchTimeoutMs; break;
            default: break;
        }
        
        if (timeout > 0 && (currentTimeMs - handoff.phaseStartTime) > timeout) {
            failHandoff(handoff, "Timeout");
            stats_.timeoutCount++;
            toRemove.push_back(playerId);
            continue;
        }
        
        // Phase transitions based on position
        switch (handoff.phase) {
            case HandoffPhase::PREPARING:
                if (handoff.distanceToEdge < config_.auraEnterDistance) {
                    enterAuraOverlap(handoff, currentTimeMs);
                }
                // Player turned back
                if (handoff.distanceToEdge > config_.preparationDistance) {
                    if (cancelHandoff(playerId)) {
                        stats_.cancelledHandoffs++;
                    }
                    toRemove.push_back(playerId);
                }
                break;
                
            case HandoffPhase::AURA_OVERLAP:
                if (handoff.distanceToEdge < config_.migrationDistance) {
                    enterMigration(handoff, currentTimeMs, registry);
                }
                // Player turned back
                if (handoff.distanceToEdge > config_.preparationDistance) {
                    if (cancelHandoff(playerId)) {
                        stats_.cancelledHandoffs++;
                    }
                    toRemove.push_back(playerId);
                }
                break;
                
            case HandoffPhase::MIGRATING:
                if (migration_) {
                    MigrationState state = migration_->getMigrationState(handoff.entity);
                    if (state == MigrationState::COMPLETED) {
                        enterSwitching(handoff, currentTimeMs);
                    } else if (state == MigrationState::FAILED) {
                        failHandoff(handoff, "Migration failed");
                        toRemove.push_back(playerId);
                    }
                }
                break;
                
            case HandoffPhase::SWITCHING:
                // Wait for client to confirm connection to new zone
                // This is triggered by completeHandoff() called from target zone
                break;
                
            case HandoffPhase::COMPLETED:
                toRemove.push_back(playerId);
                break;
                
            default:
                break;
        }
    }
    
    // Cleanup completed/failed handoffs
    for (auto playerId : toRemove) {
        activeHandoffs_.erase(playerId);
    }
}

void ZoneHandoffController::enterPreparation(ActiveHandoff& handoff, uint32_t targetZoneId, 
                                             uint32_t currentTimeMs) {
    handoff.phase = HandoffPhase::PREPARING;
    handoff.phaseStartTime = currentTimeMs;
    handoff.targetZoneId = targetZoneId;
    
    // Look up target zone connection info
    if (zoneLookup_) {
        ZoneDefinition* targetDef = zoneLookup_(targetZoneId);
        if (targetDef) {
            handoff.targetZoneHost = targetDef->host;
            handoff.targetZonePort = targetDef->port;
        }
    }
    
    std::cout << "[HANDOFF] Player " << handoff.playerId 
              << " entering preparation for zone " << targetZoneId 
              << " (" << handoff.distanceToEdge << "m from edge)" << std::endl;
    
    stats_.totalHandoffs++;
    
    if (onHandoffStarted_) {
        onHandoffStarted_(handoff.playerId, myZoneId_, targetZoneId, true);
    }
}

void ZoneHandoffController::enterAuraOverlap(ActiveHandoff& handoff, uint32_t currentTimeMs) {
    handoff.phase = HandoffPhase::AURA_OVERLAP;
    handoff.phaseStartTime = currentTimeMs;
    
    // Start syncing entity to adjacent zone
    // AuraProjectionManager handles this automatically when entity enters aura
    // The position is already tracked by the AuraProjectionManager via checkEntityZoneTransitions
    if (aura_) {
        // Register entity in aura - actual position is tracked separately via updateEntityState
        Position emptyPos;
        aura_->onEntityEnteringAura(handoff.entity, emptyPos, myZoneId_);
    }
    
    std::cout << "[HANDOFF] Player " << handoff.playerId 
              << " entering aura overlap (" << handoff.distanceToEdge << "m from edge)" << std::endl;
}

void ZoneHandoffController::enterMigration(ActiveHandoff& handoff, uint32_t currentTimeMs, 
                                           Registry& registry) {
    handoff.phase = HandoffPhase::MIGRATING;
    handoff.phaseStartTime = currentTimeMs;
    
    // Start entity migration
    if (migration_) {
        bool started = migration_->initiateMigration(
            handoff.entity, 
            handoff.targetZoneId, 
            registry
        );
        
        if (!started) {
            failHandoff(handoff, "Failed to initiate migration");
            return;
        }
    }
    
    std::cout << "[HANDOFF] Player " << handoff.playerId 
              << " entering migration (" << handoff.distanceToEdge << "m from edge)" << std::endl;
}

void ZoneHandoffController::enterSwitching(ActiveHandoff& handoff, uint32_t currentTimeMs) {
    handoff.phase = HandoffPhase::SWITCHING;
    handoff.phaseStartTime = currentTimeMs;
    
    // Generate handoff token for client auth
    handoff.handoffToken = generateHandoffToken(handoff.playerId);
    
    // Send handoff instruction to client
    sendHandoffInstructionToClient(handoff);
    
    std::cout << "[HANDOFF] Player " << handoff.playerId 
              << " switching to zone " << handoff.targetZoneId 
              << " at " << handoff.targetZoneHost << ":" << handoff.targetZonePort << std::endl;
}

void ZoneHandoffController::completeMigration(ActiveHandoff& handoff, uint32_t currentTimeMs) {
    handoff.phase = HandoffPhase::COMPLETED;
    
    uint32_t handoffTime = currentTimeMs - handoff.phaseStartTime;
    
    std::cout << "[HANDOFF] Player " << handoff.playerId 
              << " completed handoff to zone " << handoff.targetZoneId 
              << " (took " << handoffTime << "ms)" << std::endl;
    
    updateStats(handoffTime, true);
    
    if (onHandoffCompleted_) {
        onHandoffCompleted_(handoff.playerId, myZoneId_, handoff.targetZoneId, true);
    }
}

void ZoneHandoffController::failHandoff(ActiveHandoff& handoff, const std::string& reason) {
    std::cerr << "[HANDOFF] Player " << handoff.playerId 
              << " handoff failed: " << reason << std::endl;
    
    stats_.failedHandoffs++;
    
    if (onHandoffCompleted_) {
        onHandoffCompleted_(handoff.playerId, myZoneId_, handoff.targetZoneId, false);
    }
}

bool ZoneHandoffController::cancelHandoff(uint64_t playerId) {
    auto it = activeHandoffs_.find(playerId);
    if (it == activeHandoffs_.end()) {
        return false;
    }
    
    // Cancel migration if in progress
    if (it->second.phase == HandoffPhase::MIGRATING && migration_) {
        migration_->cancelMigration(it->second.entity);
    }
    
    std::cout << "[HANDOFF] Player " << playerId << " handoff cancelled" << std::endl;
    
    activeHandoffs_.erase(it);
    return true;
}

HandoffPhase ZoneHandoffController::getPlayerPhase(uint64_t playerId) const {
    auto it = activeHandoffs_.find(playerId);
    if (it != activeHandoffs_.end()) {
        return it->second.phase;
    }
    return HandoffPhase::NONE;
}

bool ZoneHandoffController::isHandoffInProgress(uint64_t playerId) const {
    return activeHandoffs_.find(playerId) != activeHandoffs_.end();
}

bool ZoneHandoffController::validateHandoffToken(uint64_t playerId, const std::string& token) {
    auto it = activeHandoffs_.find(playerId);
    if (it == activeHandoffs_.end()) {
        return false;
    }
    
    return it->second.handoffToken == token;
}

void ZoneHandoffController::completeHandoff(uint64_t playerId, bool success) {
    auto it = activeHandoffs_.find(playerId);
    if (it == activeHandoffs_.end()) {
        return;
    }
    
    uint32_t currentTimeMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
    );
    
    if (success) {
        completeMigration(it->second, currentTimeMs);
    } else {
        failHandoff(it->second, "Target zone reported failure");
        activeHandoffs_.erase(it);
    }
}

uint32_t ZoneHandoffController::determineTargetZone(const Position& pos, 
                                                    const Velocity& vel) const {
    // Use position-based lookup first
    if (zoneByPosition_) {
        float x = pos.x * Constants::FIXED_TO_FLOAT;
        float z = pos.z * Constants::FIXED_TO_FLOAT;
        
        float vx = vel.dx * Constants::FIXED_TO_FLOAT;
        float vz = vel.dz * Constants::FIXED_TO_FLOAT;
        
        // Predict position 2 seconds in the future
        float futureX = x + vx * 2.0f;
        float futureZ = z + vz * 2.0f;
        
        // Return zone that contains predicted position
        return zoneByPosition_(futureX, futureZ);
    }
    
    // Fallback: simple boundary detection based on zone definition
    if (myZoneDef_.zoneId != 0) {
        float x = pos.x * Constants::FIXED_TO_FLOAT;
        float z = pos.z * Constants::FIXED_TO_FLOAT;
        
        // Check if moving beyond X boundary
        if (x > myZoneDef_.maxX && !myZoneDef_.adjacentZones.empty()) {
            return myZoneDef_.adjacentZones[0];
        }
        if (x < myZoneDef_.minX && !myZoneDef_.adjacentZones.empty()) {
            return myZoneDef_.adjacentZones[0];
        }
        if (z > myZoneDef_.maxZ && myZoneDef_.adjacentZones.size() > 1) {
            return myZoneDef_.adjacentZones[1];
        }
        if (z < myZoneDef_.minZ && myZoneDef_.adjacentZones.size() > 1) {
            return myZoneDef_.adjacentZones[1];
        }
    }
    
    return 0;  // Unknown
}

float ZoneHandoffController::calculateDistanceToEdge(const Position& pos) const {
    // Calculate distance to the nearest edge of my zone
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    if (myZoneDef_.zoneId != 0) {
        // Calculate distance to each edge
        float distToMaxX = myZoneDef_.maxX - x;
        float distToMinX = x - myZoneDef_.minX;
        float distToMaxZ = myZoneDef_.maxZ - z;
        float distToMinZ = z - myZoneDef_.minZ;
        
        // Find minimum positive distance (closest edge we're approaching)
        float minDist = 10000.0f;
        if (distToMaxX > 0) minDist = std::min(minDist, distToMaxX);
        if (distToMinX > 0) minDist = std::min(minDist, distToMinX);
        if (distToMaxZ > 0) minDist = std::min(minDist, distToMaxZ);
        if (distToMinZ > 0) minDist = std::min(minDist, distToMinZ);
        
        return minDist;
    }
    
    // Fallback: simple world bounds check
    float distToMaxX = Constants::WORLD_MAX_X - x;
    float distToMinX = x - Constants::WORLD_MIN_X;
    float distToMaxZ = Constants::WORLD_MAX_Z - z;
    float distToMinZ = z - Constants::WORLD_MIN_Z;
    
    return std::min({distToMaxX, distToMinX, distToMaxZ, distToMinZ});
}

glm::vec2 ZoneHandoffController::calculateMovementDirection(const Velocity& vel) const {
    float vx = vel.dx * Constants::FIXED_TO_FLOAT;
    float vz = vel.dz * Constants::FIXED_TO_FLOAT;
    
    float len = std::sqrt(vx * vx + vz * vz);
    if (len > 0.001f) {
        return glm::vec2(vx / len, vz / len);
    }
    
    return glm::vec2(0, 0);
}

std::string ZoneHandoffController::generateHandoffToken(uint64_t playerId) {
    // Generate secure random token (256 bits)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::string token;
    token.reserve(64);  // 32 bytes = 64 hex chars
    
    for (int i = 0; i < 64; i++) {
        char hex[] = "0123456789abcdef";
        token += hex[dis(gen)];
    }
    
    return token;
}

void ZoneHandoffController::sendHandoffInstructionToClient(const ActiveHandoff& handoff) {
    if (!network_) return;
    
    // Build handoff packet
    // [type:1][target_zone:4][host_len:1][host][port:2][token_len:1][token]
    
    std::vector<uint8_t> packet;
    packet.reserve(128);
    
    // Packet type: Handoff instruction (reliable event)
    packet.push_back(static_cast<uint8_t>(PacketType::ReliableEvent));
    
    // Event subtype
    packet.push_back(0x01);  // Handoff instruction
    
    // Target zone ID
    packet.resize(packet.size() + 4);
    *reinterpret_cast<uint32_t*>(&packet[packet.size() - 4]) = handoff.targetZoneId;
    
    // Host
    packet.push_back(static_cast<uint8_t>(handoff.targetZoneHost.size()));
    packet.insert(packet.end(), handoff.targetZoneHost.begin(), handoff.targetZoneHost.end());
    
    // Port
    packet.resize(packet.size() + 2);
    *reinterpret_cast<uint16_t*>(&packet[packet.size() - 2]) = handoff.targetZonePort;
    
    // Token
    packet.push_back(static_cast<uint8_t>(handoff.handoffToken.size()));
    packet.insert(packet.end(), handoff.handoffToken.begin(), handoff.handoffToken.end());
    
    // Send to client
    network_->sendEvent(handoff.connection, packet);
    
    std::cout << "[HANDOFF] Sent handoff instruction to player " << handoff.playerId 
              << " for zone " << handoff.targetZoneId << std::endl;
}

void ZoneHandoffController::updateStats(uint32_t handoffTimeMs, bool success) {
    if (success) {
        stats_.successfulHandoffs++;
        
        // Update average handoff time
        uint32_t totalSuccessful = stats_.successfulHandoffs;
        stats_.avgHandoffTimeMs = 
            (stats_.avgHandoffTimeMs * (totalSuccessful - 1) + handoffTimeMs) / totalSuccessful;
        
        stats_.maxHandoffTimeMs = std::max(stats_.maxHandoffTimeMs, handoffTimeMs);
    } else {
        stats_.failedHandoffs++;
    }
}

} // namespace DarkAges
