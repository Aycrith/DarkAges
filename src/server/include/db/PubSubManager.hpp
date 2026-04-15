#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>

namespace DarkAges {

// Forward declarations
class RedisManager;
struct RedisInternal;

// ============================================================================
// Pub/Sub Message Types for Cross-Zone Communication
// ============================================================================

// Message types for cross-zone communication
enum class ZoneMessageType : uint8_t {
    ENTITY_SYNC = 1,        // Entity state update for aura
    MIGRATION_REQUEST = 2,  // Initiate entity migration
    MIGRATION_STATE = 3,    // Migration state update
    MIGRATION_COMPLETE = 4, // Migration finished
    BROADCAST = 5,          // Zone-wide broadcast
    CHAT = 6,               // Cross-zone chat
    ZONE_STATUS = 7         // Zone status update
};

// Cross-zone message header
struct ZoneMessage {
    ZoneMessageType type;
    uint32_t sourceZoneId;
    uint32_t targetZoneId;  // 0 = broadcast to all
    uint32_t timestamp;
    uint32_t sequence;
    std::vector<uint8_t> payload;
    
    std::vector<uint8_t> serialize() const;
    static std::optional<ZoneMessage> deserialize(const std::vector<uint8_t>& data);
};

// Manages Redis pub/sub operations for cross-zone communication
// Extracted from RedisManager for cohesion - pub/sub, zone messaging, listener thread
class PubSubManager {
public:
    explicit PubSubManager(RedisManager& redis, RedisInternal& internal);
    ~PubSubManager();

    // Publish message to channel
    void publish(std::string_view channel, std::string_view message);
    
    // Subscribe to channel with callback
    void subscribe(std::string_view channel,
                   std::function<void(std::string_view, std::string_view)> callback);
    
    // Unsubscribe from channel
    void unsubscribe(std::string_view channel);
    
    // Process subscribed messages - call every tick
    void processSubscriptions();
    
    // Zone-specific helpers
    void publishToZone(uint32_t zoneId, const ZoneMessage& message);
    void broadcastToAllZones(const ZoneMessage& message);
    void subscribeToZoneChannel(uint32_t myZoneId,
                                std::function<void(const ZoneMessage&)> callback);

private:
    RedisManager& redis_;
    RedisInternal& internal_;
};

} // namespace DarkAges
