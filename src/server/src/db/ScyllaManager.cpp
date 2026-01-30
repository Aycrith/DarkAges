// Full implementation of ScyllaManager using cassandra-cpp-driver
// [DATABASE_AGENT] ScyllaDB persistence layer for combat events and player stats

#include "db/ScyllaManager.hpp"
#include "Constants.hpp"
#include <cassandra.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <thread>
#include <algorithm>

namespace DarkAges {

// ============================================================================
// Internal Implementation Structure
// ============================================================================

struct ScyllaManager::ScyllaInternal {
    CassCluster* cluster{nullptr};
    CassSession* session{nullptr};
    
    // Prepared statements for high-performance operations
    const CassPrepared* insertCombatEvent{nullptr};
    const CassPrepared* updatePlayerStats{nullptr};
    const CassPrepared* insertPlayerSession{nullptr};
    const CassPrepared* queryCombatHistoryByAttacker{nullptr};
    const CassPrepared* queryCombatHistoryByTarget{nullptr};
    const CassPrepared* queryPlayerStats{nullptr};
    const CassPrepared* queryTopKillers{nullptr};
    const CassPrepared* queryKillFeed{nullptr};
    
    // Pending async operations tracking
    std::atomic<uint64_t> pendingOperations{0};
    
    ~ScyllaInternal() {
        cleanupPreparedStatements();
    }
    
    void cleanupPreparedStatements() {
        if (insertCombatEvent) {
            cass_prepared_free(insertCombatEvent);
            insertCombatEvent = nullptr;
        }
        if (updatePlayerStats) {
            cass_prepared_free(updatePlayerStats);
            updatePlayerStats = nullptr;
        }
        if (insertPlayerSession) {
            cass_prepared_free(insertPlayerSession);
            insertPlayerSession = nullptr;
        }
        if (queryCombatHistoryByAttacker) {
            cass_prepared_free(queryCombatHistoryByAttacker);
            queryCombatHistoryByAttacker = nullptr;
        }
        if (queryCombatHistoryByTarget) {
            cass_prepared_free(queryCombatHistoryByTarget);
            queryCombatHistoryByTarget = nullptr;
        }
        if (queryPlayerStats) {
            cass_prepared_free(queryPlayerStats);
            queryPlayerStats = nullptr;
        }
        if (queryTopKillers) {
            cass_prepared_free(queryTopKillers);
            queryTopKillers = nullptr;
        }
        if (queryKillFeed) {
            cass_prepared_free(queryKillFeed);
            queryKillFeed = nullptr;
        }
    }
};

// ============================================================================
// Utility Functions
// ============================================================================

namespace {
    // Convert timestamp to day bucket string (YYYY-MM-DD format)
    std::string getDayBucket(uint32_t timestamp) {
        auto time = static_cast<std::time_t>(timestamp);
        std::tm tm = *std::gmtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }
    
    // Get current timestamp in seconds
    uint32_t getCurrentTimestamp() {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }
    
    // Async callback data structure for write operations
    struct WriteCallbackData {
        ScyllaManager::WriteCallback callback;
        ScyllaManager* manager;
        
        WriteCallbackData(ScyllaManager::WriteCallback cb, ScyllaManager* mgr)
            : callback(std::move(cb)), manager(mgr) {}
    };
    
    // Async callback data for query operations
    template<typename T>
    struct QueryCallbackData {
        T callback;
        ScyllaManager* manager;
        
