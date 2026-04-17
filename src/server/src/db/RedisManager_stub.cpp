// Stub implementation for RedisManager when hiredis is not available
#include "db/RedisManager.hpp"
#include "db/RedisInternal.hpp"
#include <queue>
#include <mutex>
#include <atomic>

namespace DarkAges {

// Minimal RedisInternal for stub
struct RedisInternal {
    bool connected{false};
    std::atomic<uint64_t> commandsSent_{0};
    std::atomic<uint64_t> commandsCompleted_{0};
    std::atomic<uint64_t> commandsFailed_{0};
    std::queue<std::function<void()>> callbackQueue;
    std::mutex callbackMutex;
};

RedisManager::RedisManager() : internal_(std::make_unique<RedisInternal>()) {}
RedisManager::~RedisManager() = default;

bool RedisManager::initialize(const std::string&, uint16_t) { return true; }
void RedisManager::update() {}
bool RedisManager::isConnected() const { return true; }
void RedisManager::shutdown() {}
void RedisManager::set(std::string_view, std::string_view, unsigned int, std::function<void(bool)>) {}
void RedisManager::get(std::string_view, GetCallback) {}
void RedisManager::del(std::string_view, SetCallback) {}

// Stub implementations for session management (no-op when Redis is disabled)
void RedisManager::savePlayerSession(const PlayerSession&, SetCallback callback) {
    if (callback) callback(true);
}
void RedisManager::loadPlayerSession(uint64_t, SessionCallback callback) {
    if (callback) callback(AsyncResult<PlayerSession>{});
}
void RedisManager::removePlayerSession(uint64_t, SetCallback callback) {
    if (callback) callback(true);
}
void RedisManager::updatePlayerPosition(uint64_t, const Position&, uint32_t, SetCallback callback) {
    if (callback) callback(true);
}
void RedisManager::addPlayerToZone(uint32_t, uint64_t, SetCallback callback) {
    if (callback) callback(true);
}
void RedisManager::removePlayerFromZone(uint32_t, uint64_t, SetCallback callback) {
    if (callback) callback(true);
}
void RedisManager::getZonePlayers(uint32_t, std::function<void(const AsyncResult<std::vector<uint64_t>>&)> callback) {
    if (callback) callback(AsyncResult<std::vector<uint64_t>>{});
}

// Stub metrics
uint64_t RedisManager::getCommandsSent() const { return 0; }
uint64_t RedisManager::getCommandsCompleted() const { return 0; }
uint64_t RedisManager::getCommandsFailed() const { return 0; }
float RedisManager::getAverageLatencyMs() const { return 0.0f; }

// Stub streams
void RedisManager::xadd(std::string_view, std::string_view, const std::unordered_map<std::string, std::string>&, StreamAddCallback) {}
void RedisManager::xread(std::string_view, std::string_view, StreamReadCallback, uint32_t, uint32_t) {}

// Stub pipeline
void RedisManager::pipelineSet(const std::vector<std::pair<std::string, std::string>>&, uint32_t, SetCallback) {}

} // namespace DarkAges
