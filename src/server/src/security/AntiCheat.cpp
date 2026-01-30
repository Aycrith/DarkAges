#include "security/AntiCheat.hpp"
#include "Constants.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace DarkAges {
namespace Security {

// ============================================================================
// BehaviorProfile Implementation
// ============================================================================

void BehaviorProfile::recordViolation(const CheatDetectionResult& detection) {
    ViolationRecord record;
    record.type = detection.type;
    record.timestamp = detection.timestamp;
    record.confidence = detection.confidence;
    if (detection.description) {
        record.details = detection.description;
    }
    
    violationHistory.push_back(record);
    
    // Keep only recent violations
    while (violationHistory.size() > MAX_VIOLATIONS) {
        violationHistory.erase(violationHistory.begin());
    }
    
    // Update type-specific counters
    switch (detection.type) {
        case CheatType::SPEED_HACK:
            speedViolations++;
            break;
        case CheatType::TELEPORT:
            teleportDetections++;
            break;
        case CheatType::FLY_HACK:
            flyViolations++;
            break;
        default:
            break;
    }
    
    // Decrease trust based on severity
    int8_t trustDelta = -5;
    switch (detection.severity) {
        case ViolationSeverity::WARNING:
            trustDelta = -5;
            break;
        case ViolationSeverity::SUSPICIOUS:
            trustDelta = -15;
            break;
        case ViolationSeverity::CRITICAL:
            trustDelta = -25;
            break;
        case ViolationSeverity::BAN:
            trustDelta = -100;
            break;
        default:
            trustDelta = -2;
    }
    
    updateTrustScore(trustDelta);
    consecutiveCleanTicks = 0;
}

void BehaviorProfile::recordCleanMovement() {
    consecutiveCleanTicks++;
    
    // Recover trust slowly over time
    if (consecutiveCleanTicks % 600 == 0) {  // Every ~10 seconds at 60Hz
        updateTrustScore(1);
    }
}

void BehaviorProfile::updateTrustScore(int8_t delta) {
    int16_t newScore = static_cast<int16_t>(trustScore) + delta;
    trustScore = static_cast<uint8_t>(std::clamp(newScore, static_cast<int16_t>(0), static_cast<int16_t>(100)));
}

bool BehaviorProfile::isNewPlayer(uint32_t currentTimeMs) const {
    // Consider new if account less than 5 minutes old
    return (currentTimeMs - accountCreationTime) < 300000;
}

uint32_t BehaviorProfile::getRecentViolationCount(uint32_t windowMs, 
    uint32_t currentTimeMs) const {
    
    uint32_t count = 0;
    for (const auto& violation : violationHistory) {
        if (currentTimeMs - violation.timestamp <= windowMs) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// AntiCheatSystem Implementation
// ============================================================================

AntiCheatSystem::AntiCheatSystem() = default;

AntiCheatSystem::~AntiCheatSystem() {
    shutdown();
}

bool AntiCheatSystem::initialize() {
    if (initialized_) {
        return true;
    }
    
    std::cout << "[ANTICHEAT] System initialized\n";
    std::cout << "[ANTICHEAT] Speed tolerance: " << config_.speedTolerance << "x\n";
    std::cout << "[ANTICHEAT] Max teleport distance: " << config_.maxTeleportDistance << "m\n";
    std::cout << "[ANTICHEAT] Rate limit: " << config_.maxInputsPerSecond << " inputs/sec\n";
    
    initialized_ = true;
    return true;
}

void AntiCheatSystem::shutdown() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(profileMutex_);
    profiles_.clear();
    
    initialized_ = false;
}

CheatDetectionResult AntiCheatSystem::validateMovement(EntityID entity,
    const Position& oldPos, const Position& newPos,
    uint32_t dtMs, const InputState& input,
    Registry& registry) {
    
    // Validate position bounds first
    auto result = validatePositionBounds(newPos);
    if (result.detected) return result;
    
    // Check for teleport
    result = detectTeleport(entity, oldPos, newPos, registry);
    if (result.detected) return result;
    
    // Check for speed hack
    result = detectSpeedHack(entity, oldPos, newPos, dtMs, input, registry);
    if (result.detected) return result;
    
    // Check for fly hack (if we have velocity component)
    if (Velocity* vel = registry.try_get<Velocity>(entity)) {
        result = detectFlyHack(entity, newPos, *vel, input, registry);
        if (result.detected) return result;
    }
    
    // Check for no-clip
    result = detectNoClip(entity, oldPos, newPos, registry);
    if (result.detected) return result;
    
    // Record clean movement for trust score
    if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
        recordCleanMovement(info->playerId);
    }
    
    return CheatDetectionResult{};  // No cheat detected
}

CheatDetectionResult AntiCheatSystem::detectSpeedHack(EntityID entity,
    const Position& oldPos, const Position& newPos,
    uint32_t dtMs, const InputState& input, Registry& registry) {
    
    CheatDetectionResult result;
    
    if (dtMs == 0) return result;
    
    // Calculate actual speed
    float actualSpeed = calculateSpeed(oldPos, newPos, dtMs);
    
    // Calculate max allowed speed based on input
    float maxSpeed = calculateMaxAllowedSpeed(input);
    float tolerance = maxSpeed * (config_.speedTolerance - 1.0f);
    
    // Get behavior profile for trust-based adjustments
    float trustMultiplier = 1.0f;
    if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
        if (auto* profile = getProfile(info->playerId)) {
            // More lenient for trusted players
            if (profile->isTrusted()) {
                trustMultiplier = 1.1f;
            }
            // Stricter for suspicious players
            else if (profile->isSuspicious()) {
                trustMultiplier = 0.8f;
            }
            // New player leniency
            if (profile->isNewPlayer(static_cast<uint32_t>(
                std::chrono::steady_clock::now().time_since_epoch().count() / 1000000))) {
                trustMultiplier *= 1.2f;
            }
        }
    }
    