        QueryCallbackData(T cb, ScyllaManager* mgr)
            : callback(std::move(cb)), manager(mgr) {}
    };
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ScyllaManager::ScyllaManager() 
    : internal_(std::make_unique<ScyllaInternal>()) {}

ScyllaManager::~ScyllaManager() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool ScyllaManager::initialize(const std::string& host, uint16_t port) {
    // Create cluster and session
    internal_->cluster = cass_cluster_new();
    internal_->session = cass_session_new();
    
    if (!internal_->cluster || !internal_->session) {
        return false;
    }
    
    // Configure cluster for high-performance async operations
    cass_cluster_set_contact_points(internal_->cluster, host.c_str());
    cass_cluster_set_port(internal_->cluster, port);
    
    // I/O threads for concurrent operations
    cass_cluster_set_num_threads_io(internal_->cluster, 4);
    
    // Queue sizes for handling burst traffic
    cass_cluster_set_queue_size_io(internal_->cluster, 10000);
    cass_cluster_set_pending_requests_low_water_mark(internal_->cluster, 1000);
    cass_cluster_set_pending_requests_high_water_mark(internal_->cluster, 5000);
    
    // Connection pooling
    cass_cluster_set_core_connections_per_host(internal_->cluster, 2);
    cass_cluster_set_max_connections_per_host(internal_->cluster, 8);
    
    // Reconnection policy
    cass_cluster_set_reconnect_wait_time(internal_->cluster, 1000);
    
    // Consistency level for writes (LOCAL_QUORUM for durability/performance balance)
    cass_cluster_set_consistency(internal_->cluster, CASS_CONSISTENCY_LOCAL_QUORUM);
    
    // Connect to cluster
    CassFuture* connect_future = cass_session_connect(internal_->session, internal_->cluster);
    cass_future_wait(connect_future);
    
    CassError rc = cass_future_error_code(connect_future);
    if (rc != CASS_OK) {
        const char* message;
        size_t message_length;
        cass_future_error_message(connect_future, &message, &message_length);
        (void)message; // Log error if logging available
        cass_future_free(connect_future);
        return false;
    }
    cass_future_free(connect_future);
    
    // Create schema
    if (!createSchema()) {
        return false;
    }
    
    // Prepare statements for high-performance operations
    if (!prepareStatements()) {
        return false;
    }
    
    connected_ = true;
    return true;
}

void ScyllaManager::shutdown() {
    if (!internal_->session) {
        return;
    }
    
    // Wait for pending operations to complete (with timeout)
    int wait_cycles = 100; // Max 10 seconds
    while (internal_->pendingOperations.load() > 0 && wait_cycles-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    internal_->cleanupPreparedStatements();
    
    if (internal_->session) {
        CassFuture* close_future = cass_session_close(internal_->session);
        cass_future_wait(close_future);
        cass_future_free(close_future);
        cass_session_free(internal_->session);
        internal_->session = nullptr;
    }
    
    if (internal_->cluster) {
        cass_cluster_free(internal_->cluster);
        internal_->cluster = nullptr;
    }
    
    connected_ = false;
}

bool ScyllaManager::isConnected() const {
    return connected_ && internal_->session != nullptr;
}

// ============================================================================
// Schema Creation
// ============================================================================

bool ScyllaManager::createSchema() {
    // Create keyspace
    const char* createKeyspace = 
        "CREATE KEYSPACE IF NOT EXISTS darkages "
        "WITH replication = {"
        "  'class': 'SimpleStrategy', "
        "  'replication_factor': 1 "
        "}";
    
    CassStatement* stmt = cass_statement_new(createKeyspace, 0);
    CassFuture* future = cass_session_execute(internal_->session, stmt);
    cass_future_wait(future);
    
    CassError rc = cass_future_error_code(future);
    cass_future_free(future);
    cass_statement_free(stmt);
    
    if (rc != CASS_OK) {
        return false;
    }
    
    // Create combat_events table
    const char* createCombatEvents = 
        "CREATE TABLE IF NOT EXISTS darkages.combat_events ("
        "  day_bucket text, "
        "  timestamp timestamp, "
        "  event_id uuid, "
        "  attacker_id bigint, "
        "  target_id bigint, "
        "  damage int, "
        "  hit_type text, "
        "  event_type text, "
        "  zone_id int, "
        "  position_x double, "
        "  position_y double, "
        "  position_z double, "
        "  PRIMARY KEY ((day_bucket), timestamp, event_id) "
        ") WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC) "
        "AND compaction = {'class': 'TimeWindowCompactionStrategy', "
        "  'compaction_window_unit': 'DAYS', 'compaction_window_size': 1} "
        "AND default_time_to_live = 2592000";  // 30 days TTL
    
    stmt = cass_statement_new(createCombatEvents, 0);
    future = cass_session_execute(internal_->session, stmt);
    cass_future_wait(future);
    rc = cass_future_error_code(future);
    cass_future_free(future);
    cass_statement_free(stmt);
    
    if (rc != CASS_OK) {
        return false;
    }
    
    // Create index on attacker_id for player combat history queries
    const char* createAttackerIndex = 
        "CREATE INDEX IF NOT EXISTS idx_combat_attacker "
        "ON darkages.combat_events(attacker_id)";
    
    stmt = cass_statement_new(createAttackerIndex, 0);
    future = cass_session_execute(internal_->session, stmt);
    cass_future_wait(future);
    cass_future_free(future);
    cass_statement_free(stmt);
    // Index creation failure is non-fatal
    
    // Create index on target_id
    const char* createTargetIndex = 
        "CREATE INDEX IF NOT EXISTS idx_combat_target "
        "ON darkages.combat_events(target_id)";
    
    stmt = cass_statement_new(createTargetIndex, 0);
    future = cass_session_execute(internal_->session, stmt);
    cass_future_wait(future);
    cass_future_free(future);
    cass_statement_free(stmt);
    
    // Create player_stats table (using counters)
    const char* createPlayerStats = 
        "CREATE TABLE IF NOT EXISTS darkages.player_stats ("
        "  player_id bigint PRIMARY KEY, "
        "  total_kills counter, "
        "  total_deaths counter, "
        "  total_assists counter, "
        "  total_damage_dealt counter, "
        "  total_damage_taken counter, "
        "  total_damage_blocked counter, "
        "  total_healing_done counter, "
        "  total_playtime_minutes counter, "
        "  total_matches counter, "
        "  total_wins counter, "
        "  last_updated timestamp "
        ")";
    
    stmt = cass_statement_new(createPlayerStats, 0);
    future = cass_session_execute(internal_->session, stmt);
    cass_future_wait(future);
    rc = cass_future_error_code(future);
    cass_future_free(future);
    cass_statement_free(stmt);
    
    if (rc != CASS_OK) {
        return false;
    }
    
    // Create player_sessions table
    const char* createPlayerSessions = 
        "CREATE TABLE IF NOT EXISTS darkages.player_sessions ("
        "  player_id bigint, "
        "  session_start timestamp, "
        "  session_end timestamp, "
        "  zone_id int, "
        "  kills int, "
        "  deaths int, "
        "  damage_dealt bigint, "
        "  damage_taken bigint, "
        "  playtime_minutes int, "
        "  PRIMARY KEY ((player_id), session_start) "
        ") WITH CLUSTERING ORDER BY (session_start DESC) "
        "AND default_time_to_live = 7776000";  // 90 days
    
    stmt = cass_statement_new(createPlayerSessions, 0);
    future = cass_session_execute(internal_->session, stmt);
    cass_future_wait(future);
    rc = cass_future_error_code(future);
    cass_future_free(future);
    cass_statement_free(stmt);
    
    if (rc != CASS_OK) {
        return false;
    }
    
    // Create leaderboard_daily table
    const char* createLeaderboard = 
        "CREATE TABLE IF NOT EXISTS darkages.leaderboard_daily ("
        "  category text, "
        "  day text, "
        "  player_id bigint, "
        "  player_name text, "
        "  score bigint, "
        "  PRIMARY KEY ((category, day), score, player_id) "
        ") WITH CLUSTERING ORDER BY (score DESC, player_id ASC) "
        "AND default_time_to_live = 7776000";  // 90 days
    
    stmt = cass_statement_new(createLeaderboard, 0);
    future = cass_session_execute(internal_->session, stmt);
    cass_future_wait(future);
    rc = cass_future_error_code(future);
    cass_future_free(future);
    cass_statement_free(stmt);
    
    return rc == CASS_OK;
}

// ============================================================================
// Statement Preparation
// ============================================================================

bool ScyllaManager::prepareStatements() {
    // Prepare INSERT for combat events
    const char* insertCombat = 
        "INSERT INTO darkages.combat_events "
        "(day_bucket, timestamp, event_id, attacker_id, target_id, damage, "
        " hit_type, event_type, zone_id, position_x, position_y, position_z) "
        "VALUES (?, ?, uuid(), ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    CassFuture* future = cass_session_prepare(internal_->session, insertCombat);
    cass_future_wait(future);
    CassError rc = cass_future_error_code(future);
    if (rc != CASS_OK) {
        cass_future_free(future);
        return false;
    }
    internal_->insertCombatEvent = cass_future_get_prepared(future);
    cass_future_free(future);
    
    // Prepare UPDATE for player stats (counters)
    const char* updateStats = 
        "UPDATE darkages.player_stats SET "
        "total_kills = total_kills + ?, "
        "total_deaths = total_deaths + ?, "
        "total_assists = total_assists + ?, "
        "total_damage_dealt = total_damage_dealt + ?, "
        "total_damage_taken = total_damage_taken + ?, "
        "total_damage_blocked = total_damage_blocked + ?, "
        "total_healing_done = total_healing_done + ?, "
        "total_playtime_minutes = total_playtime_minutes + ?, "
        "total_matches = total_matches + ?, "
        "total_wins = total_wins + ?, "
        "last_updated = toTimestamp(now()) "
        "WHERE player_id = ?";
    
    future = cass_session_prepare(internal_->session, updateStats);
    cass_future_wait(future);
    rc = cass_future_error_code(future);
    if (rc != CASS_OK) {
        cass_future_free(future);
        return false;
    }
    internal_->updatePlayerStats = cass_future_get_prepared(future);
    cass_future_free(future);
    
    // Prepare INSERT for player sessions
    const char* insertSession = 
        "INSERT INTO darkages.player_sessions "
        "(player_id, session_start, session_end, zone_id, kills, deaths, "
        " damage_dealt, damage_taken, playtime_minutes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    future = cass_session_prepare(internal_->session, insertSession);
    cass_future_wait(future);
    rc = cass_future_error_code(future);
    if (rc != CASS_OK) {
        cass_future_free(future);
        return false;
    }
    internal_->insertPlayerSession = cass_future_get_prepared(future);
    cass_future_free(future);
    
    // Prepare query for combat history by attacker
    const char* queryByAttacker = 
        "SELECT timestamp, attacker_id, target_id, damage, hit_type, event_type, zone_id "
        "FROM darkages.combat_events "
        "WHERE attacker_id = ? AND day_bucket = ? "
        "ALLOW FILTERING";
    
    future = cass_session_prepare(internal_->session, queryByAttacker);
    cass_future_wait(future);
    if (cass_future_error_code(future) == CASS_OK) {
        internal_->queryCombatHistoryByAttacker = cass_future_get_prepared(future);
    }
    cass_future_free(future);
    
    // Prepare query for player stats
    const char* queryStats = 
        "SELECT total_kills, total_deaths, total_damage_dealt, total_damage_taken, "
        "total_playtime_minutes FROM darkages.player_stats WHERE player_id = ?";
    
    future = cass_session_prepare(internal_->session, queryStats);
    cass_future_wait(future);
    if (cass_future_error_code(future) == CASS_OK) {
        internal_->queryPlayerStats = cass_future_get_prepared(future);
    }
    cass_future_free(future);
    
    // Prepare query for kill feed (events where target died)
    const char* queryKillFeed = 
        "SELECT timestamp, attacker_id, target_id, event_type, zone_id "
        "FROM darkages.combat_events "
        "WHERE zone_id = ? AND day_bucket = ? AND event_type = 'kill' "
        "ALLOW FILTERING";
    
    future = cass_session_prepare(internal_->session, queryKillFeed);
    cass_future_wait(future);
    if (cass_future_error_code(future) == CASS_OK) {
        internal_->queryKillFeed = cass_future_get_prepared(future);
    }
    cass_future_free(future);
    
    return true;
}

// ============================================================================
// Combat Event Logging
// ============================================================================

void ScyllaManager::logCombatEvent(const CombatEvent& event, WriteCallback callback) {
    if (!isConnected() || !internal_->insertCombatEvent) {
        if (callback) {
            callback(false);
        }
        writesFailed_++;
        return;
    }
    
    writesQueued_++;
    internal_->pendingOperations++;
    
    // Bind prepared statement
    CassStatement* statement = cass_prepared_bind(internal_->insertCombatEvent);
    
    // Get day bucket from timestamp
    std::string dayBucket = getDayBucket(event.timestamp);
    
    // Bind parameters
    cass_statement_bind_string(statement, 0, dayBucket.c_str());
    cass_statement_bind_int64(statement, 1, static_cast<cass_int64_t>(event.timestamp) * 1000); // Convert to milliseconds
    cass_statement_bind_int64(statement, 2, static_cast<cass_int64_t>(event.attackerId));
    cass_statement_bind_int64(statement, 3, static_cast<cass_int64_t>(event.targetId));
    cass_statement_bind_int32(statement, 4, event.damageAmount);
    cass_statement_bind_string(statement, 5, event.eventType.c_str());
    cass_statement_bind_string(statement, 6, event.eventType.c_str());
    cass_statement_bind_int32(statement, 7, static_cast<cass_int32_t>(event.zoneId));
    cass_statement_bind_double(statement, 8, event.position.x * Constants::FIXED_TO_FLOAT);
    cass_statement_bind_double(statement, 9, event.position.y * Constants::FIXED_TO_FLOAT);
    cass_statement_bind_double(statement, 10, event.position.z * Constants::FIXED_TO_FLOAT);
    
    // Execute async
    CassFuture* future = cass_session_execute(internal_->session, statement);
    
    // Set up callback
    auto* cbData = new WriteCallbackData(callback, this);
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cbData = static_cast<WriteCallbackData*>(data);
        CassError rc = cass_future_error_code(f);
        bool success = (rc == CASS_OK);
        
        if (cbData->manager) {
            cbData->manager->internal_->pendingOperations--;
            if (success) {
                cbData->manager->writesCompleted_++;
            } else {
                cbData->manager->writesFailed_++;
            }
        }
        
        if (cbData->callback) {
            cbData->callback(success);
        }
        delete cbData;
    }, cbData);
    
