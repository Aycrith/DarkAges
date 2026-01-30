// [ZONE_AGENT] Main zone server implementation
// Orchestrates all systems for a single game zone

#include "zones/ZoneServer.hpp"
#include "zones/ReplicationOptimizer.hpp"
#include "zones/EntityMigration.hpp"
#include "combat/LagCompensatedCombat.hpp"
#include "profiling/PerfettoProfiler.hpp"
#include "profiling/PerformanceMonitor.hpp"
#include "monitoring/MetricsExporter.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <ctime>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Profiling macros that compile out when profiling is disabled
#ifdef ENABLE_PROFILING
    #define ZONE_TRACE_EVENT(name, cat) TRACE_EVENT(name, cat)
    #define ZONE_TRACE_BEGIN(name, cat) TRACE_BEGIN(name, cat)
    #define ZONE_TRACE_END(name, cat) TRACE_END(name, cat)
    #define ZONE_TRACE_INSTANT(name, cat) TRACE_INSTANT(name, cat)
    #define ZONE_TRACE_COUNTER(name, val) TRACE_COUNTER(name, val)
#else
    #define ZONE_TRACE_EVENT(name, cat)
    #define ZONE_TRACE_BEGIN(name, cat)
    #define ZONE_TRACE_END(name, cat)
    #define ZONE_TRACE_INSTANT(name, cat)
    #define ZONE_TRACE_COUNTER(name, val)
#endif

