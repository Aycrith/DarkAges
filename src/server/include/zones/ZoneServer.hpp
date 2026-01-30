#pragma once

#include "ecs/CoreTypes.hpp"
#include "physics/SpatialHash.hpp"
#include "physics/MovementSystem.hpp"
#include "netcode/NetworkManager.hpp"
#include "db/RedisManager.hpp"
#include "db/ScyllaManager.hpp"
#include "zones/AreaOfInterest.hpp"
#include "zones/AuraProjection.hpp"
#include "zones/EntityMigration.hpp"
#include "zones/ZoneHandoff.hpp"
#include "zones/ReplicationOptimizer.hpp"
#include "combat/PositionHistory.hpp"
#include "combat/LagCompensatedCombat.hpp"
#include "combat/CombatSystem.hpp"
#include "security/AntiCheat.hpp"
#include "profiling/PerfettoProfiler.hpp"
#include "profiling/PerformanceMonitor.hpp"
#include "memory/MemoryPool.hpp"
#include "memory/FixedVector.hpp"
#include <memory>
#include <atomic>
#include <chrono>
#include <deque>
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string>
#include <csignal>

// [ZONE_AGENT] Main zone server class
// Manages all systems for a single zone/shard

namespace DarkAges {

// Snapshot history entry for delta compression
struct SnapshotHistory {
    uint32_t tick;
    std::vector<Protocol::EntityStateData> entities;
    std::chrono::steady_clock::time_point timestamp;
};

// Client snapshot acknowledgment tracking
struct ClientSnapshotState {
    uint32_t lastAcknowledgedTick{0};      // Last tick client confirmed received
    uint32_t lastSentTick{0};              // Last tick we sent
    uint32_t baselineTick{0};              // Current baseline for delta compression
    uint32_t snapshotSequence{0};          // Monotonic sequence counter
    std::vector<EntityID> pendingRemovals; // Entities to remove in next snapshot
};

struct ZoneConfig {
    uint32_t zoneId{1};
    uint16_t port{Constants::DEFAULT_SERVER_PORT};
    
    // World bounds for this zone
    float minX{Constants::WORLD_MIN_X};
    float maxX{Constants::WORLD_MAX_X};
    float minZ{Constants::WORLD_MIN_Z};
    float maxZ{Constants::WORLD_MAX_Z};
    
    // Database connections
    std::string redisHost{"localhost"};
    uint16_t redisPort{Constants::REDIS_DEFAULT_PORT};
    std::string scyllaHost{"localhost"};
    uint16_t scyllaPort{Constants::SCYLLA_DEFAULT_PORT};
    
    // Aura projection buffer (overlap with adjacent zones)
    float auraBuffer{Constants::AURA_BUFFER_METERS};
};

struct TickMetrics {
    uint64_t tickCount{0};
    uint64_t totalTickTimeUs{0};
    uint64_t maxTickTimeUs{0};
    uint64_t overruns{0};
    
    // Component times (microseconds)
    uint64_t networkTimeUs{0};
    uint64_t physicsTimeUs{0};
    uint64_t gameLogicTimeUs{0};
    uint64_t replicationTimeUs{0};
    
    void reset() {
        *this = TickMetrics{};
    }
};

// [ZONE_AGENT] Main server class
class ZoneServer {
public:
    ZoneServer();
    ~ZoneServer();
    
    // Initialize all systems
    bool initialize(const ZoneConfig& config);
    
    // Run main loop (blocking)
    void run();
    
    // Request shutdown (can be called from signal handlers)
    void requestShutdown();
    
    // Stop server (internal use)
    void stop();
    
    // Check if server is running
    [[nodiscard]] bool isRunning() const { return running_; }
    
    // Check if shutdown was requested
    [[nodiscard]] bool isShutdownRequested() const { return shutdownRequested_; }
    
    // Single tick update (for external loop control)
    bool tick();
    
    // Get current server time in milliseconds
    [[nodiscard]] uint32_t getCurrentTimeMs() const;
    
    // Get current tick number
    [[nodiscard]] uint32_t getCurrentTick() const { return currentTick_; }
    
    // Access subsystems
    [[nodiscard]] Registry& getRegistry() { return registry_; }
    [[nodiscard]] NetworkManager& getNetwork() { return *network_; }
    [[nodiscard]] SpatialHash& getSpatialHash() { return spatialHash_; }
    [[nodiscard]] MovementSystem& getMovementSystem() { return movementSystem_; }
    [[nodiscard]] RedisManager& getRedis() { return *redis_; }
    
    // Get metrics
    [[nodiscard]] const TickMetrics& getMetrics() const { return metrics_; }
    
    // Spawn player entity
    EntityID spawnPlayer(ConnectionID connectionId, uint64_t playerId, 
                        const std::string& username, const Position& spawnPos);
    
    // Despawn entity
    void despawnEntity(EntityID entity);
    
    // AOI debugging
    [[nodiscard]] AreaOfInterestSystem& getAOISystem() { return areaOfInterestSystem_; }

private:
    // System update phases
    void updateNetwork();
    void updatePhysics();
    void updateGameLogic();
    void updateReplication();
    void updateDatabase();
    
    // Client connection handlers
    void onClientConnected(ConnectionID connectionId);
    void onClientDisconnected(ConnectionID connectionId);
    void onClientInput(const ClientInputPacket& input);
    