    cass_future_free(future);
    cass_statement_free(statement);
}

void ScyllaManager::logCombatEventsBatch(const std::vector<CombatEvent>& events, 
                                         WriteCallback callback) {
    if (!isConnected() || events.empty()) {
        if (callback) {
            callback(isConnected());
        }
        return;
    }
    
    writesQueued_ += events.size();
    internal_->pendingOperations++;
    
    // Create unlogged batch for high-performance writes
    CassBatch* batch = cass_batch_new(CASS_BATCH_TYPE_UNLOGGED);
    
    // Set batch timeout
    cass_batch_set_timeout(batch, 5000); // 5 seconds
    
    for (const auto& event : events) {
        CassStatement* statement = cass_prepared_bind(internal_->insertCombatEvent);
        
        std::string dayBucket = getDayBucket(event.timestamp);
        
        cass_statement_bind_string(statement, 0, dayBucket.c_str());
        cass_statement_bind_int64(statement, 1, static_cast<cass_int64_t>(event.timestamp) * 1000);
        cass_statement_bind_int64(statement, 2, static_cast<cass_int64_t>(event.attackerId));
        cass_statement_bind_int64(statement, 3, static_cast<cass_int64_t>(event.targetId));
        cass_statement_bind_int32(statement, 4, event.damageAmount);
        cass_statement_bind_string(statement, 5, event.eventType.c_str());
        cass_statement_bind_string(statement, 6, event.eventType.c_str());
        cass_statement_bind_int32(statement, 7, static_cast<cass_int32_t>(event.zoneId));
        cass_statement_bind_double(statement, 8, event.position.x * Constants::FIXED_TO_FLOAT);
        cass_statement_bind_double(statement, 9, event.position.y * Constants::FIXED_TO_FLOAT);
        cass_statement_bind_double(statement, 10, event.position.z * Constants::FIXED_TO_FLOAT);
        
        cass_batch_add_statement(batch, statement);
        cass_statement_free(statement);
    }
    
    // Execute batch
    CassFuture* future = cass_session_execute_batch(internal_->session, batch);
    
    auto* cbData = new WriteCallbackData(callback, this);
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cbData = static_cast<WriteCallbackData*>(data);
        CassError rc = cass_future_error_code(f);
        bool success = (rc == CASS_OK);
        
        if (cbData->manager) {
            cbData->manager->internal_->pendingOperations--;
            if (success) {
                cbData->manager->writesCompleted_++;
            } else {
                cbData->manager->writesFailed_++;
            }
        }
        
        if (cbData->callback) {
            cbData->callback(success);
        }
        delete cbData;
    }, cbData);
    
    cass_future_free(future);
    cass_batch_free(batch);
}

