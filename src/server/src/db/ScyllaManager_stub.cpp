// Stub implementation for ScyllaManager when driver is not available
#include "db/ScyllaManager.hpp"

namespace DarkAges {

// Stub definition for internal struct
struct ScyllaManager::ScyllaInternal {};

ScyllaManager::ScyllaManager() : internal_(std::make_unique<ScyllaInternal>()) {}
ScyllaManager::~ScyllaManager() = default;

bool ScyllaManager::initialize(const std::string&, uint16_t) { return true; }
void ScyllaManager::update() {}
bool ScyllaManager::isConnected() const { return true; }
void ScyllaManager::logCombatEvent(const CombatEvent&, WriteCallback) {}
void ScyllaManager::updatePlayerStats(const PlayerCombatStats&, WriteCallback) {}

} // namespace DarkAges
