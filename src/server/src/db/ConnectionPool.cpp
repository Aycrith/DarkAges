// [DATABASE_AGENT] Connection pool implementation

#include "db/ConnectionPool.hpp"
#include "Constants.hpp"
#include <iostream>
#include <chrono>

#ifdef REDIS_AVAILABLE
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <hiredis.h>
#endif

namespace DarkAges {

#ifdef REDIS_AVAILABLE

ConnectionPool::Context* ConnectionPool::createConnection() {
    struct timeval timeout;
    timeout.tv_sec = Constants::REDIS_CONNECTION_TIMEOUT_MS / 1000;
    timeout.tv_usec = (Constants::REDIS_CONNECTION_TIMEOUT_MS % 1000) * 1000;

    Context* ctx = redisConnectWithTimeout(host_.c_str(), port_, timeout);
    if (!ctx || ctx->err) {
        if (ctx) {
            std::cerr << "[CONNECTION_POOL] Connection error: " << ctx->errstr << std::endl;
            redisFree(ctx);
        } else {
            std::cerr << "[CONNECTION_POOL] Failed to allocate context" << std::endl;
        }
        return nullptr;
    }

    redisEnableKeepAlive(ctx);
    return ctx;
}

void ConnectionPool::destroyConnection(Context* ctx) {
    if (ctx) {
        redisFree(ctx);
    }
}

bool ConnectionPool::initialize(const std::string& host, uint16_t port,
                          size_t minPoolSize, size_t maxPoolSize) {
    host_ = host;
    port_ = port;
    minPoolSize_ = minPoolSize;
    maxPoolSize_ = maxPoolSize;
    shutdown_ = false;

    std::cout << "[CONNECTION_POOL] Initializing (" << minPoolSize_ << "-" << maxPoolSize_ << ")..." << std::endl;

    for (size_t i = 0; i < minPoolSize_; ++i) {
        auto* ctx = createConnection();
        if (!ctx) {
            std::cerr << "[CONNECTION_POOL] Failed to create initial connection " << i << std::endl;
            shutdown();
            return false;
        }

        Connection conn;
        conn.ctx = ctx;
        conn.lastUsed = std::chrono::steady_clock::now();
        conn.inUse = false;
        connections_.push_back(conn);
    }

    std::cout << "[CONNECTION_POOL] Initialized with " << connections_.size() << " connections" << std::endl;
    return true;
}

ConnectionPool::Context* ConnectionPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto pred = [this]() {
        if (shutdown_) return true;
        for (auto& conn : connections_) {
            if (!conn.inUse) return true;
        }
        return connections_.size() < maxPoolSize_;
    };

    if (!cv_.wait_for(lock, timeout, pred)) {
        return nullptr;
    }

    if (shutdown_) return nullptr;

    for (auto& conn : connections_) {
        if (!conn.inUse) {
            if (conn.ctx && conn.ctx->err) {
                redisFree(conn.ctx);
                conn.ctx = createConnection();
                if (!conn.ctx) continue;
            }

            conn.inUse = true;
            conn.lastUsed = std::chrono::steady_clock::now();
            return conn.ctx;
        }
    }

    if (connections_.size() < maxPoolSize_) {
        auto* ctx = createConnection();
        if (ctx) {
            Connection conn;
            conn.ctx = ctx;
            conn.lastUsed = std::chrono::steady_clock::now();
            conn.inUse = true;
            connections_.push_back(conn);
            return ctx;
        }
    }

    return nullptr;
}

void ConnectionPool::release(Context* ctx) {
    if (!ctx) return;

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& conn : connections_) {
        if (conn.ctx == ctx) {
            if (ctx->err) {
                redisFree(ctx);
                conn.ctx = nullptr;
                conn.inUse = false;

                if (connections_.size() <= minPoolSize_) {
                    conn.ctx = createConnection();
                }
            } else {
                conn.inUse = false;
                conn.lastUsed = std::chrono::steady_clock::now();
            }
            break;
        }
    }

    cv_.notify_one();
}

void ConnectionPool::shutdown() {
    shutdown_ = true;
    cv_.notify_all();

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& conn : connections_) {
        if (conn.ctx) {
            redisFree(conn.ctx);
            conn.ctx = nullptr;
        }
    }

    connections_.clear();
}

size_t ConnectionPool::getPoolSize() const {
    return connections_.size();
}

size_t ConnectionPool::getAvailableCount() const {
    size_t count = 0;
    for (const auto& conn : connections_) {
        if (!conn.inUse) count++;
    }
    return count;
}

#endif // REDIS_AVAILABLE

} // namespace DarkAges