// ============================================================================
// Player Stats
// ============================================================================

void ScyllaManager::updatePlayerStats(const PlayerCombatStats& stats, WriteCallback callback) {
    if (!isConnected() || !internal_->updatePlayerStats) {
        if (callback) {
            callback(false);
        }
        writesFailed_++;
        return;
    }
    
    writesQueued_++;
    internal_->pendingOperations++;
    
    CassStatement* statement = cass_prepared_bind(internal_->updatePlayerStats);
    
    // Bind counter increments
    cass_statement_bind_int64(statement, 0, static_cast<cass_int64_t>(stats.kills));
    cass_statement_bind_int64(statement, 1, static_cast<cass_int64_t>(stats.deaths));
    cass_statement_bind_int64(statement, 2, 0); // assists (not in current struct)
    cass_statement_bind_int64(statement, 3, static_cast<cass_int64_t>(stats.damageDealt));
    cass_statement_bind_int64(statement, 4, static_cast<cass_int64_t>(stats.damageTaken));
    cass_statement_bind_int64(statement, 5, 0); // damage_blocked
    cass_statement_bind_int64(statement, 6, 0); // healing_done
    cass_statement_bind_int64(statement, 7, 0); // playtime_minutes
    cass_statement_bind_int64(statement, 8, 0); // matches
    cass_statement_bind_int64(statement, 9, 0); // wins
    cass_statement_bind_int64(statement, 10, static_cast<cass_int64_t>(stats.playerId));
    
    CassFuture* future = cass_session_execute(internal_->session, statement);
    
    auto* cbData = new WriteCallbackData(callback, this);
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cbData = static_cast<WriteCallbackData*>(data);
        CassError rc = cass_future_error_code(f);
        bool success = (rc == CASS_OK);
        
        if (cbData->manager) {
            cbData->manager->internal_->pendingOperations--;
            if (success) {
                cbData->manager->writesCompleted_++;
            } else {
                cbData->manager->writesFailed_++;
            }
        }
        
        if (cbData->callback) {
            cbData->callback(success);
        }
        delete cbData;
    }, cbData);
    
    cass_future_free(future);
    cass_statement_free(statement);
}