namespace DarkAges {

ZoneServer::ZoneServer() 
    : auraManager_(1),  // Default zone ID, will be updated in initialize
      tempAllocator_(std::make_unique<Memory::StackAllocator>(1024 * 1024)),
      smallPool_(std::make_unique<Memory::SmallPool>()),
      mediumPool_(std::make_unique<Memory::MediumPool>()),
      largePool_(std::make_unique<Memory::LargePool>()) {
}
ZoneServer::~ZoneServer() = default;

bool ZoneServer::initialize(const ZoneConfig& config) {
    config_ = config;
    
    std::cout << "[ZONE " << config_.zoneId << "] Initializing..." << std::endl;
    
    // [PHASE 4C] Initialize entity migration manager
    migrationManager_ = std::make_unique<EntityMigrationManager>(config_.zoneId, redis_.get());
    migrationManager_->setZonePortLookup([this](uint32_t zoneId) -> uint16_t {
        // Simple port calculation: base port + zoneId
        // In production, this would query a service registry
        return Constants::DEFAULT_SERVER_PORT + static_cast<uint16_t>(zoneId) - 1;
    });
    
    std::cout << "[ZONE " << config_.zoneId << "] Entity migration initialized" << std::endl;
    
    // [PHASE 4E] Initialize zone handoff controller
    initializeHandoffController();
    
    // [PHASE 4B] Initialize aura projection manager
    auraManager_ = AuraProjectionManager(config_.zoneId);
    
    // Load adjacent zone definitions from config/Redis
    std::vector<ZoneDefinition> adjacentZones;
    // TODO: Populate from configuration or service discovery
    auraManager_.initialize(adjacentZones);
    
    std::cout << "[ZONE " << config_.zoneId << "] Aura projection initialized" << std::endl;
    
    // Initialize network
    network_ = std::make_unique<NetworkManager>();
    if (!network_->initialize(config_.port)) {
        std::cerr << "[ZONE " << config_.zoneId << "] Failed to initialize network!" << std::endl;
        return false;
    }
    
    // Set up network callbacks
    network_->setOnClientConnected([this](ConnectionID connId) {
        onClientConnected(connId);
    });
    network_->setOnClientDisconnected([this](ConnectionID connId) {
        onClientDisconnected(connId);
    });
    
    // Initialize Redis
    redis_ = std::make_unique<RedisManager>();
    if (!redis_->initialize(config_.redisHost, config_.redisPort)) {
        std::cerr << "[ZONE " << config_.zoneId << "] Failed to connect to Redis!" << std::endl;
        // Non-fatal for Phase 0 - can run without Redis
        std::cout << "[ZONE " << config_.zoneId << "] Continuing without Redis..." << std::endl;
    }
    
    // Initialize ScyllaDB for combat logging
    scylla_ = std::make_unique<ScyllaManager>();
    if (!scylla_->initialize(config_.scyllaHost, config_.scyllaPort)) {
        std::cerr << "[ZONE " << config_.zoneId << "] Failed to connect to ScyllaDB" << std::endl;
        // Non-fatal - can run without logging
        std::cout << "[ZONE " << config_.zoneId << "] Continuing without ScyllaDB..." << std::endl;
    } else {
        std::cout << "[ZONE " << config_.zoneId << "] ScyllaDB connected for combat logging" << std::endl;
    }
    
    // Initialize ECS - nothing special needed for EnTT
    
    // Record start time
    startTime_ = std::chrono::steady_clock::now();
    lastTickTime_ = startTime_;
    
    // Set up combat callbacks
    combatSystem_.setOnDeath([this](EntityID victim, EntityID killer) {
        onEntityDied(victim, killer);
    });
    
    combatSystem_.setOnDamage([this](EntityID attacker, EntityID target, 
                                     int16_t damage, const Position& location) {
        sendCombatEvent(attacker, target, damage, location);
    });
    
    // [SECURITY_AGENT] Initialize anti-cheat system
    initializeAntiCheat();
    
    // [PERFORMANCE_AGENT] Initialize profiler if enabled
#ifdef ENABLE_PROFILING
    profilingEnabled_ = true;
    std::string tracePath = "zone_" + std::to_string(config_.zoneId) + "_trace.json";
    Profiling::PerfettoProfiler::instance().initialize(tracePath);
    
    // Start performance monitor
    perfMonitor_ = std::make_unique<Profiling::PerformanceMonitor>();
    perfMonitor_->start(30000);  // 30 second report interval
    
    std::cout << "[ZONE " << config_.zoneId << "] Profiling enabled, trace: " << tracePath << std::endl;
#endif
    
    // [DEVOPS_AGENT] Initialize metrics exporter
    uint16_t metricsPort = static_cast<uint16_t>(8080 + config_.zoneId - 1);
    if (!Monitoring::MetricsExporter::Instance().Initialize(metricsPort)) {
        std::cerr << "[ZONE " << config_.zoneId << "] Warning: Failed to start metrics exporter" << std::endl;
        // Non-fatal - server can run without metrics
    } else {
        std::cout << "[ZONE " << config_.zoneId << "] Metrics exporter on port " << metricsPort << std::endl;
    }
    
    std::cout << "[ZONE " << config_.zoneId << "] Initialization complete" << std::endl;
    return true;
}

void ZoneServer::run() {
    std::cout << "[ZONE " << config_.zoneId << "] Starting main loop..." << std::endl;
    
    setupSignalHandlers();
    running_ = true;
    shutdownRequested_ = false;
    
    // Main game loop at 60Hz (16.67ms per tick)
    constexpr auto tickInterval = std::chrono::microseconds(16667);
    auto nextTick = std::chrono::steady_clock::now();
    uint32_t tickCount = 0;
    
    std::cout << "[ZONE " << config_.zoneId << "] Server running at 60Hz on port " << config_.port << std::endl;
    
    while (running_) {
        auto frameStart = std::chrono::steady_clock::now();
        
        // Execute one game tick
        tick();
        tickCount++;
        
        // Frame rate limiting
        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
        
        if (elapsed < tickInterval) {
            // Sleep to maintain 60Hz
            std::this_thread::sleep_for(tickInterval - elapsed);
        } else if (elapsed > tickInterval * 2) {
            // Frame overrun warning (only log every 60 ticks to avoid spam)
            if (tickCount % 60 == 0) {
                std::cerr << "[ZONE " << config_.zoneId << "] Tick overrun: " << elapsed.count() 
                          << " us (budget: " << tickInterval.count() << " us)" << std::endl;
            }
        }
        
        nextTick += tickInterval;
    }
    
    std::cout << "[ZONE " << config_.zoneId << "] Main loop ended after " << tickCount << " ticks" << std::endl;
    shutdown();
}

// Global pointer for signal handler access
static ZoneServer* g_zoneServerInstance = nullptr;

void ZoneServer::setupSignalHandlers() {
    // Store global pointer for signal handlers
    g_zoneServerInstance = this;
    
    #ifdef _WIN32
    // Windows signal handling
    std::signal(SIGINT, [](int) {
        std::cout << "[SIGNAL] SIGINT received, requesting shutdown..." << std::endl;
        if (g_zoneServerInstance) {
            g_zoneServerInstance->requestShutdown();
        }
    });
    std::signal(SIGTERM, [](int) {
        std::cout << "[SIGNAL] SIGTERM received, requesting shutdown..." << std::endl;
        if (g_zoneServerInstance) {
            g_zoneServerInstance->requestShutdown();
        }
    });
    #else
    // Unix signal handling
    struct sigaction sa;
    sa.sa_handler = [](int sig) {
        std::cout << "[SIGNAL] Signal " << sig << " received, requesting shutdown..." << std::endl;
        if (g_zoneServerInstance) {
            g_zoneServerInstance->requestShutdown();
        }
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    #endif
}

void ZoneServer::requestShutdown() {
    if (!shutdownRequested_) {
        shutdownRequested_ = true;
        running_ = false;
        std::cout << "[ZONE " << config_.zoneId << "] Shutdown requested" << std::endl;
    }
}

void ZoneServer::shutdown() {
    std::cout << "[ZONE " << config_.zoneId << "] Shutting down ZoneServer..." << std::endl;
    
    // [PERFORMANCE_AGENT] Shutdown profiler
    if (profilingEnabled_) {
        if (perfMonitor_) {
            perfMonitor_->stop();
        }
        Profiling::PerfettoProfiler::instance().shutdown();
    }
    
    // Save player states
    for (const auto& [connId, entityId] : connectionToEntity_) {
        savePlayerState(entityId);
    }
    
    // Flush database writes
    if (scylla_ && scylla_->isConnected()) {
        std::cout << "[ZONE " << config_.zoneId << "] Processing pending database writes..." << std::endl;
        // Process any pending async operations
        scylla_->update();
    }
    
    // Stop network
    if (network_) {
        network_->shutdown();
    }
    
    // [DEVOPS_AGENT] Shutdown metrics exporter
    Monitoring::MetricsExporter::Instance().Shutdown();
    
    std::cout << "[ZONE " << config_.zoneId << "] ZoneServer shutdown complete" << std::endl;
}

void ZoneServer::stop() {
    requestShutdown();
}

void ZoneServer::savePlayerState(EntityID entity) {
    // Get player info
    const PlayerInfo* info = registry_.try_get<PlayerInfo>(entity);
    if (!info) return;
    
    // Get current position
    const Position* pos = registry_.try_get<Position>(entity);
    
    uint32_t currentTimeMs = getCurrentTimeMs();
    
    // [DATABASE_AGENT] Save to Redis for hot state (fast recovery)
    if (redis_ && redis_->isConnected()) {
        PlayerSession session;
        session.playerId = info->playerId;
        session.zoneId = config_.zoneId;
        session.connectionId = info->connectionId;
        if (pos) {
            session.position = *pos;
        }
        // Get health from combat state if available
        if (const CombatState* combat = registry_.try_get<CombatState>(entity)) {
            session.health = combat->health;
        } else {
            session.health = Constants::DEFAULT_HEALTH;
        }
        session.lastActivity = currentTimeMs;
        std::strncpy(session.username, info->username, sizeof(session.username) - 1);
        session.username[sizeof(session.username) - 1] = '\0';
        
        // Save session with 1 hour TTL
        redis_->savePlayerSession(session, [playerId = info->playerId](bool success) {
            if (!success) {
                std::cerr << "[REDIS] Failed to save session for player " << playerId << std::endl;
            }
        });
        
        // Also update position separately for quick lookups
        if (pos) {
            redis_->updatePlayerPosition(info->playerId, *pos, currentTimeMs);
        }
        
        // Update zone player set
        redis_->addPlayerToZone(config_.zoneId, info->playerId);
    }
    
    // [DATABASE_AGENT] Save to ScyllaDB for persistence (async, fire-and-forget)
    if (scylla_ && scylla_->isConnected()) {
        // TODO: Implement player state persistence
        // This would save: position, inventory, stats, etc. to player_sessions table
        // For now, we rely on Redis hot state for session recovery
    }
}

bool ZoneServer::tick() {
    ZONE_TRACE_EVENT("ZoneServer::tick", Profiling::TraceCategory::TOTAL);
    
    // [PERFORMANCE_AGENT] Reset temporary allocator at start of tick
    // This ensures zero heap allocations during tick processing
    memoryStats_.tempBytesUsed = tempAllocator_->getUsed();
    memoryStats_.peakTempBytesUsed = std::max(memoryStats_.peakTempBytesUsed, memoryStats_.tempBytesUsed);
    tempAllocator_->reset();
    memoryStats_.tempAllocationsPerTick = 0;
    
    auto tickStart = std::chrono::steady_clock::now();
    uint32_t currentTimeMs = getCurrentTimeMs();
    
    // Update all systems in order
    {
        ZONE_TRACE_EVENT("updateNetwork", Profiling::TraceCategory::NETWORK);
        updateNetwork();
    }
    
    {
        ZONE_TRACE_EVENT("updatePhysics", Profiling::TraceCategory::PHYSICS);
        updatePhysics();
    }
    
    {
        ZONE_TRACE_EVENT("updateGameLogic", Profiling::TraceCategory::GAME_LOGIC);
        updateGameLogic();
    }
    
    {
        ZONE_TRACE_EVENT("updateReplication", Profiling::TraceCategory::REPLICATION);
        updateReplication();
    }
    
    {
        ZONE_TRACE_EVENT("updateDatabase", Profiling::TraceCategory::DATABASE);
        updateDatabase();
    }
    
    // Calculate tick time
    auto tickEnd = std::chrono::steady_clock::now();
    auto tickTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(
        tickEnd - tickStart).count();
    
    // Update metrics
    metrics_.tickCount++;
    metrics_.totalTickTimeUs += tickTimeUs;
    metrics_.maxTickTimeUs = std::max<uint64_t>(metrics_.maxTickTimeUs, tickTimeUs);
    
    // Update performance monitor
    if (perfMonitor_) {
        perfMonitor_->recordTickTime(tickTimeUs);
    }
    
    // [DEVOPS_AGENT] Update Prometheus metrics
    auto& metrics = Monitoring::MetricsExporter::Instance();
    double tickTimeMs = tickTimeUs / 1000.0;
    
    metrics.TicksTotal().Increment({{"zone_id", std::to_string(config_.zoneId)}});
    metrics.TickDurationMs().Set(tickTimeMs, {{"zone_id", std::to_string(config_.zoneId)}});
    metrics.TickDurationHistogram().Observe(tickTimeMs);
    metrics.PlayerCount().Set(static_cast<double>(connectionToEntity_.size()), 
                              {{"zone_id", std::to_string(config_.zoneId)}});
    
    // Memory metrics (convert bytes to MB for readability)
    size_t memoryUsed = tempAllocator_->getUsed();
    metrics.MemoryUsedBytes().Set(static_cast<double>(memoryUsed),
                                   {{"zone_id", std::to_string(config_.zoneId)}});
    metrics.MemoryTotalBytes().Set(1024.0 * 1024.0 * 1024.0,  // 1GB assumption
                                   {{"zone_id", std::to_string(config_.zoneId)}});
    
    // Database connection status
    bool dbConnected = redis_ && redis_->isConnected();
    metrics.DbConnected().Set(dbConnected ? 1.0 : 0.0,
                              {{"zone_id", std::to_string(config_.zoneId)}});
    
    // Trace counters
    ZONE_TRACE_COUNTER("tick_time_us", static_cast<int64_t>(tickTimeUs));
    ZONE_TRACE_COUNTER("entity_count", static_cast<int64_t>(registry_.alive()));
    ZONE_TRACE_COUNTER("player_count", static_cast<int64_t>(connectionToEntity_.size()));
    
    // Check performance budgets
    checkPerformanceBudgets(tickTimeUs);
    
    // Increment tick counter
    currentTick_++;
    
    // Frame rate limiting - sleep to maintain 60Hz
    auto targetTickTime = std::chrono::microseconds(Constants::TICK_BUDGET_US);
    auto elapsed = tickEnd - tickStart;
    if (elapsed < targetTickTime) {
        std::this_thread::sleep_for(targetTickTime - elapsed);
    }
    
    return running_;
}

uint32_t ZoneServer::getCurrentTimeMs() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count()
    );
}

