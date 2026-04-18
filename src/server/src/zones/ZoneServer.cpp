// [ZONE_AGENT] Main zone server implementation
// Orchestrates all systems for a single game zone

#include "zones/ZoneServer.hpp"
#include "zones/ReplicationOptimizer.hpp"
#include "zones/EntityMigration.hpp"
#include "combat/LagCompensatedCombat.hpp"
#ifdef DARKAGES_HAS_PROTOBUF
#include "netcode/ProtobufProtocol.hpp"
#endif
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
    : smallPool_(std::make_unique<Memory::SmallPool>()),
      mediumPool_(std::make_unique<Memory::MediumPool>()),
      largePool_(std::make_unique<Memory::LargePool>()),
      tempAllocator_(std::make_unique<Memory::StackAllocator>(1024 * 1024)),
      auraManager_(1),  // Default zone ID, will be updated in initialize
      combatEventHandler_(*this),
      auraZoneHandler_(*this),
      inputHandler_(*this),
      performanceHandler_(*this),
      antiCheatHandler_(*this) {
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
    
    // [ZONE_AGENT] Initialize aura zone handler
    auraManager_ = AuraProjectionManager(config_.zoneId);
    auraZoneHandler_.setRedis(redis_.get());
    auraZoneHandler_.setAuraManager(&auraManager_);
    auraZoneHandler_.setMigrationManager(migrationManager_.get());
    auraZoneHandler_.setConnectionMappings(&connectionToEntity_, &entityToConnection_);
    auraZoneHandler_.initializeAuraManager();
    
    // [ZONE_AGENT] Initialize combat event handler
    combatEventHandler_.setConnectionMappings(&connectionToEntity_, &entityToConnection_);

    // [ZONE_AGENT] Initialize player manager
    playerManager_.setDatabaseConnections(redis_.get(), scylla_.get());
    playerManager_.setZoneId(config_.zoneId);
    std::cout << "[ZONE " << config_.zoneId << "] Player manager initialized" << std::endl;

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
    
    // Set up combat callbacks - delegate to CombatEventHandler
    combatSystem_.setOnDeath([this](EntityID victim, EntityID killer) {
        combatEventHandler_.onEntityDied(victim, killer);
    });
    
    combatSystem_.setOnDamage([this](EntityID attacker, EntityID target, 
                                     int16_t damage, const Position& location) {
        combatEventHandler_.sendCombatEvent(attacker, target, damage, location);
    });
    
    // [ZONE_AGENT] Initialize anti-cheat handler
    antiCheatHandler_.setConnectionMappings(&connectionToEntity_, &entityToConnection_);
    antiCheatHandler_.setNetwork(network_.get());
    antiCheatHandler_.setRedis(redis_.get());
    antiCheatHandler_.setScylla(scylla_.get());
    antiCheatHandler_.setAntiCheat(&antiCheat_);
    antiCheatHandler_.initialize();
    
    // [ZONE_AGENT] Set up extracted handler references
    combatEventHandler_.setNetwork(network_.get());
    combatEventHandler_.setScylla(scylla_.get());
    combatEventHandler_.setCombatSystem(&combatSystem_);
    combatEventHandler_.setLagCompensator(&lagCompensator_);
    combatEventHandler_.setAntiCheat(&antiCheat_);
    
    auraZoneHandler_.setNetwork(network_.get());
    auraZoneHandler_.setHandoffController(handoffController_.get());

    // [ZONE_AGENT] Initialize input handler
    inputHandler_.setPlayerManager(&playerManager_);
    inputHandler_.setAntiCheat(&antiCheat_);
    inputHandler_.setMovementSystem(&movementSystem_);
    inputHandler_.setNetwork(network_.get());
    inputHandler_.setCombatEventHandler(&combatEventHandler_);

    // [ZONE_AGENT] Initialize performance handler
    performanceHandler_.setNetwork(network_.get());
    performanceHandler_.setReplicationOptimizer(&replicationOptimizer_);

    // [ZONE_AGENT] Initialize zone handoff controller via AuraZoneHandler
    auraZoneHandler_.initializeHandoffController();
    
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

    // [ZONE_AGENT] Save all player states via PlayerManager
    playerManager_.saveAllPlayerStates();
    
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
        scylla_->savePlayerState(info->playerId, config_.zoneId, currentTimeMs,
            [playerId = info->playerId](bool success) {
                if (!success) {
                    std::cerr << "[SCYLLA] Failed to persist state for player " << playerId << std::endl;
                }
            });
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
    std::string zoneIdStr = std::to_string(config_.zoneId);
    std::unordered_map<std::string, std::string> zoneLabel = {{"zone_id", zoneIdStr}};
    
    metrics.TicksTotal().Increment(zoneLabel);
    metrics.TickDurationMs().Set(tickTimeMs, zoneLabel);
    metrics.TickDurationHistogram().Observe(tickTimeMs);
    
    // Player population metrics
    double playerCount = static_cast<double>(connectionToEntity_.size());
    metrics.PlayerCount().Set(playerCount, zoneLabel);
    metrics.PlayerCapacity().Set(1000.0, zoneLabel);  // Default capacity
    
    // Memory metrics
    size_t memoryUsed = tempAllocator_->getUsed();
    metrics.MemoryUsedBytes().Set(static_cast<double>(memoryUsed), zoneLabel);
    metrics.MemoryTotalBytes().Set(1024.0 * 1024.0 * 1024.0, zoneLabel);  // 1GB assumption
    
    // Database connection status
    bool dbConnected = redis_ && redis_->isConnected();
    metrics.DbConnected().Set(dbConnected ? 1.0 : 0.0, zoneLabel);
    
    // Network metrics - aggregate from all connections
    performanceHandler_.updateNetworkMetrics(metrics, zoneIdStr);
    
    // Trace counters
    ZONE_TRACE_COUNTER("tick_time_us", static_cast<int64_t>(tickTimeUs));
    ZONE_TRACE_COUNTER("entity_count", 0);
    ZONE_TRACE_COUNTER("player_count", static_cast<int64_t>(connectionToEntity_.size()));
    
    // Check performance budgets
    performanceHandler_.checkPerformanceBudgets(tickTimeUs);
    
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
        inputHandler_.onClientInput(input);
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
    combatEventHandler_.processCombat();
    
    // Health regeneration
    healthRegenSystem_.update(registry_, getCurrentTimeMs());
    
    // Process pending respawns
    combatEventHandler_.processRespawns();
    
    // [PHASE 4] Zone transitions and migration
    auraZoneHandler_.checkEntityZoneTransitions();
    
    // [PHASE 4C] Update entity migrations
    if (migrationManager_) {
        migrationManager_->update(registry_, getCurrentTimeMs());
    }
    
    // [PHASE 4E] Update zone handoffs
    auraZoneHandler_.updateZoneHandoffs();
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    metrics_.gameLogicTimeUs += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}


