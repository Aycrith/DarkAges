#pragma once

// Internal Redis state structure shared between RedisManager and sub-managers
// This file should ONLY be included by db/*.cpp files, never by external code.

#include "db/ConnectionPool.hpp"
#include <string>
#include <queue>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <unordered_map>

#ifdef REDIS_AVAILABLE
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <hiredis.h>
#endif

namespace DarkAges {

struct RedisInternal {
    std::unique_ptr<ConnectionPool> pool;
    std::string host;
    uint16_t port{6379};
    bool connected{false};
    
    // Async callback handling
    struct PendingCallback {
        std::function<void()> func;
        std::chrono::steady_clock::time_point enqueueTime;
    };
    std::queue<PendingCallback> callbackQueue;
    std::mutex callbackMutex;
    
    // Pub/Sub handling
    struct Subscription {
        std::string channel;
        std::function<void(std::string_view, std::string_view)> callback;
    };
    std::vector<Subscription> subscriptions;
    std::mutex subMutex;
    
    // Pub/Sub listener thread
    #ifdef REDIS_AVAILABLE
    redisContext* pubSubCtx{nullptr};
    #endif
    std::atomic<bool> pubSubRunning{false};
    std::thread pubSubThread;
    std::queue<std::pair<std::string, std::string>> messageQueue;
    std::mutex messageMutex;
    
    // Latency tracking
    std::queue<float> latencySamples;
    std::mutex latencyMutex;
    static constexpr size_t MAX_LATENCY_SAMPLES = 100;
    
    // Metrics
    std::atomic<uint64_t> commandsSent_{0};
    std::atomic<uint64_t> commandsCompleted_{0};
    std::atomic<uint64_t> commandsFailed_{0};
};

} // namespace DarkAges