void ZoneServer::updateNetwork() {
    auto start = std::chrono::steady_clock::now();
    
    ZONE_TRACE_EVENT("NetworkManager::update", Profiling::TraceCategory::NETWORK);
    network_->update(getCurrentTimeMs());
    
    // Process pending inputs
    auto inputs = network_->getPendingInputs();
    for (const auto& input : inputs) {
        onClientInput(input);
    }
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    metrics_.networkTimeUs += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

void ZoneServer::updatePhysics() {
    auto start = std::chrono::steady_clock::now();
    
    uint32_t currentTimeMs = getCurrentTimeMs();
    
    // Update spatial hash
    ZONE_TRACE_EVENT("BroadPhase::update", Profiling::TraceCategory::PHYSICS);
    broadPhaseSystem_.update(registry_, spatialHash_);
    
    // Update movement for all entities
    ZONE_TRACE_EVENT("MovementSystem::update", Profiling::TraceCategory::PHYSICS);
    movementSystem_.update(registry_, currentTimeMs);
    
    // Collision detection and resolution
    ZONE_TRACE_EVENT("Collision::resolve", Profiling::TraceCategory::PHYSICS);
    std::vector<std::pair<EntityID, EntityID>> collisionPairs;
    broadPhaseSystem_.findPotentialPairs(registry_, spatialHash_, collisionPairs);
    
    // Resolve collisions
    for (const auto& [entityA, entityB] : collisionPairs) {
        Position* posA = registry_.try_get<Position>(entityA);
        Position* posB = registry_.try_get<Position>(entityB);
        BoundingVolume* bvA = registry_.try_get<BoundingVolume>(entityA);
        BoundingVolume* bvB = registry_.try_get<BoundingVolume>(entityB);
        
        if (posA && posB && bvA && bvB) {
            movementSystem_.resolveSoftCollision(*posA, *posB, bvA->radius, bvB->radius);
            movementSystem_.resolveSoftCollision(*posB, *posA, bvB->radius, bvA->radius);
        }
    }
    
    // [COMBAT_AGENT] Record positions for lag compensation (2-second history buffer)
    // This enables server-side rewind hit validation for combat
    lagCompensator_.updateAllEntities(registry_, currentTimeMs);
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    metrics_.physicsTimeUs += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

void ZoneServer::updateGameLogic() {
    auto start = std::chrono::steady_clock::now();
    
    // Process combat inputs (attacks triggered by client input)
    ZONE_TRACE_EVENT("Combat::process", Profiling::TraceCategory::GAME_LOGIC);
    processCombat();
    
    // Health regeneration
    healthRegenSystem_.update(registry_, getCurrentTimeMs());
    
    // [PHASE 4] Zone transitions and migration
    checkEntityZoneTransitions();
    
    // [PHASE 4C] Update entity migrations
    if (migrationManager_) {
        migrationManager_->update(registry_, getCurrentTimeMs());
    }
    
    // [PHASE 4E] Update zone handoffs
    updateZoneHandoffs();
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    metrics_.gameLogicTimeUs += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

void ZoneServer::processCombat() {
    // Combat is triggered by client inputs (attack action)
    // The actual combat processing happens in validateAndApplyInput
    // when an attack input is received
}

void ZoneServer::onEntityDied(EntityID victim, EntityID killer) {
    std::cout << "[ZONE " << config_.zoneId << "] Entity " << static_cast<uint32_t>(victim) 
              << " killed by " << static_cast<uint32_t>(killer) << std::endl;
    
    // Get victim and killer info
    uint64_t victimPlayerId = 0;
    uint64_t killerPlayerId = 0;
    std::string victimName = "NPC";
    std::string killerName = "NPC";
    Position killLocation{0, 0, 0, 0};
    
    if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(victim)) {
        victimPlayerId = info->playerId;
        victimName = info->username;
    }
    if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(killer)) {
        killerPlayerId = info->playerId;
        killerName = info->username;
    }
    if (const Position* pos = registry_.try_get<Position>(victim)) {
        killLocation = *pos;
    }
    
    // Send death event to clients
    // TODO: Implement event packet broadcast
    
    // [DATABASE_AGENT] Log kill event to ScyllaDB
    if (scylla_ && scylla_->isConnected()) {
        // Log the kill for the killer
        if (killerPlayerId > 0) {
            CombatEvent killEvent;
            killEvent.eventId = 0;
            killEvent.timestamp = getCurrentTimeMs();
            killEvent.zoneId = config_.zoneId;
            killEvent.attackerId = killerPlayerId;
            killEvent.targetId = victimPlayerId;
            killEvent.eventType = "kill";
            killEvent.damageAmount = 0;  // Final blow already logged as damage
            killEvent.isCritical = false;
            killEvent.weaponType = "melee";
            killEvent.position = killLocation;
            killEvent.serverTick = currentTick_;
            
            scylla_->logCombatEvent(killEvent, nullptr);  // No callback needed
        }
        
        // Log the death for the victim
        if (victimPlayerId > 0) {
            CombatEvent deathEvent;
            deathEvent.eventId = 0;
            deathEvent.timestamp = getCurrentTimeMs();
            deathEvent.zoneId = config_.zoneId;
            deathEvent.attackerId = killerPlayerId;
            deathEvent.targetId = victimPlayerId;
            deathEvent.eventType = "death";
            deathEvent.damageAmount = 0;
            deathEvent.isCritical = false;
            deathEvent.weaponType = "melee";
            deathEvent.position = killLocation;
            deathEvent.serverTick = currentTick_;
            
            scylla_->logCombatEvent(deathEvent, nullptr);
        }
        
        // Update player stats if both are players
        if (killerPlayerId > 0 && victimPlayerId > 0) {
            // Get current date in YYYYMMDD format
            time_t now = time(nullptr);
            struct tm* tm_info = localtime(&now);
            uint32_t sessionDate = (tm_info->tm_year + 1900) * 10000 + 
                                   (tm_info->tm_mon + 1) * 100 + 
                                   tm_info->tm_mday;
            
            // Update killer stats
            PlayerCombatStats killerStats;
            killerStats.playerId = killerPlayerId;
            killerStats.sessionDate = sessionDate;
            killerStats.kills = 1;
            killerStats.deaths = 0;
            killerStats.damageDealt = 0;  // Tracked separately
            killerStats.damageTaken = 0;
            killerStats.longestKillStreak = 0;
            killerStats.currentKillStreak = 0;
            
            scylla_->updatePlayerStats(killerStats, [killerName](bool success) {
                if (!success) {
                    std::cerr << "[SCYLLA] Failed to update stats for killer: " << killerName << std::endl;
                }
            });
            
            // Update victim stats
            PlayerCombatStats victimStats;
            victimStats.playerId = victimPlayerId;
            victimStats.sessionDate = sessionDate;
            victimStats.kills = 0;
            victimStats.deaths = 1;
            victimStats.damageDealt = 0;
            victimStats.damageTaken = 0;
            victimStats.longestKillStreak = 0;
            victimStats.currentKillStreak = 0;
            
            scylla_->updatePlayerStats(victimStats, [victimName](bool success) {
                if (!success) {
                    std::cerr << "[SCYLLA] Failed to update stats for victim: " << victimName << std::endl;
                }
            });
        }
    }
    
    // Schedule respawn
    // TODO: Implement respawn timer
}

void ZoneServer::sendCombatEvent(EntityID attacker, EntityID target, int16_t damage, const Position& location) {
    // TODO: Send EventPacket to clients
    // This would serialize the damage event and broadcast to relevant players
    
    // [DATABASE_AGENT] Log combat event to ScyllaDB for analytics
    HitResult hit;
    hit.hit = true;
    hit.target = target;
    hit.damageDealt = damage;
    hit.hitLocation = location;
    hit.hitType = "damage";
    hit.isCritical = false;  // Will be set by combat system
    logCombatEvent(hit, attacker, target);
}

void ZoneServer::logCombatEvent(const HitResult& hit, EntityID attacker, EntityID target) {
    if (!scylla_ || !scylla_->isConnected()) return;
    
    // Get player IDs from PlayerInfo - use persistent IDs, not entity IDs
    uint64_t attackerPlayerId = 0;
    uint64_t targetPlayerId = 0;
    std::string attackerName = "Unknown";
    std::string targetName = "Unknown";
    
    if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(attacker)) {
        attackerPlayerId = info->playerId;
        attackerName = info->username;
    }
    if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(target)) {
        targetPlayerId = info->playerId;
        targetName = info->username;
    }
    
    // Skip logging if neither party is a player (NPC vs NPC not logged)
    if (attackerPlayerId == 0 && targetPlayerId == 0) {
        return;
    }
    
    CombatEvent event;
    event.eventId = 0;  // Will be generated by UUID
    event.timestamp = getCurrentTimeMs();
    event.zoneId = config_.zoneId;
    event.attackerId = attackerPlayerId;
    event.targetId = targetPlayerId;
    event.eventType = hit.hitType ? hit.hitType : "damage";
    event.damageAmount = hit.damageDealt;
    event.isCritical = hit.isCritical;
    event.weaponType = "melee";  // TODO: Track actual weapon from attacker equipment
    event.position = hit.hitLocation;
    event.serverTick = currentTick_;
    
    scylla_->logCombatEvent(event, [attackerPlayerId, targetPlayerId, attackerName, targetName](bool success) {
        if (!success) {
            std::cerr << "[SCYLLA] Failed to log combat event: " 
                      << attackerName << "(" << attackerPlayerId << ") -> " 
                      << targetName << "(" << targetPlayerId << ")" << std::endl;
        }
    });
}