    // Anti-cheat processing with server authority
    void validateAndApplyInput(EntityID entity, const ClientInputPacket& input);
    
    // [SECURITY_AGENT] Setup anti-cheat callbacks
    void initializeAntiCheat();
    
    // [SECURITY_AGENT] Handle cheat detection
    void onCheatDetected(uint64_t playerId, const Security::CheatDetectionResult& result);
    void onPlayerBanned(uint64_t playerId, const char* reason, uint32_t durationMinutes);
    void onPlayerKicked(uint64_t playerId, const char* reason);
    
    // [PHASE 3C] Combat processing with lag compensation
    void processAttackInput(EntityID entity, const ClientInputPacket& input);
    
    // Combat processing
    void processCombat();
    void onEntityDied(EntityID victim, EntityID killer);
    void sendCombatEvent(EntityID attacker, EntityID target, int16_t damage, const Position& location);
    void logCombatEvent(const HitResult& hit, EntityID attacker, EntityID target);
    
    // Performance monitoring
    void checkPerformanceBudgets(uint64_t tickTimeUs);
    void activateQoSDegradation();
    
    // Save player state to database
    void savePlayerState(EntityID entity);

private:
    // ECS registry
    Registry registry_;
    
    // Subsystems
    std::unique_ptr<NetworkManager> network_;
    std::unique_ptr<RedisManager> redis_;
    std::unique_ptr<ScyllaManager> scylla_;
    SpatialHash spatialHash_;
    MovementSystem movementSystem_;
    MovementValidator movementValidator_;
    BroadPhaseSystem broadPhaseSystem_;
    ReplicationOptimizer replicationOptimizer_;
    CombatSystem combatSystem_;
    HealthRegenSystem healthRegenSystem_;
    LagCompensator lagCompensator_;
    
    // [SECURITY_AGENT] Anti-cheat system
    Security::AntiCheatSystem antiCheat_;
    
    // Configuration
    ZoneConfig config_;
    
    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};
    uint32_t currentTick_{0};
    
    // Signal handling setup
    void setupSignalHandlers();
    
    // Graceful shutdown implementation
    void shutdown();
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point lastTickTime_;
    
    // Metrics
    TickMetrics metrics_;
    
    // [PERFORMANCE_AGENT] Profiling and monitoring
    std::unique_ptr<Profiling::PerformanceMonitor> perfMonitor_;
    bool profilingEnabled_{false};
    
    // [PERFORMANCE_AGENT] Memory pools for zero-allocation tick processing
    // Heap-allocated to avoid stack overflow (pools are 640KB - 1.25MB each)
    std::unique_ptr<Memory::SmallPool> smallPool_;
    std::unique_ptr<Memory::MediumPool> mediumPool_;
    std::unique_ptr<Memory::LargePool> largePool_;
    
    // Per-tick stack allocator for temporary data (1MB) - heap allocated to avoid stack overflow
    std::unique_ptr<Memory::StackAllocator> tempAllocator_;
    
    // Memory stats tracking
    struct MemoryStats {
        size_t tempAllocationsPerTick{0};
        size_t tempBytesUsed{0};
        size_t peakTempBytesUsed{0};
    } memoryStats_;
    
    // Client entity mapping
    std::unordered_map<ConnectionID, EntityID> connectionToEntity_;
    std::unordered_map<EntityID, ConnectionID> entityToConnection_;
    
    // QoS state
    bool qosDegraded_{false};
    uint32_t reducedUpdateRate_{Constants::SNAPSHOT_RATE_HZ};
    
    // Snapshot history for delta compression (last ~1 second)
    std::deque<SnapshotHistory> snapshotHistory_;
    static constexpr size_t MAX_SNAPSHOT_HISTORY = 60;  // 1 second at 60Hz
    
    // Per-client snapshot state
    std::unordered_map<ConnectionID, ClientSnapshotState> clientSnapshotState_;
    
    // Entities that were destroyed this tick (for removal notifications)
    std::vector<EntityID> destroyedEntities_;
    
    // Area of Interest system for entity filtering
    AreaOfInterestSystem areaOfInterestSystem_;
    
    // [PHASE 4B] Aura projection buffer for zone handoffs
    AuraProjectionManager auraManager_;
    
    // Aura sync interval (20Hz)
    static constexpr uint32_t AURA_SYNC_INTERVAL_MS = 50;
    uint32_t lastAuraSyncTime_{0};
    
    // Aura integration methods
    void syncAuraState();
    void handleAuraEntityMigration();
    void checkEntityZoneTransitions();
    
    // [PHASE 4C] Entity migration
    void onEntityMigrationComplete(EntityID entity, bool success);
    
    // Entity migration manager
    std::unique_ptr<EntityMigrationManager> migrationManager_;
    
    // [PHASE 4E] Zone handoff controller for seamless transitions
    std::unique_ptr<ZoneHandoffController> handoffController_;
    
    // Handoff integration methods
    void initializeHandoffController();
    void updateZoneHandoffs();
    void onHandoffStarted(uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success);
    void onHandoffCompleted(uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success);
    
    // Zone lookup callbacks for handoff controller
    ZoneDefinition* lookupZone(uint32_t zoneId);
    uint32_t findZoneByPosition(float x, float z);
};

} // namespace DarkAges