void ScyllaManager::getPlayerStats(uint64_t playerId, uint32_t sessionDate,
    std::function<void(bool success, const PlayerCombatStats& stats)> callback) {
    
    if (!isConnected() || !internal_->queryPlayerStats) {
        if (callback) {
            callback(false, PlayerCombatStats{});
        }
        return;
    }
    
    internal_->pendingOperations++;
    
    CassStatement* statement = cass_prepared_bind(internal_->queryPlayerStats);
    cass_statement_bind_int64(statement, 0, static_cast<cass_int64_t>(playerId));
    
    CassFuture* future = cass_session_execute(internal_->session, statement);
    
    using CallbackType = std::function<void(bool, const PlayerCombatStats&)>;
    auto* cbData = new QueryCallbackData<CallbackType>(callback, this);
    
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cbData = static_cast<QueryCallbackData<CallbackType>*>(data);
        CassError rc = cass_future_error_code(f);
        
        if (cbData->manager) {
            cbData->manager->internal_->pendingOperations--;
        }
        
        PlayerCombatStats stats{};
        bool success = false;
        
        if (rc == CASS_OK) {
            const CassResult* result = cass_future_get_result(f);
            CassIterator* iterator = cass_iterator_from_result(result);
            
            if (cass_iterator_next(iterator)) {
                const CassRow* row = cass_iterator_get_row(iterator);
                
                cass_int64_t kills, deaths, damageDealt, damageTaken, playtime;
                
                cass_value_get_int64(cass_row_get_column(row, 0), &kills);
                cass_value_get_int64(cass_row_get_column(row, 1), &deaths);
                cass_value_get_int64(cass_row_get_column(row, 2), &damageDealt);
                cass_value_get_int64(cass_row_get_column(row, 3), &damageTaken);
                cass_value_get_int64(cass_row_get_column(row, 4), &playtime);
                
                stats.playerId = cbData->manager ? 0 : 0; // Will be set from query context
                stats.kills = static_cast<uint32_t>(kills);
                stats.deaths = static_cast<uint32_t>(deaths);
                stats.damageDealt = static_cast<uint64_t>(damageDealt);
                stats.damageTaken = static_cast<uint64_t>(damageTaken);
                success = true;
            }
            
            cass_iterator_free(iterator);
            cass_result_free(result);
        }
        
        if (cbData->callback) {
            cbData->callback(success, stats);
        }
        delete cbData;
    }, cbData);
    
    cass_future_free(future);
    cass_statement_free(statement);
}