    if (actualSpeed > (maxSpeed + tolerance) * trustMultiplier) {
        result.detected = true;
        result.type = CheatType::SPEED_HACK;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Speed hack detected";
        result.correctedPosition = oldPos;  // Revert to old position
        result.confidence = std::min(1.0f, (actualSpeed - maxSpeed) / maxSpeed);
        result.actualValue = actualSpeed;
        result.expectedValue = maxSpeed;
        result.timestamp = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() / 1000000);
        
        // Update profile
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
            updateBehaviorProfile(info->playerId, result);
        }
        
        ++totalDetections_;
        
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            detectionCounts_[CheatType::SPEED_HACK]++;
        }
        
        reportViolation(registry.get<PlayerInfo>(entity).playerId, result);
    }
    
    return result;
}

CheatDetectionResult AntiCheatSystem::detectTeleport(EntityID entity,
    const Position& oldPos, const Position& newPos,
    Registry& registry) {
    
    CheatDetectionResult result;
    
    float distance = calculateDistance(oldPos, newPos);
    float maxTeleport = config_.maxTeleportDistance;
    
    // Check for teleport grace period (after zone changes, respawns, etc.)
    // This would typically check if the entity has a "teleportGrace" tag/component
    
    if (distance > maxTeleport) {
        result.detected = true;
        result.type = CheatType::TELEPORT;
        result.severity = config_.instantBanOnTeleport ? 
            ViolationSeverity::BAN : ViolationSeverity::CRITICAL;
        result.description = "Teleport hack detected";
        result.correctedPosition = oldPos;
        result.confidence = 1.0f;
        result.actualValue = distance;
        result.expectedValue = maxTeleport;
        result.timestamp = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() / 1000000);
        
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
            updateBehaviorProfile(info->playerId, result);
        }
        
        ++totalDetections_;
        
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            detectionCounts_[CheatType::TELEPORT]++;
        }
        
        reportViolation(registry.get<PlayerInfo>(entity).playerId, result);
    }
    
    return result;
}

