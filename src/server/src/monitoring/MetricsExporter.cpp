// DarkAges MMO - Prometheus Metrics Exporter Implementation
// [DEVOPS_AGENT] HTTP server exposing metrics in Prometheus format

#include "monitoring/MetricsExporter.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <memory>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    using SocketType = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define CLOSE_SOCKET close
    using SocketType = int;
#endif

namespace DarkAges {
namespace Monitoring {

// ============================================================================
// Counter Implementation
// ============================================================================

Counter::Counter(std::string name, std::string help) 
    : name_(std::move(name)), help_(std::move(help)) {}

void Counter::Increment(const std::unordered_map<std::string, std::string>& labels) {
    Increment(1.0, labels);
}

void Counter::Increment(double value, const std::unordered_map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = LabelsToKey(labels);
    values_[key] += value;
}

double Counter::GetValue(const std::unordered_map<std::string, std::string>& labels) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = LabelsToKey(labels);
    auto it = values_.find(key);
    return (it != values_.end()) ? it->second : 0.0;
}

std::string Counter::Serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    
    oss << "# HELP " << name_ << " " << help_ << "\n";
    oss << "# TYPE " << name_ << " counter\n";
    
    for (const auto& [key, value] : values_) {
        oss << name_;
        if (!key.empty()) {
            oss << "{" << key << "}";
        }
        oss << " " << std::fixed << std::setprecision(0) << value << "\n";
    }
    
    return oss.str();
}

std::string Counter::LabelsToKey(const std::unordered_map<std::string, std::string>& labels) const {
    if (labels.empty()) return "";
    
    std::ostringstream oss;
    bool first = true;
    // Sort labels for consistent ordering
    std::vector<std::pair<std::string, std::string>> sortedLabels(labels.begin(), labels.end());
    std::sort(sortedLabels.begin(), sortedLabels.end());
    
    for (const auto& [k, v] : sortedLabels) {
        if (!first) oss << ",";
        first = false;
        oss << k << "=\"" << v << "\"";
    }
    return oss.str();
}

// ============================================================================
// Gauge Implementation
// ============================================================================

Gauge::Gauge(std::string name, std::string help)
    : name_(std::move(name)), help_(std::move(help)) {}

void Gauge::Set(double value, const std::unordered_map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[LabelsToKey(labels)] = value;
}

void Gauge::Increment(double value, const std::unordered_map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[LabelsToKey(labels)] += value;
}

void Gauge::Decrement(double value, const std::unordered_map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[LabelsToKey(labels)] -= value;
}

double Gauge::GetValue(const std::unordered_map<std::string, std::string>& labels) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = values_.find(LabelsToKey(labels));
    return (it != values_.end()) ? it->second : 0.0;
}

std::string Gauge::Serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    
    oss << "# HELP " << name_ << " " << help_ << "\n";
    oss << "# TYPE " << name_ << " gauge\n";
    
    for (const auto& [key, value] : values_) {
        oss << name_;
        if (!key.empty()) {
            oss << "{" << key << "}";
        }
        oss << " " << std::fixed << std::setprecision(2) << value << "\n";
    }
    
    return oss.str();
}

std::string Gauge::LabelsToKey(const std::unordered_map<std::string, std::string>& labels) const {
    if (labels.empty()) return "";
    
    std::ostringstream oss;
    bool first = true;
    std::vector<std::pair<std::string, std::string>> sortedLabels(labels.begin(), labels.end());
    std::sort(sortedLabels.begin(), sortedLabels.end());
    
    for (const auto& [k, v] : sortedLabels) {
        if (!first) oss << ",";
        first = false;
        oss << k << "=\"" << v << "\"";
    }
    return oss.str();
}

// ============================================================================
// Histogram Implementation
// ============================================================================

Histogram::Histogram(std::string name, std::string help, std::vector<double> buckets)
    : name_(std::move(name)), help_(std::move(help)), buckets_(std::move(buckets)) {
    // Ensure buckets are sorted
    std::sort(buckets_.begin(), buckets_.end());
    // Add infinity bucket
    buckets_.push_back(std::numeric_limits<double>::infinity());
}

