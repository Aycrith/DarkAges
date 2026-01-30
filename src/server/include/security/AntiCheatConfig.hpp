#pragma once
#include <cstdint>

// [SECURITY_AGENT] Tunable anti-cheat parameters
// All values can be adjusted at runtime via configuration

namespace DarkAges {
namespace Security {

struct AntiCheatConfig {
    // ========================================================================
    // SPEED CHECK PARAMETERS
    // ========================================================================
    
    // Tolerance multiplier for speed checks (1.2 = 20% tolerance for lag)
    float speedTolerance = 1.2f;
    
    // Number of speed violations before auto-kick
    uint32_t speedViolationThreshold = 3;
    
    // Time window for violation counting (milliseconds)
    uint32_t speedViolationWindowMs = 5000;
    
    // Maximum time without ground contact before fly detection (milliseconds)
    uint32_t maxAirTimeMs = 500;
    
    // ========================================================================
    // TELEPORT DETECTION
    // ========================================================================
    
    // Maximum allowed position change in one tick (meters)
    float maxTeleportDistance = 100.0f;
    
    // Whether to instantly ban on teleport detection
    bool instantBanOnTeleport = true;
    
    // Teleport grace period after zone changes (milliseconds)
    uint32_t teleportGracePeriodMs = 2000;
    
    // ========================================================================
    // RATE LIMITING
    // ========================================================================
    
    // Maximum input packets per second per player
    uint32_t maxInputsPerSecond = 60;
    
    // Time window for rate limiting (milliseconds)
    uint32_t inputWindowMs = 1000;
    
    // Burst allowance (can exceed rate for this many packets)
    uint32_t inputBurstAllowance = 5;
    
    // ========================================================================
    // FLY/NO-CLIP DETECTION
    // ========================================================================
    
    // Distance to check for ground below player
    float groundCheckDistance = 1.0f;
    
    // Maximum Y velocity without jump input (m/s)
    float maxVerticalSpeedNoJump = 0.5f;
    
    // Number of consecutive fly violations before kick
    uint32_t flyViolationThreshold = 3;
    
    // ========================================================================
    // INPUT VALIDATION
    // ========================================================================
    
    // Maximum yaw value (radians, 2*PI is full circle)
    float maxYaw = 6.283185f;  // 2 * PI
    
    // Maximum pitch value (radians, PI/2 is 90 degrees)
    float maxPitch = 1.570796f;  // PI / 2
    
    // Minimum time between attack inputs (milliseconds)
    uint32_t minAttackIntervalMs = 500;
    
    // ========================================================================
    // TRUST SYSTEM
    // ========================================================================
    
    // Initial trust score for new players (0-100)
    uint8_t initialTrustScore = 50;
    
    // Minimum trust score for lenient checks
    uint8_t minTrustForLenientChecks = 70;
    
    // Trust score below which to apply stricter checks
    uint8_t suspiciousTrustThreshold = 30;
    
    // Trust recovery rate per clean minute
    uint8_t trustRecoveryPerMinute = 5;
    
    // ========================================================================
    // POSITION VALIDATION
    // ========================================================================
    
    // Tolerance for position reconciliation (meters)
    float positionTolerance = 0.5f;
    
    // Maximum distance for lag compensation (meters)
    float maxLagCompensationDistance = 10.0f;
    
    // Number of consecutive position mismatches before correction
    uint32_t positionMismatchThreshold = 3;
    
    // ========================================================================
    // COMBAT VALIDATION
    // ========================================================================
    
    // Maximum allowed damage per hit
    int16_t maxDamagePerHit = 5000;  // 500 health in fixed-point
    
    // Maximum range for melee attacks (meters)
    float maxMeleeRange = 3.0f;
    
    // Maximum range for ranged attacks (meters)
    float maxRangedRange = 50.0f;
    
    // Maximum aim deviation tolerance (degrees)
    float maxAimDeviation = 30.0f;
    
    // Minimum time between damage applications (milliseconds)
    uint32_t damageCooldownMs = 100;
    
    // ========================================================================
    // STATISTICAL ANALYSIS
    // ========================================================================
    
    // Window size for behavior analysis (seconds)
    uint32_t behaviorAnalysisWindowSec = 60;
    
    // Accuracy threshold for aimbot detection (percentage)
    float aimbotAccuracyThreshold = 95.0f;
    
    // Minimum samples for statistical analysis
    uint32_t minSamplesForAnalysis = 10;
    
    // ========================================================================
    // RESPONSE SEVERITY THRESHOLDS
    // ========================================================================
    
    // Threshold for INFO response
    uint32_t infoThreshold = 1;
    
    // Threshold for WARNING response
    uint32_t warningThreshold = 2;
    
    // Threshold for SUSPICIOUS response
    uint32_t suspiciousThreshold = 3;
    
    // Threshold for CRITICAL (kick) response
    uint32_t criticalThreshold = 5;
    
    // Threshold for BAN response
    uint32_t banThreshold = 10;
    
    // ========================================================================
    // BAN/KICK CONFIGURATION
    // ========================================================================
    
    // Default ban duration for first offense (minutes)
    uint32_t defaultBanDurationMinutes = 60;
    
    // Ban duration multiplier for repeat offenders
    float banDurationMultiplier = 2.0f;
    
    // Maximum ban duration (minutes, 0 = permanent)
    uint32_t maxBanDurationMinutes = 10080;  // 1 week
    
    // Kick message to display to client
    const char* kickMessage = "Kicked for violation of server rules";
    
    // Ban message to display to client
    const char* banMessage = "Banned for suspected cheating";
};

// [SECURITY_AGENT] Global configuration instance
// Can be hot-reloaded at runtime
inline AntiCheatConfig& getAntiCheatConfig() {
    static AntiCheatConfig config;
    return config;
}

} // namespace Security
} // namespace DarkAges
