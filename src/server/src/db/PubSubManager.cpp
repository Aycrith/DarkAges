// Pub/Sub management implementation for cross-zone communication
// Handles publish/subscribe, zone messaging, and the pub/sub listener thread

#include "db/PubSubManager.hpp"
#include "db/RedisManager.hpp"
#include "db/RedisInternal.hpp"
#include "Constants.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef REDIS_AVAILABLE
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <hiredis.h>
#endif

namespace DarkAges {

// ============================================================================
// Pub/Sub Listener Thread
// ============================================================================

#ifdef REDIS_AVAILABLE
static void PubSubListener(RedisInternal* internal) {
    while (internal->pubSubRunning) {
        redisReply* reply = nullptr;
        
        if (internal->pubSubCtx && redisGetReply(internal->pubSubCtx, (void**)&reply) == REDIS_OK) {
            if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
                // Subscription message format: [message, channel, data]
                std::string_view msgType(reply->element[0]->str, reply->element[0]->len);
                if (msgType == "message" || msgType == "pmessage") {
                    size_t channelIdx = (msgType == "pmessage") ? 2 : 1;
                    size_t messageIdx = (msgType == "pmessage") ? 3 : 2;
                    
                    if (reply->elements > messageIdx) {
                        std::string channel(reply->element[channelIdx]->str, reply->element[channelIdx]->len);
                        std::string message(reply->element[messageIdx]->str, reply->element[messageIdx]->len);
                        
                        {
                            std::lock_guard<std::mutex> lock(internal->messageMutex);
                            internal->messageQueue.emplace(std::move(channel), std::move(message));
                        }
                    }
                }
            }
            freeReplyObject(reply);
        } else {
            // Error or shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
#endif

// ============================================================================
// ZoneMessage Serialization
// ============================================================================

std::vector<uint8_t> ZoneMessage::serialize() const {
    std::vector<uint8_t> data;
    
    // Header: type(1) + source(4) + target(4) + timestamp(4) + sequence(4) + payload_size(4)
    size_t headerSize = 1 + 4 + 4 + 4 + 4 + 4;
    data.resize(headerSize + payload.size());
    
    size_t offset = 0;
    data[offset++] = static_cast<uint8_t>(type);
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = sourceZoneId;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = targetZoneId;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = timestamp;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = sequence;
    offset += 4;
    
    *reinterpret_cast<uint32_t*>(&data[offset]) = static_cast<uint32_t>(payload.size());
    offset += 4;
    
    std::memcpy(&data[offset], payload.data(), payload.size());
    
    return data;
}

std::optional<ZoneMessage> ZoneMessage::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 21) {  // Minimum header size
        return std::nullopt;
    }
    
    ZoneMessage msg;
    size_t offset = 0;
    
    msg.type = static_cast<ZoneMessageType>(data[offset++]);
    
    msg.sourceZoneId = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.targetZoneId = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.timestamp = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    msg.sequence = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    uint32_t payloadSize = *reinterpret_cast<const uint32_t*>(&data[offset]);
    offset += 4;
    
    if (data.size() < offset + payloadSize) {
        return std::nullopt;
    }
    
    msg.payload.assign(data.begin() + offset, data.begin() + offset + payloadSize);
    
    return msg;
}

// ============================================================================
// PubSubManager Implementation
// ============================================================================

PubSubManager::PubSubManager(RedisManager& redis, RedisInternal& internal)
    : redis_(redis), internal_(internal) {}

PubSubManager::~PubSubManager() {
    // Stop pub/sub thread if running
    #ifdef REDIS_AVAILABLE
    internal_.pubSubRunning = false;
    if (internal_.pubSubThread.joinable()) {
        internal_.pubSubThread.join();
    }
    if (internal_.pubSubCtx) {
        redisFree(internal_.pubSubCtx);
        internal_.pubSubCtx = nullptr;
    }
    #endif
}

void PubSubManager::publish(std::string_view channel, std::string_view message) {
    if (!internal_.connected) return;
    
    internal_.commandsSent_++;
    
    #ifdef REDIS_AVAILABLE
    auto* ctx = internal_.pool->acquire();
    if (!ctx) {
        internal_.commandsFailed_++;
        return;
    }
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "PUBLISH %b %b",
                                                  channel.data(), channel.size(),
                                                  message.data(), message.size());
    
    if (reply) {
        freeReplyObject(reply);
        internal_.commandsCompleted_++;
    } else {
        internal_.commandsFailed_++;
    }
    
    internal_.pool->release(ctx);
    #else
    (void)channel;
    (void)message;
    internal_.commandsCompleted_++;
    #endif
}