void Histogram::Observe(double value, const std::unordered_map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = LabelsToKey(labels);
    
    auto& data = data_[key];
    if (data.counts.empty()) {
        // Initialize counts with unique_ptr to atomic
        data.counts.reserve(buckets_.size());
        for (size_t i = 0; i < buckets_.size(); ++i) {
            data.counts.push_back(std::make_unique<std::atomic<uint64_t>>(0));
        }
    }
    
    // Find bucket and increment
    for (size_t i = 0; i < buckets_.size(); ++i) {
        if (value <= buckets_[i]) {
            data.counts[i]->fetch_add(1);
            break;
        }
    }
    
    data.sum.fetch_add(value);
    data.total_count.fetch_add(1);
}

std::string Histogram::Serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    
    oss << "# HELP " << name_ << " " << help_ << "\n";
    oss << "# TYPE " << name_ << " histogram\n";
    
    for (const auto& [key, data] : data_) {
        // Serialize buckets
        for (size_t i = 0; i < buckets_.size(); ++i) {
            oss << name_ << "_bucket{";
            if (!key.empty()) {
                oss << key << ",";
            }
            if (std::isinf(buckets_[i])) {
                oss << "le=\"+Inf\"}";
            } else {
                oss << "le=\"" << std::fixed << std::setprecision(4) << buckets_[i] << "\"}";
            }
            oss << " " << data.counts[i]->load() << "\n";
        }
        
        // Sum and count
        oss << name_ << "_sum";
        if (!key.empty()) {
            oss << "{" << key << "}";
        }
        oss << " " << std::fixed << std::setprecision(2) << data.sum.load() << "\n";
        
        oss << name_ << "_count";
        if (!key.empty()) {
            oss << "{" << key << "}";
        }
        oss << " " << data.total_count.load() << "\n";
    }
    
    return oss.str();
}

std::string Histogram::LabelsToKey(const std::unordered_map<std::string, std::string>& labels) const {
    if (labels.empty()) return "";
    
    std::ostringstream oss;
    bool first = true;
    std::vector<std::pair<std::string, std::string>> sortedLabels(labels.begin(), labels.end());
    std::sort(sortedLabels.begin(), sortedLabels.end());
    
    for (const auto& [k, v] : sortedLabels) {
        if (!first) oss << ",";
        first = false;
        oss << k << "=\"" << v << "\"";
    }
    return oss.str();
}

// ============================================================================
// MetricsExporter Implementation
// ============================================================================

MetricsExporter& MetricsExporter::Instance() {
    static MetricsExporter instance;
    return instance;
}

bool MetricsExporter::Initialize(uint16_t port) {
    port_ = port;
    
    InitDefaultMetrics();
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[METRICS] WSAStartup failed\n";
        return false;
    }
#endif
    
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == -1) {
        std::cerr << "[METRICS] Failed to create socket\n";
        return false;
    }
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(serverSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::cerr << "[METRICS] Failed to bind to port " << port_ << "\n";
        CLOSE_SOCKET(serverSocket_);
        return false;
    }
    
    if (listen(serverSocket_, 5) == -1) {
        std::cerr << "[METRICS] Failed to listen on socket\n";
        CLOSE_SOCKET(serverSocket_);
        return false;
    }
    
    running_ = true;
    serverThread_ = std::thread(&MetricsExporter::HttpServerLoop, this);
    
    std::cout << "[METRICS] Metrics exporter started on port " << port_ << "\n";
    std::cout << "[METRICS] Prometheus endpoint: http://localhost:" << port_ << "/metrics\n";
    
    return true;
}

void MetricsExporter::Shutdown() {
    running_ = false;
    
    if (serverSocket_ != -1) {
        CLOSE_SOCKET(serverSocket_);
        serverSocket_ = -1;
    }
    
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    std::cout << "[METRICS] Metrics exporter stopped\n";
}