CheatDetectionResult AntiCheatSystem::detectFlyHack(EntityID entity,
    const Position& pos, const Velocity& vel,
    const InputState& input, Registry& registry) {
    
    CheatDetectionResult result;
    
    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float vy = vel.dy * Constants::FIXED_TO_FLOAT;
    
    // Check if Y position is valid (not flying above world)
    if (y > Constants::WORLD_MAX_Y) {
        result.detected = true;
        result.type = CheatType::FLY_HACK;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Fly hack detected - above world bounds";
        result.confidence = 1.0f;
        
        // Clamp Y to valid range
        result.correctedPosition = pos;
        result.correctedPosition.y = static_cast<Constants::Fixed>(
            Constants::WORLD_MAX_Y * Constants::FLOAT_TO_FIXED);
        
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
            updateBehaviorProfile(info->playerId, result);
        }
        
        ++totalDetections_;
        return result;
    }
    
    // Check vertical velocity without jump input
    // Only check if not grounded and not jumping
    if (vy > config_.maxVerticalSpeedNoJump && !input.jump) {
        // Could be fly hack or just jumping
        // More sophisticated check would track air time
        if (!isGrounded(entity, pos, registry)) {
            // Check how long they've been airborne
            if (AntiCheatState* cheatState = registry.try_get<AntiCheatState>(entity)) {
                if (cheatState->lastValidationTime > 0) {
                    uint32_t airTime = static_cast<uint32_t>(
                        std::chrono::steady_clock::now().time_since_epoch().count() / 1000000)
                        - cheatState->lastValidationTime;
                    
                    if (airTime > config_.maxAirTimeMs) {
                        result.detected = true;
                        result.type = CheatType::FLY_HACK;
                        result.severity = ViolationSeverity::SUSPICIOUS;
                        result.description = "Possible fly hack - extended air time";
                        result.confidence = std::min(1.0f, 
                            static_cast<float>(airTime) / (config_.maxAirTimeMs * 2.0f));
                        
                        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
                            updateBehaviorProfile(info->playerId, result);
                        }
                        
                        ++totalDetections_;
                        return result;
                    }
                }
            }
        }
    }
    
    return result;
}

CheatDetectionResult AntiCheatSystem::detectNoClip(EntityID entity,
    const Position& oldPos, const Position& newPos,
    Registry& registry) {
    
    CheatDetectionResult result;
    
    // Simplified no-clip detection: check if path intersects with world geometry
    // In a full implementation, this would use the spatial hash to check
    // for collision with static world geometry along the movement path
    
    // For now, placeholder implementation
    // TODO: Implement proper collision detection with world geometry
    
    return result;
}

CheatDetectionResult AntiCheatSystem::validateInput(EntityID entity,
    const InputState& input, uint32_t currentTimeMs,
    Registry& registry) {
    
    CheatDetectionResult result;
    
    // Validate yaw value (should be within reasonable bounds)
    if (input.yaw < -config_.maxYaw || input.yaw > config_.maxYaw) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::WARNING;
        result.description = "Invalid yaw value";
        result.confidence = 1.0f;
        return result;
    }
    
    // Validate pitch value
    if (input.pitch < -config_.maxPitch || input.pitch > config_.maxPitch) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::WARNING;
        result.description = "Invalid pitch value";
        result.confidence = 1.0f;
        return result;
    }
    
    // Check for impossible input combinations
    if (input.forward && input.backward) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::INFO;
        result.description = "Conflicting input detected";
        result.confidence = 0.5f;
        return result;
    }
    
    if (input.left && input.right) {
        result.detected = true;
        result.type = CheatType::INPUT_MANIPULATION;
        result.severity = ViolationSeverity::INFO;
        result.description = "Conflicting input detected";
        result.confidence = 0.5f;
        return result;
    }
    
    return result;
}

CheatDetectionResult AntiCheatSystem::checkRateLimit(EntityID entity,
    uint32_t currentTimeMs, Registry& registry) {
    
    CheatDetectionResult result;
    
    AntiCheatState* cheatState = registry.try_get<AntiCheatState>(entity);
    if (!cheatState) return result;
    
    // Check if time window has passed
    if (currentTimeMs - cheatState->inputWindowStart > config_.inputWindowMs) {
        // New window
        cheatState->inputWindowStart = currentTimeMs;
        cheatState->inputCount = 1;
        return result;
    }
    
    // Increment count
    cheatState->inputCount++;
    
    // Check against limit (with burst allowance)
    if (cheatState->inputCount > config_.maxInputsPerSecond + config_.inputBurstAllowance) {
        result.detected = true;
        result.type = CheatType::PACKET_FLOODING;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Input rate limit exceeded";
        result.confidence = std::min(1.0f, 
            static_cast<float>(cheatState->inputCount - config_.maxInputsPerSecond) / 
            config_.maxInputsPerSecond);
        
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(entity)) {
            updateBehaviorProfile(info->playerId, result);
        }
        
        ++totalDetections_;
        
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            detectionCounts_[CheatType::PACKET_FLOODING]++;
        }
        
        reportViolation(registry.get<PlayerInfo>(entity).playerId, result);
    }
    
    return result;
}

