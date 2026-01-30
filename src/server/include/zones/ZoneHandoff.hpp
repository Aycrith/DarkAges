#pragma once
#include "zones/EntityMigration.hpp"
#include "zones/AuraProjection.hpp"
#include "zones/ZoneDefinition.hpp"
#include "netcode/NetworkManager.hpp"
#include "ecs/CoreTypes.hpp"
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <cstdint>

namespace DarkAges {

// Handoff phases for seamless transition
enum class HandoffPhase {
    NONE,
    PREPARING,      // Detect approaching boundary, pre-warm target zone
    AURA_OVERLAP,   // Player in both zones (buffer region)
    MIGRATING,      // Transferring authority to target zone
    SWITCHING,      // Client switching connection to new zone
    COMPLETED       // Fully in new zone
};

// Handoff configuration
struct HandoffConfig {
    float preparationDistance = 75.0f;  // Start preparing at 75m from edge
    float auraEnterDistance = 50.0f;    // Enter aura at 50m
    float migrationDistance = 25.0f;    // Start migration at 25m
    float handoffDistance = 10.0f;      // Complete handoff at 10m
    
    uint32_t preparationTimeoutMs = 5000;
    uint32_t migrationTimeoutMs = 3000;
    uint32_t switchTimeoutMs = 2000;
};

// Active handoff tracking
struct ActiveHandoff {
    uint64_t playerId;
    EntityID entity;
    ConnectionID connection;
    
    uint32_t sourceZoneId;
    uint32_t targetZoneId;
    
    HandoffPhase phase;
    uint32_t phaseStartTime;
    
    // Progress tracking
    float distanceToEdge;
    glm::vec2 movementDirection;
    
    // Target zone connection info
    std::string targetZoneHost;
    uint16_t targetZonePort;
    
    // Handoff token for client auth
    std::string handoffToken;
};

// [ZONE_AGENT] Seamless zone handoff controller
// Manages the entire handoff process from detection through completion
class ZoneHandoffController {
public:
    using HandoffCallback = std::function<void(uint64_t playerId, uint32_t sourceZone, 
                                               uint32_t targetZone, bool success)>;
    using ZoneLookupCallback = std::function<ZoneDefinition*(uint32_t zoneId)>;
    using ZoneByPositionCallback = std::function<uint32_t(float x, float z)>;

public:
    ZoneHandoffController(uint32_t myZoneId, 
                         EntityMigrationManager* migration,
                         AuraProjectionManager* aura,
                         NetworkManager* network);
    
    // Initialize
    bool initialize(const HandoffConfig& config = HandoffConfig{});
    
    // Set zone lookup callbacks
    void setZoneLookupCallbacks(ZoneLookupCallback zoneLookup, ZoneByPositionCallback zoneByPos);
    
    // Set my zone definition (for distance calculations)
    void setMyZoneDefinition(const ZoneDefinition& zoneDef) { myZoneDef_ = zoneDef; }
    
    // Check if player should start handoff process
    void checkPlayerPosition(uint64_t playerId, EntityID entity, ConnectionID conn,
                            const Position& pos, const Velocity& vel,
                            uint32_t currentTimeMs);
    
    // Update handoff process (call every tick)
    void update(Registry& registry, uint32_t currentTimeMs);
    
    // Handle handoff token validation from client
    bool validateHandoffToken(uint64_t playerId, const std::string& token);
    
    // Complete handoff (called by target zone via CrossZoneMessenger)
    void completeHandoff(uint64_t playerId, bool success);
    
    // Cancel ongoing handoff (player turned back, disconnected, etc.)
    bool cancelHandoff(uint64_t playerId);
    
    // Get current handoff phase for player
    HandoffPhase getPlayerPhase(uint64_t playerId) const;
    
    // Check if player is in any handoff phase
    bool isHandoffInProgress(uint64_t playerId) const;
    
    // Get all active handoffs (for monitoring/debugging)
    [[nodiscard]] const std::unordered_map<uint64_t, ActiveHandoff>& getActiveHandoffs() const {
        return activeHandoffs_;
    }
    
    // Set callbacks
    void setOnHandoffStarted(HandoffCallback cb) { onHandoffStarted_ = cb; }
    void setOnHandoffCompleted(HandoffCallback cb) { onHandoffCompleted_ = cb; }
    
    // Get handoff statistics
    struct HandoffStats {
        uint32_t totalHandoffs{0};
        uint32_t successfulHandoffs{0};
        uint32_t failedHandoffs{0};
        uint32_t cancelledHandoffs{0};
        uint32_t timeoutCount{0};
        uint32_t avgHandoffTimeMs{0};
        uint32_t maxHandoffTimeMs{0};
    };
    [[nodiscard]] const HandoffStats& getStats() const { return stats_; }

private:
    uint32_t myZoneId_;
    ZoneDefinition myZoneDef_;
    HandoffConfig config_;
    
    EntityMigrationManager* migration_;
    AuraProjectionManager* aura_;
    NetworkManager* network_;
    
    std::unordered_map<uint64_t, ActiveHandoff> activeHandoffs_;
    HandoffStats stats_;
    
    // Callbacks
    HandoffCallback onHandoffStarted_;
    HandoffCallback onHandoffCompleted_;
    ZoneLookupCallback zoneLookup_;
    ZoneByPositionCallback zoneByPosition_;
    
    // Phase handlers
    void enterPreparation(ActiveHandoff& handoff, uint32_t targetZoneId, uint32_t currentTimeMs);
    void enterAuraOverlap(ActiveHandoff& handoff, uint32_t currentTimeMs);
    void enterMigration(ActiveHandoff& handoff, uint32_t currentTimeMs, Registry& registry);
    void enterSwitching(ActiveHandoff& handoff, uint32_t currentTimeMs);
    void completeMigration(ActiveHandoff& handoff, uint32_t currentTimeMs);
    void failHandoff(ActiveHandoff& handoff, const std::string& reason);
    
    // Helpers
    uint32_t determineTargetZone(const Position& pos, const Velocity& vel) const;
    float calculateDistanceToEdge(const Position& pos) const;
    glm::vec2 calculateMovementDirection(const Velocity& vel) const;
    std::string generateHandoffToken(uint64_t playerId);
    void sendHandoffInstructionToClient(const ActiveHandoff& handoff);
    void updateStats(uint32_t handoffTimeMs, bool success);
};

} // namespace DarkAges