void PubSubManager::subscribe(std::string_view channel,
                               std::function<void(std::string_view, std::string_view)> callback) {
    if (!internal_.connected) return;
    
    {
        std::lock_guard<std::mutex> lock(internal_.subMutex);
        RedisInternal::Subscription sub;
        sub.channel = std::string(channel);
        sub.callback = std::move(callback);
        internal_.subscriptions.push_back(std::move(sub));
    }
    
    #ifdef REDIS_AVAILABLE
    // Start pub/sub listener if not running
    if (!internal_.pubSubRunning) {
        internal_.pubSubRunning = true;
        
        // Create dedicated pub/sub connection
        struct timeval timeout;
        timeout.tv_sec = Constants::REDIS_CONNECTION_TIMEOUT_MS / 1000;
        timeout.tv_usec = (Constants::REDIS_CONNECTION_TIMEOUT_MS % 1000) * 1000;
        
        internal_.pubSubCtx = redisConnectWithTimeout(internal_.host.c_str(), internal_.port, timeout);
        if (internal_.pubSubCtx && !internal_.pubSubCtx->err) {
            internal_.pubSubThread = std::thread(PubSubListener, &internal_);
        } else {
            std::cerr << "[REDIS] Failed to create pub/sub connection" << std::endl;
            internal_.pubSubRunning = false;
            if (internal_.pubSubCtx) {
                redisFree(internal_.pubSubCtx);
                internal_.pubSubCtx = nullptr;
            }
        }
    }
    
    // Subscribe to channel
    if (internal_.pubSubCtx && !internal_.pubSubCtx->err) {
        redisReply* reply = (redisReply*)redisCommand(internal_.pubSubCtx, "SUBSCRIBE %b",
                                                      channel.data(), channel.size());
        if (reply) {
            freeReplyObject(reply);
        }
    }
    #else
    (void)channel;
    #endif
}

void PubSubManager::unsubscribe(std::string_view channel) {
    if (!internal_.connected) return;
    
    {
        std::lock_guard<std::mutex> lock(internal_.subMutex);
        auto& subs = internal_.subscriptions;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
            [channel](const auto& sub) { return sub.channel == channel; }), subs.end());
    }
    
    #ifdef REDIS_AVAILABLE
    if (internal_.pubSubCtx && !internal_.pubSubCtx->err) {
        redisReply* reply = (redisReply*)redisCommand(internal_.pubSubCtx, "UNSUBSCRIBE %b",
                                                      channel.data(), channel.size());
        if (reply) {
            freeReplyObject(reply);
        }
    }
    #endif
    
    std::cout << "[REDIS] Unsubscribed from " << channel << std::endl;
}

void PubSubManager::processSubscriptions() {
    if (!internal_.connected) return;
    
    // Process queued messages
    std::queue<std::pair<std::string, std::string>> messages;
    {
        std::lock_guard<std::mutex> lock(internal_.messageMutex);
        messages.swap(internal_.messageQueue);
    }
    
    while (!messages.empty()) {
        const auto& [channel, message] = messages.front();
        
        std::lock_guard<std::mutex> lock(internal_.subMutex);
        for (const auto& sub : internal_.subscriptions) {
            // Simple channel matching (exact match for now, could use pattern matching)
            if (sub.channel == channel) {
                sub.callback(channel, message);
            }
        }
        
        messages.pop();
    }
}

void PubSubManager::publishToZone(uint32_t zoneId, const ZoneMessage& message) {
    std::string channel = "zone:" + std::to_string(zoneId) + ":messages";
    auto data = message.serialize();
    publish(channel, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

void PubSubManager::broadcastToAllZones(const ZoneMessage& message) {
    std::string channel = "zone:broadcast";
    auto data = message.serialize();
    publish(channel, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

void PubSubManager::subscribeToZoneChannel(uint32_t myZoneId,
                                            std::function<void(const ZoneMessage&)> callback) {
    std::string channel = "zone:" + std::to_string(myZoneId) + ":messages";
    
    subscribe(channel, [callback](std::string_view ch, std::string_view msg) {
        (void)ch;
        std::vector<uint8_t> data(msg.begin(), msg.end());
        auto message = ZoneMessage::deserialize(data);
        if (message) {
            callback(*message);
        }
    });
    
    // Also subscribe to broadcast channel
    subscribe("zone:broadcast", [callback](std::string_view ch, std::string_view msg) {
        (void)ch;
        std::vector<uint8_t> data(msg.begin(), msg.end());
        auto message = ZoneMessage::deserialize(data);
        if (message) {
            callback(*message);
        }
    });
}

} // namespace DarkAges