CheatDetectionResult AntiCheatSystem::validatePositionBounds(
    const Position& pos) const {
    
    CheatDetectionResult result;
    
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    // Check world bounds
    if (x < Constants::WORLD_MIN_X || x > Constants::WORLD_MAX_X ||
        y < Constants::WORLD_MIN_Y || y > Constants::WORLD_MAX_Y ||
        z < Constants::WORLD_MIN_Z || z > Constants::WORLD_MAX_Z) {
        
        result.detected = true;
        result.type = CheatType::POSITION_SPOOFING;
        result.severity = ViolationSeverity::CRITICAL;
        result.description = "Position out of world bounds";
        result.confidence = 1.0f;
        
        // Clamp to valid bounds
        result.correctedPosition.x = static_cast<Constants::Fixed>(
            std::clamp(x, Constants::WORLD_MIN_X, Constants::WORLD_MAX_X) * 
            Constants::FLOAT_TO_FIXED);
        result.correctedPosition.y = static_cast<Constants::Fixed>(
            std::clamp(y, Constants::WORLD_MIN_Y, Constants::WORLD_MAX_Y) * 
            Constants::FLOAT_TO_FIXED);
        result.correctedPosition.z = static_cast<Constants::Fixed>(
            std::clamp(z, Constants::WORLD_MIN_Z, Constants::WORLD_MAX_Z) * 
            Constants::FLOAT_TO_FIXED);
    }
    
    return result;
}

CheatDetectionResult AntiCheatSystem::validateCombat(EntityID attacker,
    EntityID target, int16_t claimedDamage, 
    const Position& claimedHitPos, const Position& attackerPos,
    Registry& registry) {
    
    CheatDetectionResult result;
    
    // Validate damage amount
    if (claimedDamage > config_.maxDamagePerHit) {
        result.detected = true;
        result.type = CheatType::DAMAGE_HACK;
        result.severity = ViolationSeverity::BAN;
        result.description = "Damage exceeds maximum allowed";
        result.confidence = 1.0f;
        result.actualValue = static_cast<float>(claimedDamage);
        result.expectedValue = static_cast<float>(config_.maxDamagePerHit);
        
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(attacker)) {
            updateBehaviorProfile(info->playerId, result);
        }
        
        ++totalDetections_;
        return result;
    }
    
    // Validate hit range
    float hitDistance = calculateDistance(attackerPos, claimedHitPos);
    float maxRange = config_.maxMeleeRange;  // Could check weapon type for range
    
    if (hitDistance > maxRange * 1.5f) {  // 50% tolerance for lag
        result.detected = true;
        result.type = CheatType::HITBOX_EXTENSION;
        result.severity = ViolationSeverity::SUSPICIOUS;
        result.description = "Hit range exceeds maximum";
        result.confidence = std::min(1.0f, hitDistance / maxRange - 1.0f);
        result.actualValue = hitDistance;
        result.expectedValue = maxRange;
        
        if (PlayerInfo* info = registry.try_get<PlayerInfo>(attacker)) {
            updateBehaviorProfile(info->playerId, result);
        }
        
        ++totalDetections_;
        return result;
    }
    
    return result;
}

float AntiCheatSystem::calculateMaxAllowedSpeed(const InputState& input) const {
    float baseSpeed = Constants::MAX_PLAYER_SPEED;
    if (input.sprint) {
        baseSpeed = Constants::MAX_SPRINT_SPEED;
    }
    return baseSpeed;
}

