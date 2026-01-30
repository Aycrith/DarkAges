#include "zones/CrossZoneMessenger.hpp"
#include "Constants.hpp"
#include <iostream>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace DarkAges {

CrossZoneMessenger::CrossZoneMessenger(uint32_t myZoneId, RedisManager* redis)
    : myZoneId_(myZoneId), redis_(redis) {}

CrossZoneMessenger::~CrossZoneMessenger() {
    shutdown();
}

bool CrossZoneMessenger::initialize() {
    if (!redis_ || !redis_->isConnected()) {
        std::cerr << "[CROSSZONE] Redis not connected" << std::endl;
        return false;
    }
    
    // Subscribe to our zone channel using the high-level zone subscription
    redis_->subscribeToZoneChannel(myZoneId_, [this](const ZoneMessage& msg) {
        messagesReceived_++;
        onMessageReceived(msg);
    });
    
    initialized_ = true;
    std::cout << "[CROSSZONE] Initialized for zone " << myZoneId_ << std::endl;
    return true;
}

void CrossZoneMessenger::shutdown() {
    if (!initialized_ || !redis_) {
        return;
    }
    
    // Unsubscribe from zone-specific channels
    redis_->unsubscribe(getZoneChannel(myZoneId_));
    redis_->unsubscribe(getBroadcastChannel());
    
    initialized_ = false;
    std::cout << "[CROSSZONE] Shutdown complete for zone " << myZoneId_ << std::endl;
}

void CrossZoneMessenger::sendEntitySync(uint32_t targetZoneId, EntityID entity,
                                        const Position& pos, const Velocity& vel) {
    if (!redis_ || !initialized_) return;
    
    ZoneMessage msg;
    msg.type = ZoneMessageType::ENTITY_SYNC;
    msg.sourceZoneId = myZoneId_;
    msg.targetZoneId = targetZoneId;
    msg.timestamp = 0;  // Will be set if needed
    msg.sequence = ++messageSequence_;
    
    // Serialize entity state
    // Format: entity(4) + pos_x(4) + pos_y(4) + pos_z(4) + pos_timestamp(4) + 
    //         vel_x(4) + vel_y(4) + vel_z(4) = 32 bytes
    msg.payload.resize(sizeof(EntityID) + sizeof(Position) + sizeof(Velocity));
    size_t offset = 0;
    
    *reinterpret_cast<EntityID*>(&msg.payload[offset]) = entity;
    offset += sizeof(EntityID);
    
    std::memcpy(&msg.payload[offset], &pos, sizeof(Position));
    offset += sizeof(Position);
    
    std::memcpy(&msg.payload[offset], &vel, sizeof(Velocity));
    
    redis_->publishToZone(targetZoneId, msg);
    messagesSent_++;
}

void CrossZoneMessenger::sendMigrationRequest(uint32_t targetZoneId, 
                                              const EntitySnapshot& snapshot) {
    if (!redis_ || !initialized_) return;
    
    ZoneMessage msg;
    msg.type = ZoneMessageType::MIGRATION_REQUEST;
    msg.sourceZoneId = myZoneId_;
    msg.targetZoneId = targetZoneId;
    msg.sequence = ++messageSequence_;
    msg.payload = snapshot.serialize();
    
    redis_->publishToZone(targetZoneId, msg);
    messagesSent_++;
    
    std::cout << "[CROSSZONE] Sent migration request to zone " << targetZoneId 
              << " for entity " << static_cast<uint32_t>(snapshot.entity) << std::endl;
}

void CrossZoneMessenger::sendMigrationState(uint32_t targetZoneId, EntityID entity,
                                            MigrationState state, 
                                            const std::vector<uint8_t>& data) {
    if (!redis_ || !initialized_) return;
    
    ZoneMessage msg;
    msg.type = ZoneMessageType::MIGRATION_STATE;
    msg.sourceZoneId = myZoneId_;
    msg.targetZoneId = targetZoneId;
    msg.sequence = ++messageSequence_;
    
    // Payload: entity(4) + state(1) + data
    msg.payload.resize(sizeof(EntityID) + 1 + data.size());
    
    *reinterpret_cast<EntityID*>(&msg.payload[0]) = entity;
    msg.payload[sizeof(EntityID)] = static_cast<uint8_t>(state);
    
    if (!data.empty()) {
        std::memcpy(&msg.payload[sizeof(EntityID) + 1], data.data(), data.size());
    }
    
    redis_->publishToZone(targetZoneId, msg);
    messagesSent_++;
}

