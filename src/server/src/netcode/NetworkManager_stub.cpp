// Stub implementation for NetworkManager when GNS is not available
#include "netcode/NetworkManager.hpp"

namespace DarkAges {

// Stub definition for internal struct
struct GNSInternal {};

NetworkManager::NetworkManager() : internal_(std::make_unique<GNSInternal>()) {}
NetworkManager::~NetworkManager() = default;

bool NetworkManager::initialize(uint16_t) { return true; }
void NetworkManager::update(uint32_t) {}
void NetworkManager::shutdown() {}
void NetworkManager::sendSnapshot(ConnectionID, std::span<const uint8_t>) {}
void NetworkManager::sendEvent(ConnectionID, std::span<const uint8_t>) {}
void NetworkManager::broadcastSnapshot(std::span<const uint8_t>) {}
void NetworkManager::disconnect(ConnectionID, const char*) {}
std::vector<ClientInputPacket> NetworkManager::getPendingInputs() { return {}; }
ConnectionQuality NetworkManager::getConnectionQuality(ConnectionID) const { return {}; }
bool NetworkManager::isConnected(ConnectionID) const { return false; }
size_t NetworkManager::getConnectionCount() const { return 0; }
uint64_t NetworkManager::getTotalBytesSent() const { return 0; }
uint64_t NetworkManager::getTotalBytesReceived() const { return 0; }
bool NetworkManager::isRateLimited(ConnectionID) const { return false; }
bool NetworkManager::shouldAcceptConnection(const std::string&) { return true; }
bool NetworkManager::processPacket(ConnectionID, const std::string&, uint32_t, uint32_t) { return true; }

namespace Protocol {
std::vector<uint8_t> createDeltaSnapshot(uint32_t, uint32_t, 
    std::span<const EntityStateData>, 
    std::span<const EntityID>, 
    std::span<const EntityStateData>) { return {}; }

std::vector<uint8_t> serializeInput(const InputState&) { return {}; }
bool deserializeInput(std::span<const uint8_t>, InputState&) { return false; }

bool EntityStateData::equalsPosition(const EntityStateData&) const { return false; }
bool EntityStateData::equalsRotation(const EntityStateData&) const { return false; }
bool EntityStateData::equalsVelocity(const EntityStateData&) const { return false; }
} // namespace Protocol

} // namespace DarkAges