float AntiCheatSystem::calculateDistance(const Position& a, const Position& b) const {
    float dx = (a.x - b.x) * Constants::FIXED_TO_FLOAT;
    float dy = (a.y - b.y) * Constants::FIXED_TO_FLOAT;
    float dz = (a.z - b.z) * Constants::FIXED_TO_FLOAT;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

float AntiCheatSystem::calculateSpeed(const Position& from, const Position& to, 
    uint32_t dtMs) const {
    
    if (dtMs == 0) return 0.0f;
    float distance = calculateDistance(from, to);
    return distance / (dtMs / 1000.0f);
}

bool AntiCheatSystem::isPositionValid(const Position& pos) const {
    return isWithinWorldBounds(pos);
}

bool AntiCheatSystem::isWithinWorldBounds(const Position& pos) const {
    float x = pos.x * Constants::FIXED_TO_FLOAT;
    float y = pos.y * Constants::FIXED_TO_FLOAT;
    float z = pos.z * Constants::FIXED_TO_FLOAT;
    
    return x >= Constants::WORLD_MIN_X && x <= Constants::WORLD_MAX_X &&
           y >= Constants::WORLD_MIN_Y && y <= Constants::WORLD_MAX_Y &&
           z >= Constants::WORLD_MIN_Z && z <= Constants::WORLD_MAX_Z;
}

bool AntiCheatSystem::wouldCollideWithWorld(const Position& pos) const {
    // Placeholder - would check spatial hash against world geometry
    return false;
}

bool AntiCheatSystem::isGrounded(EntityID entity, const Position& pos, 
    Registry& registry) const {
    // Placeholder - would check for ground below player
    // In full implementation, cast ray downward to check for ground
    return true;  // Conservative default
}

void AntiCheatSystem::applyPositionCorrection(EntityID entity, 
    const Position& correctedPos, Registry& registry) {
    
    if (Position* pos = registry.try_get<Position>(entity)) {
        *pos = correctedPos;
    }
}

void AntiCheatSystem::updateBehaviorProfile(uint64_t playerId, 
    const CheatDetectionResult& detection) {
    
    std::lock_guard<std::mutex> lock(profileMutex_);
    
    auto& profile = profiles_[playerId];
    if (profile.playerId == 0) {
        profile.playerId = playerId;
        profile.accountCreationTime = detection.timestamp;
    }
    
    profile.recordViolation(detection);
}

void AntiCheatSystem::recordCleanMovement(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(profileMutex_);
    
    auto it = profiles_.find(playerId);
    if (it != profiles_.end()) {
        it->second.recordCleanMovement();
    }
}

BehaviorProfile* AntiCheatSystem::getProfile(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(profileMutex_);
    
    auto it = profiles_.find(playerId);
    if (it != profiles_.end()) {
        return &it->second;
    }
    
    // Create new profile
    auto& profile = profiles_[playerId];
    profile.playerId = playerId;
    profile.trustScore = config_.initialTrustScore;
    profile.accountCreationTime = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() / 1000000);
    return &profile;
}

const BehaviorProfile* AntiCheatSystem::getProfile(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(profileMutex_);
    
    auto it = profiles_.find(playerId);
    if (it != profiles_.end()) {
        return &it->second;
    }
    return nullptr;
}

void AntiCheatSystem::removeProfile(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(profileMutex_);
    profiles_.erase(playerId);
}

void AntiCheatSystem::reportViolation(uint64_t playerId, 
    const CheatDetectionResult& result) {
    
    std::cerr << "[ANTICHEAT] Player " << playerId 
              << " - " << result.description
              << " [" << cheatTypeToString(result.type) << "]"
              << " (confidence: " << static_cast<int>(result.confidence * 100) << "%)"
              << " [" << severityToString(result.severity) << "]\n";
    
    if (onCheatDetected_) {
        onCheatDetected_(playerId, result);
    }
    
    // Handle severity-based actions
    handleSeverityAction(playerId, result);
}

void AntiCheatSystem::handleSeverityAction(uint64_t playerId, 
    const CheatDetectionResult& result) {
    
    switch (result.severity) {
        case ViolationSeverity::CRITICAL:
            kickPlayer(playerId, result.description);
            break;
        case ViolationSeverity::BAN:
            banPlayer(playerId, result.description, config_.defaultBanDurationMinutes);
            break;
        default:
            // INFO, WARNING, SUSPICIOUS - just log
            break;
    }
}

void AntiCheatSystem::banPlayer(uint64_t playerId, const char* reason,
    uint32_t durationMinutes) {
    
    std::cerr << "[ANTICHEAT] Banning player " << playerId 
              << " for " << durationMinutes << " minutes: " << reason << "\n";
    
    ++playersBanned_;
    
    if (onPlayerBanned_) {
        onPlayerBanned_(playerId, reason, durationMinutes);
    }
}