void CrossZoneMessenger::sendMigrationComplete(uint32_t targetZoneId, EntityID entity, 
                                               uint64_t playerId) {
    if (!redis_ || !initialized_) return;
    
    ZoneMessage msg;
    msg.type = ZoneMessageType::MIGRATION_COMPLETE;
    msg.sourceZoneId = myZoneId_;
    msg.targetZoneId = targetZoneId;
    msg.sequence = ++messageSequence_;
    
    // Payload: entity(4) + playerId(8) = 12 bytes
    msg.payload.resize(sizeof(EntityID) + sizeof(uint64_t));
    
    *reinterpret_cast<EntityID*>(&msg.payload[0]) = entity;
    *reinterpret_cast<uint64_t*>(&msg.payload[sizeof(EntityID)]) = playerId;
    
    redis_->publishToZone(targetZoneId, msg);
    messagesSent_++;
}

void CrossZoneMessenger::broadcast(const std::vector<uint8_t>& data) {
    if (!redis_ || !initialized_) return;
    
    ZoneMessage msg;
    msg.type = ZoneMessageType::BROADCAST;
    msg.sourceZoneId = myZoneId_;
    msg.targetZoneId = 0;  // All zones
    msg.sequence = ++messageSequence_;
    msg.payload = data;
    
    redis_->broadcastToAllZones(msg);
    messagesSent_++;
}

void CrossZoneMessenger::update() {
    if (redis_) {
        redis_->processSubscriptions();
    }
}

void CrossZoneMessenger::onMessageReceived(const ZoneMessage& message) {
    // Filter messages not for us (targetZoneId == 0 means broadcast)
    if (message.targetZoneId != 0 && message.targetZoneId != myZoneId_) {
        return;
    }
    
    // Ignore our own messages
    if (message.sourceZoneId == myZoneId_) {
        return;
    }
    
    switch (message.type) {
        case ZoneMessageType::ENTITY_SYNC:
            if (onEntitySync_) {
                // Deserialize entity sync
                if (message.payload.size() >= sizeof(EntityID) + sizeof(Position) + sizeof(Velocity)) {
                    EntityID entity = *reinterpret_cast<const EntityID*>(&message.payload[0]);
                    Position pos;
                    Velocity vel;
                    
                    std::memcpy(&pos, &message.payload[sizeof(EntityID)], sizeof(Position));
                    std::memcpy(&vel, &message.payload[sizeof(EntityID) + sizeof(Position)], 
                               sizeof(Velocity));
                    
                    onEntitySync_(message.sourceZoneId, entity, pos, vel);
                }
            }
            break;
            
        case ZoneMessageType::MIGRATION_REQUEST:
            if (onMigrationRequest_) {
                auto snapshot = EntitySnapshot::deserialize(message.payload);
                if (snapshot) {
                    onMigrationRequest_(*snapshot);
                }
            }
            break;
            
        case ZoneMessageType::MIGRATION_STATE:
            if (onMigrationState_) {
                if (message.payload.size() >= sizeof(EntityID) + 1) {
                    EntityID entity = *reinterpret_cast<const EntityID*>(&message.payload[0]);
                    MigrationState state = static_cast<MigrationState>(message.payload[sizeof(EntityID)]);
                    
                    std::vector<uint8_t> data(message.payload.begin() + sizeof(EntityID) + 1,
                                             message.payload.end());
                    
                    onMigrationState_(entity, state, data);
                }
            }
            break;
            
        case ZoneMessageType::MIGRATION_COMPLETE:
            // Migration complete notification - could trigger cleanup in source zone
            if (message.payload.size() >= sizeof(EntityID) + sizeof(uint64_t)) {
                EntityID entity = *reinterpret_cast<const EntityID*>(&message.payload[0]);
                uint64_t playerId = *reinterpret_cast<const uint64_t*>(&message.payload[sizeof(EntityID)]);
                
                std::cout << "[CROSSZONE] Migration complete notification: entity " 
                          << static_cast<uint32_t>(entity) << " (player " << playerId << ")"
                          << " from zone " << message.sourceZoneId << std::endl;
            }
            break;
            
        case ZoneMessageType::BROADCAST:
            if (onBroadcast_) {
                onBroadcast_(message.sourceZoneId, message.payload);
            } else {
                std::cout << "[CROSSZONE] Received broadcast from zone " 
                          << message.sourceZoneId << " (" << message.payload.size() << " bytes)" << std::endl;
            }
            break;
            
        case ZoneMessageType::CHAT:
            // Handle cross-zone chat
            std::cout << "[CROSSZONE] Chat message from zone " << message.sourceZoneId << std::endl;
            break;
            
        case ZoneMessageType::ZONE_STATUS:
            // Handle zone status updates
            break;
            
        default:
            std::cerr << "[CROSSZONE] Unknown message type: " 
                      << static_cast<int>(message.type) << std::endl;
            break;
    }
}

} // namespace DarkAges