void ZoneServer::updateReplication() {
    auto start = std::chrono::steady_clock::now();
    
    // [PHASE 4B] Sync aura state with adjacent zones
    ZONE_TRACE_EVENT("Aura::syncState", Profiling::TraceCategory::REPLICATION);
    syncAuraState();
    
    // Only send snapshots at snapshot rate (20Hz default)
    static uint32_t lastSnapshotTick = 0;
    uint32_t ticksPerSnapshot = Constants::TICK_RATE_HZ / reducedUpdateRate_;
    
    if (currentTick_ - lastSnapshotTick >= ticksPerSnapshot) {
        lastSnapshotTick = currentTick_;
        
        // [ZONE_AGENT] Store full world state in history for delta compression
        // This is the authoritative state for this tick
        std::vector<Protocol::EntityStateData> allEntities;
        auto view = registry_.view<Position, Velocity, Rotation>(entt::exclude<StaticTag>);
        // Note: EnTT view doesn't have size(), we'll grow dynamically
        
        for (auto entity : view) {
            Protocol::EntityStateData state{};
            state.entity = entity;
            state.position = view.get<Position>(entity);
            state.velocity = view.get<Velocity>(entity);
            state.rotation = view.get<Rotation>(entity);
            
            if (const CombatState* combat = registry_.try_get<CombatState>(entity)) {
                state.healthPercent = static_cast<uint8_t>(combat->healthPercent());
            } else {
                state.healthPercent = 100;
            }
            
            state.animState = 0;
            
            if (registry_.all_of<PlayerTag>(entity)) {
                state.entityType = 0;
            } else if (registry_.all_of<ProjectileTag>(entity)) {
                state.entityType = 1;
            } else if (registry_.all_of<NPCTag>(entity)) {
                state.entityType = 3;
            } else {
                state.entityType = 2;
            }
            
            allEntities.push_back(state);
        }
        
        // Store snapshot in history
        SnapshotHistory history;
        history.tick = currentTick_;
        history.entities = std::move(allEntities);
        history.timestamp = std::chrono::steady_clock::now();
        snapshotHistory_.push_back(std::move(history));
        
        // Clean up old snapshots (keep 1 second worth)
        while (snapshotHistory_.size() > MAX_SNAPSHOT_HISTORY) {
            snapshotHistory_.pop_front();
        }
        
        // [ZONE_AGENT] Send personalized snapshots to each player using spatial optimization
        for (const auto& [connId, entityId] : connectionToEntity_) {
            const Position* viewerPos = registry_.try_get<Position>(entityId);
            if (!viewerPos) continue;
            
            // Calculate priorities for all visible entities using spatial hash
            auto priorities = replicationOptimizer_.calculatePriorities(
                registry_, spatialHash_, entityId, *viewerPos, currentTick_);
            
            // Filter by update rate based on priority (near=20Hz, mid=10Hz, far=5Hz)
            auto entitiesToUpdate = replicationOptimizer_.filterByUpdateRate(priorities, currentTick_);
            
            if (entitiesToUpdate.empty() && destroyedEntities_.empty()) {
                continue;  // Nothing to send this tick
            }
            
            // Build entity states for this player's visible entities
            auto entityStates = replicationOptimizer_.buildEntityStates(
                registry_, entitiesToUpdate, currentTick_);
            
            // Apply data culling based on priority to reduce bandwidth
            for (auto& state : entityStates) {
                // Find priority for this entity
                int priority = 2;  // Default to far
                for (const auto& p : priorities) {
                    if (p.entity == state.entity) {
                        priority = p.priority;
                        break;
                    }
                }
                ReplicationOptimizer::cullNonEssentialFields(state, priority);
            }
            
            // Get client snapshot state for delta compression
            auto& clientState = clientSnapshotState_[connId];
            
            // Find baseline for delta compression
            uint32_t baselineTick = 0;
            std::vector<Protocol::EntityStateData> baselineEntities;
            
            if (clientState.lastAcknowledgedTick > 0 && 
                clientState.lastAcknowledgedTick >= currentTick_ - MAX_SNAPSHOT_HISTORY) {
                for (const auto& hist : snapshotHistory_) {
                    if (hist.tick == clientState.lastAcknowledgedTick) {
                        baselineTick = clientState.lastAcknowledgedTick;
                        baselineEntities = hist.entities;
                        break;
                    }
                }
            }
            
            // Create and send delta snapshot
            auto snapshotData = Protocol::createDeltaSnapshot(
                currentTick_,
                baselineTick,
                entityStates,
                destroyedEntities_,
                baselineEntities
            );
            
            network_->sendSnapshot(connId, snapshotData);
            
            // Mark entities as updated for this client
            for (EntityID e : entitiesToUpdate) {
                replicationOptimizer_.markEntityUpdated(connId, e, currentTick_);
            }
            
            clientState.lastSentTick = currentTick_;
            clientState.baselineTick = baselineTick > 0 ? baselineTick : currentTick_;
            clientState.snapshotSequence++;
        }
        
        destroyedEntities_.clear();
    }
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    metrics_.replicationTimeUs += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

void ZoneServer::updateDatabase() {
    ZONE_TRACE_EVENT("Database::update", Profiling::TraceCategory::DATABASE);
    
    // Update Redis async operations
    if (redis_ && redis_->isConnected()) {
        // Process async callbacks and pending commands
        redis_->update();
        // Process pub/sub messages for cross-zone communication
        redis_->processSubscriptions();
    }
    
    // Update ScyllaDB async operations (process pending writes)
    if (scylla_ && scylla_->isConnected()) {
        scylla_->update();
    }
}

void ZoneServer::onClientConnected(ConnectionID connectionId) {
    std::cout << "[ZONE " << config_.zoneId << "] Client connected: " << connectionId << std::endl;
    
    // Spawn player entity
    Position spawnPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), getCurrentTimeMs());
    EntityID entity = spawnPlayer(connectionId, connectionId, "Player" + std::to_string(connectionId), spawnPos);
    
    std::cout << "[ZONE " << config_.zoneId << "] Spawned entity " << static_cast<uint32_t>(entity) 
              << " for connection " << connectionId << std::endl;
}