// ============================================================================
// Analytics Queries
// ============================================================================

void ScyllaManager::getTopKillers(uint32_t zoneId, uint32_t startTime, uint32_t endTime, 
                                  int limit,
    std::function<void(bool success, const std::vector<std::pair<uint64_t, uint32_t>>&)> callback) {
    
    if (!isConnected()) {
        if (callback) {
            callback(false, {});
        }
        return;
    }
    
    // For now, query all combat events and aggregate (can be optimized with materialized view)
    const char* query = 
        "SELECT attacker_id, count(*) as kill_count "
        "FROM darkages.combat_events "
        "WHERE day_bucket = ? AND event_type = 'kill' "
        "ALLOW FILTERING";
    
    internal_->pendingOperations++;
    
    CassStatement* statement = cass_statement_new(query, 1);
    std::string dayBucket = getDayBucket(startTime);
    cass_statement_bind_string(statement, 0, dayBucket.c_str());
    
    CassFuture* future = cass_session_execute(internal_->session, statement);
    
    using ResultType = std::vector<std::pair<uint64_t, uint32_t>>;
    using CallbackType = std::function<void(bool, const ResultType&)>;
    auto* cbData = new QueryCallbackData<CallbackType>(callback, this);
    
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cbData = static_cast<QueryCallbackData<CallbackType>*>(data);
        CassError rc = cass_future_error_code(f);
        
        if (cbData->manager) {
            cbData->manager->internal_->pendingOperations--;
        }
        
        ResultType results;
        bool success = false;
        
        if (rc == CASS_OK) {
            const CassResult* result = cass_future_get_result(f);
            CassIterator* iterator = cass_iterator_from_result(result);
            
            while (cass_iterator_next(iterator)) {
                const CassRow* row = cass_iterator_get_row(iterator);
                cass_int64_t attackerId;
                cass_int64_t killCount;
                
                cass_value_get_int64(cass_row_get_column(row, 0), &attackerId);
                cass_value_get_int64(cass_row_get_column(row, 1), &killCount);
                
                results.emplace_back(static_cast<uint64_t>(attackerId), 
                                    static_cast<uint32_t>(killCount));
            }
            
            cass_iterator_free(iterator);
            cass_result_free(result);
            success = true;
        }
        
        if (cbData->callback) {
            cbData->callback(success, results);
        }
        delete cbData;
    }, cbData);
    
    cass_future_free(future);
    cass_statement_free(statement);
}