void ZoneServer::updateReplication() {
    auto start = std::chrono::steady_clock::now();
    
    // [PHASE 4B] Sync aura state with adjacent zones
    ZONE_TRACE_EVENT("Aura::syncState", Profiling::TraceCategory::REPLICATION);
    auraZoneHandler_.syncAuraState();
    
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

    // [ZONE_AGENT] Use PlayerManager to register player
    Position spawnPos = Position::fromVec3(glm::vec3(0.0f, 0.0f, 0.0f), getCurrentTimeMs());
    EntityID entity = playerManager_.registerPlayer(
        connectionId,
        static_cast<uint64_t>(connectionId),
        "Player" + std::to_string(connectionId),
        spawnPos
    );

    std::cout << "[ZONE " << config_.zoneId << "] Spawned entity " << static_cast<uint32_t>(entity)
              << " for connection " << connectionId << std::endl;
}

void ZoneServer::onClientDisconnected(ConnectionID connectionId) {
    std::cout << "[ZONE " << config_.zoneId << "] Client disconnected: " << connectionId << std::endl;

    // [ZONE_AGENT] Get entity via PlayerManager
    EntityID entity = playerManager_.getEntityByConnection(connectionId);

    if (entity != entt::null) {
        // [SECURITY_AGENT] Clean up anti-cheat behavior profile
        if (const PlayerInfo* info = registry_.try_get<PlayerInfo>(entity)) {
            antiCheat_.removeProfile(info->playerId);
        }

        // Save state via PlayerManager
        playerManager_.savePlayerState(entity);

        // [ZONE_AGENT] Full cleanup via despawnEntity (destroyedEntities_, replication, lagCompensator)
        despawnEntity(entity);

        // [ZONE_AGENT] Remove mappings via PlayerManager
        playerManager_.removeConnectionMapping(entity);
    }

    // Clean up client snapshot state
    clientSnapshotState_.erase(connectionId);

    // [ZONE_AGENT] Clean up replication optimizer tracking for disconnected player
    replicationOptimizer_.removeClientTracking(connectionId);
}

// Input handling delegated to InputHandler (see InputHandler.cpp)

// Anti-cheat initialization and event handling delegated to AntiCheatHandler (see AntiCheatHandler.cpp)

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
#ifdef DARKAGES_HAS_PROTOBUF
                auto damageEvent = Netcode::ProtobufProtocol::createDamageEvent(
                    static_cast<uint32_t>(entity),
                    static_cast<uint32_t>(hit.target),
                    static_cast<int32_t>(hit.damageDealt)
                );
                damageEvent.set_timestamp(getCurrentTimeMs());
                auto eventData = Netcode::ProtobufProtocol::serializeEvent(damageEvent);
                network_->sendEvent(targetConnIt->second, eventData);
#endif
                std::cerr << "[NETWORK] Sent damage event: " << hit.damageDealt 
                          << " to entity " << static_cast<uint32_t>(hit.target) << std::endl;
            }
            
            // [NETWORK_AGENT] Send hit confirmation to attacker
            auto attackerConnIt = entityToConnection_.find(entity);
            if (attackerConnIt != entityToConnection_.end()) {
#ifdef DARKAGES_HAS_PROTOBUF
                auto hitConfirm = Netcode::ProtobufProtocol::createDamageEvent(
                    static_cast<uint32_t>(entity),
                    static_cast<uint32_t>(hit.target),
                    static_cast<int32_t>(hit.damageDealt)
                );
                hitConfirm.set_timestamp(getCurrentTimeMs());
                auto eventData = Netcode::ProtobufProtocol::serializeEvent(hitConfirm);
                network_->sendEvent(attackerConnIt->second, eventData);
#endif
                std::cerr << "[NETWORK] Sent hit confirmation: " << hit.damageDealt 
                          << " to attacker entity " << static_cast<uint32_t>(entity) << std::endl;
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
