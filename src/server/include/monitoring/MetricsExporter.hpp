#pragma once

// [DEVOPS_AGENT] Prometheus metrics exporter for DarkAges MMO
// Exposes server metrics in Prometheus text format at /metrics endpoint

#include <string>
#include <sstream>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace DarkAges {
namespace Monitoring {

// Metric types supported by Prometheus
enum class MetricType {
    Counter,    // Monotonically increasing
    Gauge,      // Can go up or down
    Histogram,  // Distribution of values
    Summary     // Similar to histogram but calculates quantiles
};

// Individual metric value with labels
struct MetricValue {
    std::unordered_map<std::string, std::string> labels;
    double value;
    std::chrono::steady_clock::time_point timestamp;
};

// Counter metric - only increases
class Counter {
public:
    explicit Counter(std::string name, std::string help);
    
    void Increment(const std::unordered_map<std::string, std::string>& labels = {});
    void Increment(double value, const std::unordered_map<std::string, std::string>& labels = {});
    
    double GetValue(const std::unordered_map<std::string, std::string>& labels = {}) const;
    std::string Serialize() const;
    
private:
    std::string name_;
    std::string help_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, double> values_; // Key is serialized labels
    
    std::string LabelsToKey(const std::unordered_map<std::string, std::string>& labels) const;
};

// Gauge metric - can go up or down
class Gauge {
public:
    explicit Gauge(std::string name, std::string help);
    
    void Set(double value, const std::unordered_map<std::string, std::string>& labels = {});
    void Increment(double value = 1.0, const std::unordered_map<std::string, std::string>& labels = {});
    void Decrement(double value = 1.0, const std::unordered_map<std::string, std::string>& labels = {});
    
    double GetValue(const std::unordered_map<std::string, std::string>& labels = {}) const;
    std::string Serialize() const;
    
private:
    std::string name_;
    std::string help_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, double> values_;
    
    std::string LabelsToKey(const std::unordered_map<std::string, std::string>& labels) const;
};

// Histogram metric - tracks value distribution
class Histogram {
public:
    Histogram(std::string name, std::string help, std::vector<double> buckets);
    
    void Observe(double value, const std::unordered_map<std::string, std::string>& labels = {});
    
    std::string Serialize() const;
    
private:
    std::string name_;
    std::string help_;
    std::vector<double> buckets_;
    mutable std::mutex mutex_;
    
    struct BucketData {
        std::vector<std::atomic<uint64_t>> counts;
        std::atomic<double> sum{0.0};
        std::atomic<uint64_t> total_count{0};
    };
    std::unordered_map<std::string, BucketData> data_;
    
    std::string LabelsToKey(const std::unordered_map<std::string, std::string>& labels) const;
};

// Main metrics registry and HTTP exporter
class MetricsExporter {
public:
    static MetricsExporter& Instance();
    
    // Initialize the metrics HTTP server
    bool Initialize(uint16_t port = 8080);
    void Shutdown();
    
    // Metric registration
    Counter* CreateCounter(const std::string& name, const std::string& help);
    Gauge* CreateGauge(const std::string& name, const std::string& help);
    Histogram* CreateHistogram(const std::string& name, const std::string& help, 
                               const std::vector<double>& buckets);
    
    // Get existing metrics
    Counter* GetCounter(const std::string& name);
    Gauge* GetGauge(const std::string& name);
    Histogram* GetHistogram(const std::string& name);
    
    // Generate Prometheus format output
    std::string SerializeAll() const;
    
    // Pre-defined server metrics accessors
    Counter& TicksTotal() { return *ticksTotal_; }
    Counter& ErrorsTotal() { return *errorsTotal_; }
    Counter& PacketsSentTotal() { return *packetsSentTotal_; }
    Counter& PacketsLostTotal() { return *packetsLostTotal_; }
    Counter& ReplicationBytesTotal() { return *replicationBytesTotal_; }
    
    Gauge& TickDurationMs() { return *tickDurationMs_; }
    Gauge& PlayerCount() { return *playerCount_; }
    Gauge& MemoryUsedBytes() { return *memoryUsedBytes_; }
    Gauge& MemoryTotalBytes() { return *memoryTotalBytes_; }
    Gauge& DbConnected() { return *dbConnected_; }
    
    Histogram& TickDurationHistogram() { return *tickDurationHistogram_; }
    
private:
    MetricsExporter() = default;
    ~MetricsExporter() = default;
    MetricsExporter(const MetricsExporter&) = delete;
    MetricsExporter& operator=(const MetricsExporter&) = delete;
    
    void InitDefaultMetrics();
    void HttpServerLoop();
    void HandleRequest(int clientSocket);
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Counter>> counters_;
    std::unordered_map<std::string, std::unique_ptr<Gauge>> gauges_;
    std::unordered_map<std::string, std::unique_ptr<Histogram>> histograms_;
    
    // HTTP server state
    int serverSocket_ = -1;
    uint16_t port_ = 8080;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    
    // Pre-defined metrics
    Counter* ticksTotal_ = nullptr;
    Counter* errorsTotal_ = nullptr;
    Counter* packetsSentTotal_ = nullptr;
    Counter* packetsLostTotal_ = nullptr;
    Counter* replicationBytesTotal_ = nullptr;
    
    Gauge* tickDurationMs_ = nullptr;
    Gauge* playerCount_ = nullptr;
    Gauge* memoryUsedBytes_ = nullptr;
    Gauge* memoryTotalBytes_ = nullptr;
    Gauge* dbConnected_ = nullptr;
    
    Histogram* tickDurationHistogram_ = nullptr;
};

// RAII helper for timing scoped operations
class ScopedTimer {
public:
    explicit ScopedTimer(Histogram& histogram);
    ~ScopedTimer();
    
    double ElapsedMs() const;
    
private:
    Histogram& histogram_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace Monitoring
} // namespace DarkAges