void ZoneServer::onClientDisconnected(ConnectionID connectionId) {
    std::cout << "[ZONE " << config_.zoneId << "] Client disconnected: " << connectionId << std::endl;
    
    // Find entity and clean up anti-cheat profile
    auto it = connectionToEntity_.find(connectionId);
    if (it != connectionToEntity_.end()) {
        EntityID entity = it->second;
        
        // [SECURITY_AGENT] Clean up anti-cheat behavior profile
        if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(entity)) {
            antiCheat_.removeProfile(info->playerId);
        }
        
        despawnEntity(entity);
        connectionToEntity_.erase(it);
    }
    
    // Clean up client snapshot state
    clientSnapshotState_.erase(connectionId);
    
    // [ZONE_AGENT] Clean up replication optimizer tracking for disconnected player
    replicationOptimizer_.removeClientTracking(connectionId);
}

void ZoneServer::onClientInput(const ClientInputPacket& input) {
    // Find entity for this connection
    auto it = connectionToEntity_.find(input.connectionId);
    if (it == connectionToEntity_.end()) {
        return;  // Unknown connection
    }
    
    EntityID entity = it->second;
    validateAndApplyInput(entity, input);
}

void ZoneServer::validateAndApplyInput(EntityID entity, const ClientInputPacket& input) {
    uint32_t currentTimeMs = getCurrentTimeMs();
    
    // [SECURITY_AGENT] Rate limiting check - prevent packet flooding
    auto rateResult = antiCheat_.checkRateLimit(entity, currentTimeMs, registry_);
    if (rateResult.detected) {
        std::cerr << "[ZONE " << config_.zoneId << "] Rate limit exceeded for entity " 
                  << static_cast<uint32_t>(entity) << std::endl;
        
        auto connIt = entityToConnection_.find(entity);
        if (connIt != entityToConnection_.end()) {
            network_->disconnect(connIt->second, "Rate limit exceeded");
        }
        return;
    }
    
    // [SECURITY_AGENT] Input validation - check for invalid input values
    auto inputResult = antiCheat_.validateInput(entity, input.input, currentTimeMs, registry_);
    if (inputResult.detected) {
        std::cerr << "[ZONE " << config_.zoneId << "] Input validation failed: " 
                  << inputResult.description << std::endl;
        
        // Kick for critical input manipulation
        if (inputResult.severity == Security::ViolationSeverity::CRITICAL) {
            auto connIt = entityToConnection_.find(entity);
            if (connIt != entityToConnection_.end()) {
                network_->disconnect(connIt->second, inputResult.description);
            }
            return;
        }
        // Otherwise continue with sanitized input (logged as warning)
    }
    
    // Store old position for anti-cheat comparison
    Position oldPos{0, 0, 0, 0};
    if (Position* pos = registry_.try_get<Position>(entity)) {
        oldPos = *pos;
    }
    
    // Apply input with physics validation
    auto result = movementSystem_.applyInput(registry_, entity, input.input, currentTimeMs);
    
    // [SECURITY_AGENT] Comprehensive anti-cheat validation
    bool positionCorrected = false;
    
    if (!result.valid) {
        // Basic movement validation failed (movement system level)
        std::cout << "[ZONE " << config_.zoneId << "] Movement validation failed: " 
                  << result.violationReason << std::endl;
        
        // Apply basic correction
        if (Position* pos = registry_.try_get<Position>(entity)) {
            *pos = result.correctedPosition;
        }
        if (Velocity* vel = registry_.try_get<Velocity>(entity)) {
            *vel = result.correctedVelocity;
        }
        positionCorrected = true;
    }
    
    // [SECURITY_AGENT] Advanced anti-cheat validation
    if (Position* newPos = registry_.try_get<Position>(entity)) {
        // Calculate delta time from input timestamp
        uint32_t dtMs = 16;  // Default to one tick
        if (NetworkState* netState = registry_.try_get<NetworkState>(entity)) {
            if (netState->lastInputTime > 0) {
                dtMs = currentTimeMs - netState->lastInputTime;
                // Clamp to reasonable range to prevent exploits
                dtMs = std::min(dtMs, 1000u);  // Max 1 second
                dtMs = std::max(dtMs, 1u);     // Min 1ms
            }
        }
        
        // Run comprehensive anti-cheat checks
        auto cheatResult = antiCheat_.validateMovement(entity, oldPos, *newPos, 
                                                        dtMs, input.input, registry_);
        
        if (cheatResult.detected) {
            std::cerr << "[ANTICHEAT] " << cheatResult.description 
                      << " [confidence: " << static_cast<int>(cheatResult.confidence * 100) << "%]"
                      << " for entity " << static_cast<uint32_t>(entity) << std::endl;
            
            // Apply position correction from anti-cheat
            *newPos = cheatResult.correctedPosition;
            positionCorrected = true;
            
            // Send position correction to client (server authority)
            auto connIt = entityToConnection_.find(entity);
            if (connIt != entityToConnection_.end()) {
                Velocity vel{};
                if (Velocity* v = registry_.try_get<Velocity>(entity)) {
                    vel = *v;
                }
                Security::ServerAuthority::sendPositionCorrection(
                    connIt->second, *newPos, vel, input.input.sequence);
                
                // Handle kick/ban based on severity
                if (cheatResult.severity == Security::ViolationSeverity::CRITICAL ||
                    cheatResult.severity == Security::ViolationSeverity::BAN) {
                    network_->disconnect(connIt->second, cheatResult.description);
                    
                    // Log the enforcement action
                    std::cerr << "[ANTICHEAT] Player " << connIt->second 
                              << (cheatResult.severity == Security::ViolationSeverity::BAN 
                                  ? " banned" : " kicked")
                              << " for " << cheatResult.description << std::endl;
                    return;
                }
            }
        }
        
        // Update anti-cheat state tracking
        if (AntiCheatState* cheatState = registry_.try_get<AntiCheatState>(entity)) {
            cheatState->lastValidPosition = *newPos;
            cheatState->lastValidationTime = currentTimeMs;
            
            // Track max recorded speed for behavior analysis
            Velocity vel{};
            if (Velocity* v = registry_.try_get<Velocity>(entity)) {
                vel = *v;
                float speed = vel.speed();
                if (speed > cheatState->maxRecordedSpeed) {
                    cheatState->maxRecordedSpeed = speed;
                }
            }
        }
    }
    
    // Update network state
    if (NetworkState* netState = registry_.try_get<NetworkState>(entity)) {
        netState->lastInputSequence = input.input.sequence;
        netState->lastInputTime = input.receiveTimeMs;
    }
    
    // [PHASE 3C] Process attack input with lag compensation and combat validation
    if (input.input.attack) {
        processAttackInput(entity, input);
    }
}