void ScyllaManager::getKillFeed(uint32_t zoneId, int limit,
    std::function<void(bool success, const std::vector<CombatEvent>&)> callback) {
    
    if (!isConnected()) {
        if (callback) {
            callback(false, {});
        }
        return;
    }
    
    internal_->pendingOperations++;
    
    // Query for kill events in the zone today
    const char* query = 
        "SELECT timestamp, attacker_id, target_id, event_type, zone_id "
        "FROM darkages.combat_events "
        "WHERE day_bucket = ? AND zone_id = ? AND event_type = 'kill' "
        "ALLOW FILTERING";
    
    CassStatement* statement = cass_statement_new(query, 2);
    std::string dayBucket = getDayBucket(getCurrentTimestamp());
    cass_statement_bind_string(statement, 0, dayBucket.c_str());
    cass_statement_bind_int32(statement, 1, static_cast<cass_int32_t>(zoneId));
    cass_statement_set_page_size(statement, limit);
    
    CassFuture* future = cass_session_execute(internal_->session, statement);
    
    using CallbackType = std::function<void(bool, const std::vector<CombatEvent>&)>;
    auto* cbData = new QueryCallbackData<CallbackType>(callback, this);
    
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cbData = static_cast<QueryCallbackData<CallbackType>*>(data);
        CassError rc = cass_future_error_code(f);
        
        if (cbData->manager) {
            cbData->manager->internal_->pendingOperations--;
        }
        
        std::vector<CombatEvent> events;
        bool success = false;
        
        if (rc == CASS_OK) {
            const CassResult* result = cass_future_get_result(f);
            CassIterator* iterator = cass_iterator_from_result(result);
            
            while (cass_iterator_next(iterator)) {
                const CassRow* row = cass_iterator_get_row(iterator);
                CombatEvent event{};
                
                cass_int64_t timestamp;
                cass_int64_t attackerId;
                cass_int64_t targetId;
                const char* eventType;
                size_t eventTypeLen;
                cass_int32_t zoneId;
                
                cass_value_get_int64(cass_row_get_column(row, 0), &timestamp);
                cass_value_get_int64(cass_row_get_column(row, 1), &attackerId);
                cass_value_get_int64(cass_row_get_column(row, 2), &targetId);
                cass_value_get_string(cass_row_get_column(row, 3), &eventType, &eventTypeLen);
                cass_value_get_int32(cass_row_get_column(row, 4), &zoneId);
                
                event.timestamp = static_cast<uint32_t>(timestamp / 1000);
                event.attackerId = static_cast<uint64_t>(attackerId);
                event.targetId = static_cast<uint64_t>(targetId);
                event.eventType = std::string(eventType, eventTypeLen);
                event.zoneId = static_cast<uint32_t>(zoneId);
                
                events.push_back(event);
            }
            
            cass_iterator_free(iterator);
            cass_result_free(result);
            success = true;
        }
        
        if (cbData->callback) {
            cbData->callback(success, events);
        }
        delete cbData;
    }, cbData);
    
    cass_future_free(future);
    cass_statement_free(statement);
}

