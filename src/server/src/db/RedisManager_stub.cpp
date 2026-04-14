// Stub implementation for RedisManager when hiredis is not available
#include "db/RedisManager.hpp"
#include "zones/CrossZoneMessenger.hpp"

namespace DarkAges {

// Stub definition for internal struct
struct RedisInternal {};

RedisManager::RedisManager() : internal_(std::make_unique<RedisInternal>()) {}
RedisManager::~RedisManager() = default;

bool RedisManager::initialize(const std::string&, uint16_t) { return true; }
void RedisManager::update() {}
bool RedisManager::isConnected() const { return true; }
void RedisManager::shutdown() {}
void RedisManager::subscribeToZoneChannel(uint32_t, std::function<void(const ZoneMessage&)>) {}
void RedisManager::unsubscribe(std::string_view) {}
void RedisManager::processSubscriptions() {}
void RedisManager::publishToZone(uint32_t, const ZoneMessage&) {}
void RedisManager::broadcastToAllZones(const ZoneMessage&) {}
void RedisManager::set(std::string_view, std::string_view, unsigned int, std::function<void(bool)>) {}

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

} // namespace DarkAges