void AntiCheatSystem::kickPlayer(uint64_t playerId, const char* reason) {
    std::cerr << "[ANTICHEAT] Kicking player " << playerId 
              << ": " << reason << "\n";
    
    ++playersKicked_;
    
    if (onPlayerKicked_) {
        onPlayerKicked_(playerId, reason);
    }
}

void AntiCheatSystem::flagPlayerForReview(uint64_t playerId, const char* reason) {
    std::cerr << "[ANTICHEAT] Flagging player " << playerId 
              << " for review: " << reason << "\n";
    
    // Would add to review queue for admin inspection
}

uint32_t AntiCheatSystem::getActiveProfileCount() const {
    std::lock_guard<std::mutex> lock(profileMutex_);
    return static_cast<uint32_t>(profiles_.size());
}

uint32_t AntiCheatSystem::getDetectionCount(CheatType type) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto it = detectionCounts_.find(type);
    if (it != detectionCounts_.end()) {
        return it->second;
    }
    return 0;
}

void AntiCheatSystem::resetStatistics() {
    totalDetections_ = 0;
    playersKicked_ = 0;
    playersBanned_ = 0;
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    detectionCounts_.clear();
}

// ============================================================================
// ServerAuthority Implementation
// ============================================================================

void ServerAuthority::sendPositionCorrection(ConnectionID conn,
    const Position& serverPos, const Velocity& serverVel,
    uint32_t lastProcessedInput) {
    
    // This would serialize a position correction packet and send via NetworkManager
    // Placeholder implementation
    (void)conn;
    (void)serverPos;
    (void)serverVel;
    (void)lastProcessedInput;
}

void ServerAuthority::sendStateSync(ConnectionID conn, EntityID entity,
    Registry& registry) {
    
    // Send full entity state for major resyncs
    (void)conn;
    (void)entity;
    (void)registry;
}

void ServerAuthority::sendAuthorityCorrection(ConnectionID conn,
    const Position& serverPos, const Position& clientClaimedPos,
    const char* reason) {
    
    (void)conn;
    (void)serverPos;
    (void)clientClaimedPos;
    (void)reason;
}

bool ServerAuthority::validateClientClaim(const Position& serverPos,
    const Position& claimedPos, const Velocity& serverVel,
    const Velocity& claimedVel, float tolerance) {
    
    float dx = (serverPos.x - claimedPos.x) * Constants::FIXED_TO_FLOAT;
    float dy = (serverPos.y - claimedPos.y) * Constants::FIXED_TO_FLOAT;
    float dz = (serverPos.z - claimedPos.z) * Constants::FIXED_TO_FLOAT;
    
    float positionError = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    if (positionError > tolerance) {
        return false;
    }
    
    // Also check velocity if needed
    (void)serverVel;
    (void)claimedVel;
    
    return true;
}

float ServerAuthority::calculateCorrectionMagnitude(const Position& serverPos,
    const Position& clientPos) {
    
    float dx = (serverPos.x - clientPos.x) * Constants::FIXED_TO_FLOAT;
    float dy = (serverPos.y - clientPos.y) * Constants::FIXED_TO_FLOAT;
    float dz = (serverPos.z - clientPos.z) * Constants::FIXED_TO_FLOAT;
    
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

bool ServerAuthority::needsCorrection(const Position& serverPos,
    const Position& clientPos, float tolerance) {
    
    return calculateCorrectionMagnitude(serverPos, clientPos) > tolerance;
}

// ============================================================================
// StatisticalAnalyzer Implementation (Placeholder)
// ============================================================================

bool StatisticalAnalyzer::analyzeAimPattern(EntityID entity,
    const std::vector<Rotation>& aimHistory) {
    (void)entity;
    (void)aimHistory;
    // Placeholder - would analyze aim smoothness, snap patterns, etc.
    return false;
}

float StatisticalAnalyzer::calculateMovementConsistency(
    const std::vector<Position>& positionHistory) {
    (void)positionHistory;
    // Placeholder - would calculate variance in movement patterns
    return 1.0f;
}

bool StatisticalAnalyzer::detectImpossibleReactions(
    const std::vector<uint32_t>& reactionTimeSamples) {
    (void)reactionTimeSamples;
    // Placeholder - would detect inhuman reaction times
    return false;
}

} // namespace Security
} // namespace DarkAges