// [SECURITY_AGENT] Initialize anti-cheat system and callbacks
void ZoneServer::initializeAntiCheat() {
    if (!antiCheat_.initialize()) {
        std::cerr << "[ZONE " << config_.zoneId << "] Failed to initialize anti-cheat system!" << std::endl;
        return;
    }
    
    // Set up cheat detection callback
    antiCheat_.setOnCheatDetected([this](uint64_t playerId, 
                                          const Security::CheatDetectionResult& result) {
        onCheatDetected(playerId, result);
    });
    
    // Set up ban callback
    antiCheat_.setOnPlayerBanned([this](uint64_t playerId, const char* reason, 
                                         uint32_t durationMinutes) {
        onPlayerBanned(playerId, reason, durationMinutes);
    });
    
    // Set up kick callback
    antiCheat_.setOnPlayerKicked([this](uint64_t playerId, const char* reason) {
        onPlayerKicked(playerId, reason);
    });
    
    std::cout << "[ZONE " << config_.zoneId << "] Anti-cheat system initialized" << std::endl;
}

// [SECURITY_AGENT] Handle cheat detection event
void ZoneServer::onCheatDetected(uint64_t playerId, const Security::CheatDetectionResult& result) {
    // Log to ScyllaDB for analytics and review
    if (scylla_ && scylla_->isConnected()) {
        // TODO: Implement anti-cheat event logging to database
        // This would log: timestamp, playerId, cheatType, confidence, description
    }
    
    // Could also notify monitoring systems, Discord webhooks, etc.
    if (result.severity == Security::ViolationSeverity::SUSPICIOUS) {
        // Flag for admin review but don't take action yet
        std::cout << "[ANTICHEAT] Player " << playerId << " flagged for review: " 
                  << result.description << std::endl;
    }
}

// [SECURITY_AGENT] Handle player ban event
void ZoneServer::onPlayerBanned(uint64_t playerId, const char* reason, uint32_t durationMinutes) {
    // Persist ban to database
    if (redis_->isConnected()) {
        // TODO: Implement ban persistence in Redis
        // key: ban:{playerId}, value: {reason, expiry, timestamp}
    }
    
    // Kick any connected sessions for this player
    for (const auto& [connId, entityId] : connectionToEntity_) {
        if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(entityId)) {
            if (info->playerId == playerId) {
                network_->disconnect(connId, reason);
                break;
            }
        }
    }
    
    // Log ban event
    std::cout << "[ANTICHEAT] Player " << playerId << " banned for " 
              << durationMinutes << " minutes: " << reason << std::endl;
}

// [SECURITY_AGENT] Handle player kick event
void ZoneServer::onPlayerKicked(uint64_t playerId, const char* reason) {
    // Find and kick the player
    for (const auto& [connId, entityId] : connectionToEntity_) {
        if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(entityId)) {
            if (info->playerId == playerId) {
                network_->disconnect(connId, reason);
                break;
            }
        }
    }
    
    // Log kick event
    std::cout << "[ANTICHEAT] Player " << playerId << " kicked: " << reason << std::endl;
}

void ZoneServer::processAttackInput(EntityID entity, const ClientInputPacket& input) {
    // Build attack input from client data
    AttackInput attackInput;
    attackInput.type = AttackInput::MELEE;  // Default to melee, could be determined by input
    attackInput.sequence = input.input.sequence;
    attackInput.timestamp = input.input.timestamp_ms;
    attackInput.aimDirection = glm::vec3(
        std::sin(input.input.yaw), 
        0.0f,  // Ignore pitch for horizontal aim
        std::cos(input.input.yaw)
    );
    
    // Get RTT for lag compensation
    uint32_t rttMs = 100;  // Default fallback
    if (const NetworkState* netState = registry_.try_get<NetworkState>(entity)) {
        rttMs = netState->rttMs;
        if (rttMs == 0) {
            rttMs = 100;  // Assume 100ms if not measured yet
        }
    }
    
    // Create lag-compensated attack
    // clientTimestamp = serverReceiveTime - oneWayLatency
    uint32_t oneWayLatency = rttMs / 2;
    uint32_t clientTimestamp = (input.receiveTimeMs > oneWayLatency) 
        ? input.receiveTimeMs - oneWayLatency 
        : 0;
    
    LagCompensatedAttack lagAttack;
    lagAttack.attacker = entity;
    lagAttack.input = attackInput;
    lagAttack.clientTimestamp = clientTimestamp;
    lagAttack.serverTimestamp = getCurrentTimeMs();
    lagAttack.rttMs = rttMs;
    
    // Process attack with lag compensation
    // This rewinds all potential targets to their positions at attack time
    LagCompensatedCombat lagCombat(combatSystem_, lagCompensator_);
    auto hits = lagCombat.processAttackWithRewind(registry_, lagAttack);
    
    // Send hit results to relevant clients
    for (const auto& hit : hits) {
        if (hit.hit) {
            // [SECURITY_AGENT] Validate combat action for anti-cheat
            Position attackerPos{0, 0, 0, 0};
            if (const Position* pos = registry_.try_get<Position>(entity)) {
                attackerPos = *pos;
            }
            
            auto combatValidation = antiCheat_.validateCombat(entity, hit.target, 
                                                               hit.damageDealt, hit.hitLocation, 
                                                               attackerPos, registry_);
            
            if (combatValidation.detected) {
                std::cerr << "[ANTICHEAT] Combat validation failed: " 
                          << combatValidation.description << std::endl;
                
                // Skip this hit - don't apply damage
                if (combatValidation.severity == Security::ViolationSeverity::CRITICAL ||
                    combatValidation.severity == Security::ViolationSeverity::BAN) {
                    // Kick/ban the player
                    auto connIt = entityToConnection_.find(entity);
                    if (connIt != entityToConnection_.end()) {
                        network_->disconnect(connIt->second, combatValidation.description);
                    }
                    return;
                }
                continue;  // Skip applying this hit
            }
            
            // [NETWORK_AGENT] Send damage event to target
            auto targetConnIt = entityToConnection_.find(hit.target);
            if (targetConnIt != entityToConnection_.end()) {
                // TODO: Send damage packet to target
                // network_->sendDamageEvent(targetConnIt->second, hit);
            }
            
            // [NETWORK_AGENT] Send hit confirmation to attacker
            auto attackerConnIt = entityToConnection_.find(entity);
            if (attackerConnIt != entityToConnection_.end()) {
                // TODO: Send hit confirmation packet
                // network_->sendHitConfirmation(attackerConnIt->second, hit);
            }
            
            // [DATABASE_AGENT] Log combat event for analytics
            // redis_->logCombatEvent(entity, hit.target, hit.damageDealt, lagAttack.serverTimestamp);
        }
    }
}

