#pragma once
#include "db/RedisManager.hpp"
#include "zones/EntityMigration.hpp"
#include <functional>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace DarkAges {

// [ZONE_AGENT] Handles all cross-zone communication
// Coordinates entity sync, migration, and broadcasts between zone servers
class CrossZoneMessenger {
public:
    using EntitySyncCallback = std::function<void(uint32_t sourceZone, EntityID entity, 
                                                  const Position& pos, const Velocity& vel)>;
    using MigrationCallback = std::function<void(const EntitySnapshot& snapshot)>;
    using MigrationStateCallback = std::function<void(EntityID entity, MigrationState state, 
                                                      const std::vector<uint8_t>& data)>;
    using BroadcastCallback = std::function<void(uint32_t sourceZone, const std::vector<uint8_t>& data)>;

public:
    CrossZoneMessenger(uint32_t myZoneId, RedisManager* redis);
    ~CrossZoneMessenger();
    
    // Initialize subscriptions
    bool initialize();
    
    // Shutdown
    void shutdown();
    
    // Send entity state to adjacent zone (aura sync)
    void sendEntitySync(uint32_t targetZoneId, EntityID entity, 
                        const Position& pos, const Velocity& vel);
    
    // Send migration request
    void sendMigrationRequest(uint32_t targetZoneId, const EntitySnapshot& snapshot);
    
    // Send migration state update
    void sendMigrationState(uint32_t targetZoneId, EntityID entity, 
                            MigrationState state, const std::vector<uint8_t>& data);
    
    // Send migration complete notification
    void sendMigrationComplete(uint32_t targetZoneId, EntityID entity, uint64_t playerId);
    
    // Broadcast message to all zones
    void broadcast(const std::vector<uint8_t>& data);
    
    // Process incoming messages (call every tick)
    void update();
    
    // Set callbacks
    void setOnEntitySync(EntitySyncCallback callback) { onEntitySync_ = callback; }
    void setOnMigrationRequest(MigrationCallback callback) { onMigrationRequest_ = callback; }
    void setOnMigrationState(MigrationStateCallback callback) { onMigrationState_ = callback; }
    void setOnBroadcast(BroadcastCallback callback) { onBroadcast_ = callback; }
    
    // Metrics
    [[nodiscard]] uint64_t getMessagesSent() const { return messagesSent_; }
    [[nodiscard]] uint64_t getMessagesReceived() const { return messagesReceived_; }
    [[nodiscard]] uint32_t getCurrentSequence() const { return messageSequence_; }

private:
    uint32_t myZoneId_;
    RedisManager* redis_;
    std::atomic<uint32_t> messageSequence_{0};
    std::atomic<uint64_t> messagesSent_{0};
    std::atomic<uint64_t> messagesReceived_{0};
    bool initialized_{false};
    
    // Callbacks
    EntitySyncCallback onEntitySync_;
    MigrationCallback onMigrationRequest_;
    MigrationStateCallback onMigrationState_;
    BroadcastCallback onBroadcast_;
    
    // Handle incoming messages
    void onMessageReceived(const ZoneMessage& message);
    
    // Channel name helpers
    [[nodiscard]] std::string getZoneChannel(uint32_t zoneId) const {
        return "zone:" + std::to_string(zoneId) + ":messages";
    }
    
    [[nodiscard]] std::string getBroadcastChannel() const {
        return "zone:broadcast";
    }
};

} // namespace DarkAges