// ============================================================================
// Update (Process Async Operations)
// ============================================================================

void ScyllaManager::update() {
    // Process any pending batch writes if needed
    // The cassandra-cpp-driver handles async operations internally
    // This method can be used for additional periodic tasks
    
    auto now = getCurrentTimestamp();
    
    // Check if we need to flush any queued writes
    std::unique_lock<std::mutex> lock(queueMutex_);
    if (!writeQueue_.empty() && (now - lastBatchTime_ > BATCH_INTERVAL_MS / 1000)) {
        lock.unlock();
        processBatch();
        lastBatchTime_ = now;
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void ScyllaManager::processBatch() {
    // Implementation for background batch processing if needed
    // Currently using async operations with unlogged batches for high throughput
}

void ScyllaManager::executeQuery(const std::string& query, WriteCallback callback) {
    if (!isConnected()) {
        if (callback) {
            callback(false);
        }
        return;
    }
    
    writesQueued_++;
    internal_->pendingOperations++;
    
    CassStatement* statement = cass_statement_new(query.c_str(), 0);
    CassFuture* future = cass_session_execute(internal_->session, statement);
    
    auto* cbData = new WriteCallbackData(callback, this);
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cbData = static_cast<WriteCallbackData*>(data);
        CassError rc = cass_future_error_code(f);
        bool success = (rc == CASS_OK);
        
        if (cbData->manager) {
            cbData->manager->internal_->pendingOperations--;
            if (success) {
                cbData->manager->writesCompleted_++;
            } else {
                cbData->manager->writesFailed_++;
            }
        }
        
        if (cbData->callback) {
            cbData->callback(success);
        }
        delete cbData;
    }, cbData);
    
    cass_future_free(future);
    cass_statement_free(statement);
}

} // namespace DarkAges
