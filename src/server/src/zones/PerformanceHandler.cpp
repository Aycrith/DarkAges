// Performance monitoring implementation
// Extracted from ZoneServer.cpp to improve code organization

#include "zones/PerformanceHandler.hpp"
#include "zones/ZoneServer.hpp"
#include "zones/ReplicationOptimizer.hpp"
#include "netcode/NetworkManager.hpp"
#include <iostream>
#include <chrono>

namespace DarkAges {

PerformanceHandler::PerformanceHandler(ZoneServer& server)
    : server_(server) {
}

void PerformanceHandler::checkPerformanceBudgets(uint64_t tickTimeUs) {
    if (tickTimeUs > Constants::TICK_BUDGET_US) {
        auto& metrics = server_.getMetricsRef();
        metrics.overruns++;

        if (metrics.overruns % 60 == 1) {  // Log once per second
            const auto& config = server_.getConfig();
            std::cout << "[ZONE " << config.zoneId << "] WARNING: Tick overrun: "
                      << (tickTimeUs / 1000.0f) << "ms" << std::endl;
        }

        // Activate QoS degradation if severe
        if (tickTimeUs >= 20000 && !server_.isQoSDegraded()) {  // >= 20ms
            activateQoSDegradation();
        }
    }
}

// [DEVOPS_AGENT] Update network-related Prometheus metrics
void PerformanceHandler::updateNetworkMetrics(Monitoring::MetricsExporter& metrics, const std::string& zoneIdStr) {
    auto* network = network_;
    if (!network) return;

    auto* connectionToEntity = server_.getConnectionToEntityPtr();
    if (!connectionToEntity) return;

    std::unordered_map<std::string, std::string> zoneLabel = {{"zone_id", zoneIdStr}};

    // Aggregate packet stats from all connections
    float totalPacketLoss = 0.0f;
    uint32_t connectionCount = 0;
    uint64_t totalBytesSent = 0;
    uint64_t totalBytesReceived = 0;
    uint32_t totalPacketsSent = 0;
    uint32_t totalPacketsReceived = 0;

    // Iterate through all connections to aggregate stats
    for (const auto& [connId, entity] : *connectionToEntity) {
        auto quality = network->getConnectionQuality(connId);

        totalPacketLoss += quality.packetLoss;
        totalBytesSent += quality.bytesSent;
        totalBytesReceived += quality.bytesReceived;
        totalPacketsSent += quality.packetsSent;
        totalPacketsReceived += quality.packetsReceived;
        connectionCount++;
    }

    // Calculate average packet loss percentage
    float avgPacketLoss = connectionCount > 0 ? (totalPacketLoss / connectionCount) * 100.0f : 0.0f;
    metrics.PacketLossPercent().Set(static_cast<double>(avgPacketLoss), zoneLabel);

    // Update packet counters (cumulative)
    static uint64_t lastPacketsSent = 0;
    static uint64_t lastPacketsLost = 0;
    static uint64_t lastBytesSent = 0;
    static auto lastUpdateTime = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(now - lastUpdateTime).count();

    if (elapsedSec >= 1.0) {  // Update bandwidth calculation once per second
        // Calculate bandwidth (bytes per second)
        uint64_t bytesSentDelta = totalBytesSent > lastBytesSent ? totalBytesSent - lastBytesSent : 0;
        double bandwidthBps = bytesSentDelta / elapsedSec;
        metrics.ReplicationBandwidthBps().Set(bandwidthBps, zoneLabel);

        lastBytesSent = totalBytesSent;
        lastUpdateTime = now;
    }

    // Update counter metrics
    metrics.PacketsSentTotal().Increment(static_cast<double>(totalPacketsSent), zoneLabel);

    // Estimate packets lost from packet loss percentage
    uint64_t packetsLost = static_cast<uint64_t>(totalPacketsSent * (avgPacketLoss / 100.0f));
    if (packetsLost > lastPacketsLost) {
        metrics.PacketsLostTotal().Increment(static_cast<double>(packetsLost - lastPacketsLost), zoneLabel);
        lastPacketsLost = packetsLost;
    }
}

void PerformanceHandler::activateQoSDegradation() {
    const auto& config = server_.getConfig();

    std::cout << "[ZONE " << config.zoneId << "] Activating QoS degradation" << std::endl;

    server_.setQoSDegraded(true);
    server_.setReducedUpdateRate(10);  // Reduce to 10Hz

    // Update replication optimizer config to reduce bandwidth
    if (replicationOptimizer_) {
        ReplicationOptimizer::Config config;
        config.nearRate = 10;
        config.midRate = 5;
        config.farRate = 2;
        config.maxEntitiesPerSnapshot = 50;
        replicationOptimizer_->setConfig(config);
    }
}

} // namespace DarkAges
