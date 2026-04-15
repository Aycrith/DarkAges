// [DATABASE_AGENT] Connection pool implementation for database clients
//
// Design: Thread-safe connection pool with:
// - Dynamic sizing (minPoolSize to maxPoolSize)
// - Lazy connection creation on demand
// - Health checking on acquire (detect dead connections)
// - Automatic reconnection while maintaining min pool size
// - Non-blocking acquire with timeout
// - Metrics tracking for monitoring
//
// This is a template class that can be specialized for different
// database clients (Redis, PostgreSQL, etc.) by implementing
// createConnection() and destroyConnection().

#ifndef DARKAGES_CONNECTION_POOL_HPP
#define DARKAGES_CONNECTION_POOL_HPP

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>

#ifdef REDIS_AVAILABLE
struct redisContext;
#endif

namespace DarkAges {

#ifdef REDIS_AVAILABLE
class ConnectionPool {
public:
    using Context = redisContext;

private:
    struct Connection {
        Context* ctx{nullptr};
        std::chrono::steady_clock::time_point lastUsed;
        bool inUse{false};
        uint64_t commandsExecuted{0};
    };

    std::vector<Connection> connections_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string host_;
    uint16_t port_{6379};
    size_t maxPoolSize_{10};
    size_t minPoolSize_{2};
    std::atomic<bool> shutdown_{false};
    std::atomic<uint64_t> totalCommands_{0};
    std::atomic<uint64_t> failedCommands_{0};

    Context* createConnection();
    void destroyConnection(Context* ctx);

public:
    ConnectionPool() = default;
    ~ConnectionPool() { shutdown(); }

    bool initialize(const std::string& host, uint16_t port,
                size_t minPoolSize = 2, size_t maxPoolSize = 10);

    Context* acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
    void release(Context* ctx);

    void shutdown();

    [[nodiscard]] size_t getPoolSize() const;
    [[nodiscard]] size_t getAvailableCount() const;

    void incrementCommands() { totalCommands_++; }
    void incrementFailed() { failedCommands_++; }
    [[nodiscard]] uint64_t getTotalCommands() const { return totalCommands_.load(); }
    [[nodiscard]] uint64_t getFailedCommands() const { return failedCommands_.load(); }
};
#else
class ConnectionPool {
public:
    using Context = void;

    ConnectionPool() = default;
    ~ConnectionPool() = default;

    bool initialize(const std::string& host, uint16_t port,
                    size_t minPoolSize = 2, size_t maxPoolSize = 10) {
        (void)host; (void)port; (void)minPoolSize; (void)maxPoolSize;
        return true;
    }

    Context* acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        (void)timeout;
        return nullptr;
    }

    void release(Context* ctx) { (void)ctx; }

    void shutdown() {}

    [[nodiscard]] size_t getPoolSize() const { return 0; }
    [[nodiscard]] size_t getAvailableCount() const { return 0; }

    void incrementCommands() {}
    void incrementFailed() {}
    [[nodiscard]] uint64_t getTotalCommands() const { return 0; }
    [[nodiscard]] uint64_t getFailedCommands() const { return 0; }
};
#endif

} // namespace DarkAges

#endif // DARKAGES_CONNECTION_POOL_HPP