EntityID ZoneServer::spawnPlayer(ConnectionID connectionId, uint64_t playerId,
                                const std::string& username, const Position& spawnPos) {
    EntityID entity = registry_.create();
    
    // Add components
    registry_.emplace<Position>(entity, spawnPos);
    registry_.emplace<Velocity>(entity);
    registry_.emplace<Rotation>(entity);
    registry_.emplace<BoundingVolume>(entity);
    registry_.emplace<InputState>(entity);
    registry_.emplace<CombatState>(entity);
    registry_.emplace<NetworkState>(entity);
    registry_.emplace<AntiCheatState>(entity);
    
    PlayerInfo& info = registry_.emplace<PlayerInfo>(entity);
    info.playerId = playerId;
    info.connectionId = connectionId;
    std::strncpy(info.username, username.c_str(), sizeof(info.username) - 1);
    info.username[sizeof(info.username) - 1] = '\0';
    info.sessionStart = getCurrentTimeMs();
    
    registry_.emplace<PlayerTag>(entity);
    
    // Update mappings
    connectionToEntity_[connectionId] = entity;
    entityToConnection_[entity] = connectionId;
    
    return entity;
}

void ZoneServer::despawnEntity(EntityID entity) {
    // Track destroyed entity for removal notifications
    destroyedEntities_.push_back(entity);
    
    // Remove from connection mapping
    auto it = entityToConnection_.find(entity);
    if (it != entityToConnection_.end()) {
        connectionToEntity_.erase(it->second);
        entityToConnection_.erase(it);
    }
    
    // [ZONE_AGENT] Clean up replication tracking for destroyed entity
    replicationOptimizer_.removeEntityTracking(entity);
    
    // [COMBAT_AGENT] Remove from lag compensator history
    lagCompensator_.removeEntity(entity);
    
    // Destroy entity
    registry_.destroy(entity);
}

void ZoneServer::checkPerformanceBudgets(uint64_t tickTimeUs) {
    if (tickTimeUs > Constants::TICK_BUDGET_US) {
        metrics_.overruns++;
        
        if (metrics_.overruns % 60 == 1) {  // Log once per second
            std::cout << "[ZONE " << config_.zoneId << "] WARNING: Tick overrun: " 
                      << (tickTimeUs / 1000.0f) << "ms" << std::endl;
        }
        
        // Activate QoS degradation if severe
        if (tickTimeUs > 20000 && !qosDegraded_) {  // > 20ms
            activateQoSDegradation();
        }
    }
}

void ZoneServer::activateQoSDegradation() {
    std::cout << "[ZONE " << config_.zoneId << "] Activating QoS degradation" << std::endl;
    
    qosDegraded_ = true;
    reducedUpdateRate_ = 10;  // Reduce to 10Hz
    
    // Update replication optimizer config to reduce bandwidth
    ReplicationOptimizer::Config config;
    config.nearRate = 10;
    config.midRate = 5;
    config.farRate = 2;
    config.maxEntitiesPerSnapshot = 50;
    replicationOptimizer_.setConfig(config);
}

void ZoneServer::syncAuraState() {
    uint32_t currentTime = getCurrentTimeMs();
    
    if (currentTime - lastAuraSyncTime_ < AURA_SYNC_INTERVAL_MS) {
        return;
    }
    lastAuraSyncTime_ = currentTime;
    
    // Get entities in aura to sync to adjacent zones
    std::vector<AuraEntityState> entitiesToSync;
    auraManager_.getEntitiesToSync(entitiesToSync);
    
    // Publish to Redis for adjacent zones
    if (redis_->isConnected() && !entitiesToSync.empty()) {
        for (const auto& entity : entitiesToSync) {
            // Serialize entity state
            // TODO: Implement proper serialization for aura sync
            // redis_->publish("zone:" + std::to_string(config_.zoneId) + ":aura", serializedData);
            
            // For now, log the sync
            static uint32_t syncCount = 0;
            if (++syncCount % 20 == 1) {  // Log once per second
                std::cout << "[AURA] Syncing " << entitiesToSync.size() 
                          << " entities to adjacent zones" << std::endl;
            }
        }
    }
}

void ZoneServer::handleAuraEntityMigration() {
    // Check for entities that need ownership transfer
    auto view = registry_.view<Position>(entt::exclude<StaticTag>);
    
    for (auto entity : view) {
        const Position& pos = view.get<Position>(entity);
        
        // Skip if not in aura
        if (!auraManager_.isEntityInAura(entity)) {
            continue;
        }
        
        // Check if we should take ownership
        if (auraManager_.getEntityOwnerZone(entity) != config_.zoneId) {
            if (auraManager_.shouldTakeOwnership(entity, pos)) {
                uint32_t fromZoneId = auraManager_.getEntityOwnerZone(entity);
                auraManager_.onOwnershipTransferred(entity, fromZoneId);
                
                // TODO: Notify the previous zone of ownership transfer
                // TODO: Update entity's zone assignment in Redis
            }
        }
    }
}

void ZoneServer::checkEntityZoneTransitions() {
    uint32_t currentTime = getCurrentTimeMs();
    
    auto view = registry_.view<Position>(entt::exclude<StaticTag>);
    
    for (auto entity : view) {
        const Position& pos = view.get<Position>(entity);
        
        // Check if entity is entering aura from core
        if (auraManager_.isInAuraBuffer(pos)) {
            if (!auraManager_.isEntityInAura(entity)) {
                // Entity entering aura from our core
                auraManager_.onEntityEnteringAura(entity, pos, config_.zoneId);
            } else {
                // Update entity state in aura
                Velocity vel{};
                if (auto* v = registry_.try_get<Velocity>(entity)) {
                    vel = *v;
                }
                auraManager_.updateEntityState(entity, pos, vel, currentTick_);
            }
            
            // [PHASE 4C] Check if player should migrate to another zone
            // This would typically be triggered by the AuraProjectionManager
            // detecting an entity crossing into another zone's territory
            
        } else if (auraManager_.isInCoreZone(pos)) {
            // Entity is in our core - if it was in aura, it left
            if (auraManager_.isEntityInAura(entity)) {
                auraManager_.onEntityLeavingAura(entity, config_.zoneId);
            }
        }
    }
    
    // Handle ownership transfers within aura
    handleAuraEntityMigration();
}

