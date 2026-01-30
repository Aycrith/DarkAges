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
void RedisManager::subscribeToZoneChannel(uint32_t, std::function<void(const ZoneMessage&)>) {}
void RedisManager::unsubscribe(std::string_view) {}
void RedisManager::processSubscriptions() {}
void RedisManager::publishToZone(uint32_t, const ZoneMessage&) {}
void RedisManager::broadcastToAllZones(const ZoneMessage&) {}
void RedisManager::set(std::string_view, std::string_view, unsigned int, std::function<void(bool)>) {}

} // namespace DarkAges