void MetricsExporter::InitDefaultMetrics() {
    // Counters
    ticksTotal_ = CreateCounter("darkages_ticks_total", "Total number of game ticks processed");
    errorsTotal_ = CreateCounter("darkages_errors_total", "Total number of errors");
    packetsSentTotal_ = CreateCounter("darkages_packets_sent_total", "Total packets sent to clients");
    packetsLostTotal_ = CreateCounter("darkages_packets_lost_total", "Total packets lost");
    replicationBytesTotal_ = CreateCounter("darkages_replication_bytes_total", "Total replication bytes sent");
    antiCheatViolationsTotal_ = CreateCounter("darkages_anticheat_violations_total", "Total anti-cheat violations detected");
    
    // Gauges
    tickDurationMs_ = CreateGauge("darkages_tick_duration_ms", "Last tick duration in milliseconds");
    playerCount_ = CreateGauge("darkages_player_count", "Current number of connected players");
    playerCapacity_ = CreateGauge("darkages_player_capacity", "Maximum player capacity for zone");
    memoryUsedBytes_ = CreateGauge("darkages_memory_used_bytes", "Current memory usage in bytes");
    memoryTotalBytes_ = CreateGauge("darkages_memory_total_bytes", "Total memory available in bytes");
    dbConnected_ = CreateGauge("darkages_db_connected", "Database connection status (1=connected, 0=disconnected)");
    packetLossPercent_ = CreateGauge("darkages_packet_loss_percent", "Current packet loss percentage");
    replicationBandwidthBps_ = CreateGauge("darkages_replication_bandwidth_bps", "Current replication bandwidth in bytes/sec");
    
    // Histograms
    tickDurationHistogram_ = CreateHistogram("darkages_tick_duration_hist", 
        "Tick duration distribution", 
        {1.0, 5.0, 10.0, 16.0, 20.0, 50.0, 100.0});
    
    // Set default capacity (will be overridden by zone config)
    playerCapacity_->Set(1000.0, {{"zone_id", "1"}});
}

Counter* MetricsExporter::CreateCounter(const std::string& name, const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto counter = std::make_unique<Counter>(name, help);
    auto* ptr = counter.get();
    counters_[name] = std::move(counter);
    return ptr;
}

Gauge* MetricsExporter::CreateGauge(const std::string& name, const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto gauge = std::make_unique<Gauge>(name, help);
    auto* ptr = gauge.get();
    gauges_[name] = std::move(gauge);
    return ptr;
}

Histogram* MetricsExporter::CreateHistogram(const std::string& name, const std::string& help,
                                            const std::vector<double>& buckets) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto histogram = std::make_unique<Histogram>(name, help, buckets);
    auto* ptr = histogram.get();
    histograms_[name] = std::move(histogram);
    return ptr;
}

Counter* MetricsExporter::GetCounter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    return (it != counters_.end()) ? it->second.get() : nullptr;
}

Gauge* MetricsExporter::GetGauge(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(name);
    return (it != gauges_.end()) ? it->second.get() : nullptr;
}

Histogram* MetricsExporter::GetHistogram(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = histograms_.find(name);
    return (it != histograms_.end()) ? it->second.get() : nullptr;
}

std::string MetricsExporter::SerializeAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    
    // Serialize counters
    for (const auto& [name, counter] : counters_) {
        oss << counter->Serialize() << "\n";
    }
    
    // Serialize gauges
    for (const auto& [name, gauge] : gauges_) {
        oss << gauge->Serialize() << "\n";
    }
    
    // Serialize histograms
    for (const auto& [name, histogram] : histograms_) {
        oss << histogram->Serialize() << "\n";
    }
    
    return oss.str();
}

void MetricsExporter::HttpServerLoop() {
    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        
        SocketType clientSocket = accept(serverSocket_, 
            reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        
        if (clientSocket == -1) {
            if (running_) {
                std::cerr << "[METRICS] Accept failed\n";
            }
            continue;
        }
        
        // Handle request in a simple way (could spawn threads for concurrent requests)
        HandleRequest(clientSocket);
        CLOSE_SOCKET(clientSocket);
    }
}

void MetricsExporter::HandleRequest(int clientSocket) {
    char buffer[1024];
    int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (received <= 0) {
        return;
    }
    
    buffer[received] = '\0';
    std::string request(buffer);
    
    // Simple HTTP request handling
    if (request.find("GET /metrics") != std::string::npos ||
        request.find("GET / ") != std::string::npos) {
        
        std::string metrics = SerializeAll();
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/plain; version=0.0.4\r\n";
        response << "Content-Length: " << metrics.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << metrics;
        
        send(clientSocket, response.str().c_str(), static_cast<int>(response.str().length()), 0);
    } else {
        const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(clientSocket, notFound, static_cast<int>(strlen(notFound)), 0);
    }
}

// ============================================================================
// ScopedTimer Implementation
// ============================================================================

ScopedTimer::ScopedTimer(Histogram& histogram) : histogram_(histogram) {
    start_ = std::chrono::steady_clock::now();
}

ScopedTimer::~ScopedTimer() {
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
    double ms = duration.count() / 1000.0;
    histogram_.Observe(ms);
}

double ScopedTimer::ElapsedMs() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - start_);
    return duration.count() / 1000.0;
}

} // namespace Monitoring
} // namespace DarkAges