void ZoneServer::onEntityMigrationComplete(EntityID entity, bool success) {
    if (success) {
        std::cout << "[ZONE " << config_.zoneId 
                  << "] Entity migration completed successfully for entity "
                  << static_cast<uint32_t>(entity) << std::endl;
        
        // Update connection mapping
        auto it = entityToConnection_.find(entity);
        if (it != entityToConnection_.end()) {
            ConnectionID connId = it->second;
            connectionToEntity_.erase(connId);
            entityToConnection_.erase(it);
            
            // Notify client of zone change if it's a player
            // In production, this would redirect the connection to the new zone
        }
        
        // Clean up replication tracking
        replicationOptimizer_.removeEntityTracking(entity);
        
        // Remove from lag compensator
        lagCompensator_.removeEntity(entity);
    } else {
        std::cerr << "[ZONE " << config_.zoneId 
                  << "] Entity migration failed for entity "
                  << static_cast<uint32_t>(entity) << std::endl;
        // Player stays in current zone, migration was rolled back
    }
}

// [PHASE 4E] Initialize zone handoff controller
void ZoneServer::initializeHandoffController() {
    handoffController_ = std::make_unique<ZoneHandoffController>(
        config_.zoneId,
        migrationManager_.get(),
        &auraManager_,
        network_.get()
    );
    
    // Set up zone definition for distance calculations
    ZoneDefinition zoneDef;
    zoneDef.zoneId = config_.zoneId;
    zoneDef.minX = config_.minX;
    zoneDef.maxX = config_.maxX;
    zoneDef.minZ = config_.minZ;
    zoneDef.maxZ = config_.maxZ;
    zoneDef.centerX = (config_.minX + config_.maxX) / 2.0f;
    zoneDef.centerZ = (config_.minZ + config_.maxZ) / 2.0f;
    handoffController_->setMyZoneDefinition(zoneDef);
    
    // Set up zone lookup callbacks
    handoffController_->setZoneLookupCallbacks(
        [this](uint32_t zoneId) -> ZoneDefinition* { return lookupZone(zoneId); },
        [this](float x, float z) -> uint32_t { return findZoneByPosition(x, z); }
    );
    
    // Set up handoff callbacks
    handoffController_->setOnHandoffStarted(
        [this](uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success) {
            onHandoffStarted(playerId, sourceZone, targetZone, success);
        }
    );
    handoffController_->setOnHandoffCompleted(
        [this](uint64_t playerId, uint32_t sourceZone, uint32_t targetZone, bool success) {
            onHandoffCompleted(playerId, sourceZone, targetZone, success);
        }
    );
    
    // Initialize with default config
    if (!handoffController_->initialize()) {
        std::cerr << "[ZONE " << config_.zoneId << "] Failed to initialize handoff controller!" << std::endl;
    } else {
        std::cout << "[ZONE " << config_.zoneId << "] Zone handoff controller initialized" << std::endl;
    }
}

// [PHASE 4E] Update zone handoffs - check all players for potential handoffs
void ZoneServer::updateZoneHandoffs() {
    if (!handoffController_) return;
    
    uint32_t currentTimeMs = getCurrentTimeMs();
    
    // Check all players for potential handoffs
    auto view = registry_.view<PlayerInfo, Position, Velocity>();
    
    view.each([this, currentTimeMs](EntityID entity, const PlayerInfo& info, 
                                   const Position& pos, const Velocity& vel) {
        auto connIt = entityToConnection_.find(entity);
        if (connIt == entityToConnection_.end()) return;
        
        handoffController_->checkPlayerPosition(info.playerId, entity, connIt->second,
                                               pos, vel, currentTimeMs);
    });
    
    // Update handoff controller state machine
    handoffController_->update(registry_, currentTimeMs);
}

// [PHASE 4E] Handoff started callback
void ZoneServer::onHandoffStarted(uint64_t playerId, uint32_t sourceZone, 
                                   uint32_t targetZone, bool success) {
    std::cout << "[ZONE " << config_.zoneId << "] Handoff started for player " 
              << playerId << " from zone " << sourceZone << " to zone " << targetZone << std::endl;
    
    // Could log to ScyllaDB for analytics
    // Could notify other systems (e.g., party system, guild system)
}

// [PHASE 4E] Handoff completed callback
void ZoneServer::onHandoffCompleted(uint64_t playerId, uint32_t sourceZone, 
                                     uint32_t targetZone, bool success) {
    if (success) {
        std::cout << "[ZONE " << config_.zoneId << "] Handoff completed for player " 
                  << playerId << " to zone " << targetZone << std::endl;
    } else {
        std::cerr << "[ZONE " << config_.zoneId << "] Handoff failed for player " 
                  << playerId << " to zone " << targetZone << std::endl;
    }
    
    // Update metrics, logging, etc.
}

// [PHASE 4E] Zone lookup callback - returns zone definition by ID
ZoneDefinition* ZoneServer::lookupZone(uint32_t zoneId) {
    // In production, this would query a zone registry or service discovery
    // For now, construct a basic definition based on known zone layout
    
    // Simple grid layout: zones are adjacent rectangles
    static ZoneDefinition tempDef;
    tempDef.zoneId = zoneId;
    
    // Calculate approximate position based on zone ID
    // Assuming 2x2 grid for simple case
    uint32_t zoneX = (zoneId - 1) % 2;
    uint32_t zoneZ = (zoneId - 1) / 2;
    
    float zoneWidth = (Constants::WORLD_MAX_X - Constants::WORLD_MIN_X) / 2.0f;
    float zoneDepth = (Constants::WORLD_MAX_Z - Constants::WORLD_MIN_Z) / 2.0f;
    
    tempDef.minX = Constants::WORLD_MIN_X + zoneX * zoneWidth;
    tempDef.maxX = tempDef.minX + zoneWidth;
    tempDef.minZ = Constants::WORLD_MIN_Z + zoneZ * zoneDepth;
    tempDef.maxZ = tempDef.minZ + zoneDepth;
    tempDef.centerX = (tempDef.minX + tempDef.maxX) / 2.0f;
    tempDef.centerZ = (tempDef.minZ + tempDef.maxZ) / 2.0f;
    tempDef.port = Constants::DEFAULT_SERVER_PORT + static_cast<uint16_t>(zoneId) - 1;
    tempDef.host = "127.0.0.1";  // Localhost for testing
    
    return &tempDef;
}

// [PHASE 4E] Find zone by position callback - returns zone ID containing position
uint32_t ZoneServer::findZoneByPosition(float x, float z) {
    // Simple grid-based zone lookup
    // In production, this would use a spatial index or query a world partition
    
    float zoneWidth = (Constants::WORLD_MAX_X - Constants::WORLD_MIN_X) / 2.0f;
    float zoneDepth = (Constants::WORLD_MAX_Z - Constants::WORLD_MIN_Z) / 2.0f;
    
    uint32_t zoneX = static_cast<uint32_t>((x - Constants::WORLD_MIN_X) / zoneWidth);
    uint32_t zoneZ = static_cast<uint32_t>((z - Constants::WORLD_MIN_Z) / zoneDepth);
    
    // Clamp to valid range
    zoneX = std::min(zoneX, 1u);
    zoneZ = std::min(zoneZ, 1u);
    
    return zoneZ * 2 + zoneX + 1;  // 1-indexed zone IDs
}

} // namespace DarkAges
