#pragma once
#include "ecs/CoreTypes.hpp"
#include <string>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <memory>
#include <cstddef>
#include <cstdint>

namespace DarkAges {

// [DATABASE_AGENT] Combat event data for logging
struct CombatEvent {
    uint64_t eventId;           // Unique event ID
    uint32_t timestamp;         // Server timestamp
    uint32_t zoneId;            // Zone where event occurred
    uint64_t attackerId;        // Persistent player ID (0 for NPC/environment)
    uint64_t targetId;          // Persistent player ID
    std::string eventType;      // "damage", "heal", "kill", "death", "assist"
    int16_t damageAmount;       // Damage/heal amount (fixed-point)
    bool isCritical;            // Was it a critical hit?
    std::string weaponType;     // "melee", "sword", "bow", etc.
    Position position;          // Where the event occurred
    uint32_t serverTick;        // Server tick number
};

// [DATABASE_AGENT] Player combat statistics
struct PlayerCombatStats {
    uint64_t playerId;
    uint32_t sessionDate;       // YYYYMMDD format
    uint32_t kills;
    uint32_t deaths;
    uint64_t damageDealt;
    uint64_t damageTaken;
    uint32_t longestKillStreak;
    uint32_t currentKillStreak;
};

// [DATABASE_AGENT] ScyllaDB connection manager
class ScyllaManager {
public:
    using ConnectCallback = std::function<void(bool success, const std::string& error)>;
    using WriteCallback = std::function<void(bool success)>;

public:
    ScyllaManager();
    ~ScyllaManager();
    
    // Initialize connection (non-blocking)
    bool initialize(const std::string& host = "localhost", 
                   uint16_t port = Constants::SCYLLA_DEFAULT_PORT);
    
    // Shutdown
    void shutdown();
    
    // Check if connected
    [[nodiscard]] bool isConnected() const;
    
    // Process async operations - call every tick
    void update();
    
    // === Combat Logging ===
    
    // Log a combat event (async)
    void logCombatEvent(const CombatEvent& event, WriteCallback callback = nullptr);
    
    // Batch log multiple events (more efficient)
    void logCombatEventsBatch(const std::vector<CombatEvent>& events, 
                             WriteCallback callback = nullptr);
    
    // Update player combat stats (async)
    void updatePlayerStats(const PlayerCombatStats& stats, WriteCallback callback = nullptr);
    
    // Get player stats (async)
    void getPlayerStats(uint64_t playerId, uint32_t sessionDate,
                       std::function<void(bool success, const PlayerCombatStats& stats)> callback);
    
    // === Analytics Queries ===
    
    // Get top killers for a zone/time period
    void getTopKillers(uint32_t zoneId, uint32_t startTime, uint32_t endTime, int limit,
                      std::function<void(bool success, const std::vector<std::pair<uint64_t, uint32_t>>&)> callback);
    
    // Get kill feed for a zone
    void getKillFeed(uint32_t zoneId, int limit,
                    std::function<void(bool success, const std::vector<CombatEvent>&)> callback);

    // === Metrics ===
    
    [[nodiscard]] uint64_t getWritesQueued() const { return writesQueued_; }
    [[nodiscard]] uint64_t getWritesCompleted() const { return writesCompleted_; }
    [[nodiscard]] uint64_t getWritesFailed() const { return writesFailed_; }

private:
    struct ScyllaInternal;
    std::unique_ptr<ScyllaInternal> internal_;
    
    bool connected_{false};
    
    // Write queue for batching
    struct PendingWrite {
        std::string query;
        WriteCallback callback;
        uint32_t timestamp;
    };
    std::queue<PendingWrite> writeQueue_;
    mutable std::mutex queueMutex_;
    
    // Batch write settings
    static constexpr size_t BATCH_SIZE = 100;
    static constexpr uint32_t BATCH_INTERVAL_MS = 5000;  // 5 seconds
    uint32_t lastBatchTime_{0};
    
    // Metrics
    uint64_t writesQueued_{0};
    uint64_t writesCompleted_{0};
    uint64_t writesFailed_{0};
    
    void processBatch();
    void executeQuery(const std::string& query, WriteCallback callback);
};

} // namespace DarkAges
