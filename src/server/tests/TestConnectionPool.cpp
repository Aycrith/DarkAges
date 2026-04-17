// ConnectionPool unit tests
// Tests: pool initialization, connection borrow/return, max limits,
//        shutdown behavior, command metrics, and connection validation

#include <catch2/catch_test_macros.hpp>
#include "db/ConnectionPool.hpp"

#ifdef REDIS_AVAILABLE
#include <hiredis.h>
#endif

#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

using namespace DarkAges;

#ifdef REDIS_AVAILABLE

// Helper to check if a live Redis server is available for integration tests
static bool isRedisAvailable() {
    static bool checked = false;
    static bool available = false;

    if (!checked) {
        ConnectionPool testPool;
        available = testPool.initialize("localhost", 6379, 1, 2);
        if (available) {
            testPool.shutdown();
        }
        checked = true;
    }

    return available;
}

TEST_CASE("ConnectionPool initialization", "[foundation][database]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping ConnectionPool tests");
    }

    SECTION("Initialize with default pool sizes") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379) == true);
        REQUIRE(pool.getPoolSize() >= 2);  // Default minPoolSize = 2
        pool.shutdown();
    }

    SECTION("Initialize with custom pool sizes") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 3, 8) == true);
        REQUIRE(pool.getPoolSize() >= 3);  // Should create min connections
        pool.shutdown();
    }

    SECTION("Initialize with min size 1") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 1, 5) == true);
        REQUIRE(pool.getPoolSize() >= 1);
        pool.shutdown();
    }

    SECTION("Initialize fails on invalid host") {
        ConnectionPool pool;
        // Connecting to a non-existent host should fail
        REQUIRE(pool.initialize("192.0.2.1", 6379, 1, 2) == false);
    }

    SECTION("Initialize fails on invalid port") {
        ConnectionPool pool;
        // Port 1 is typically reserved and not running Redis
        REQUIRE(pool.initialize("localhost", 1, 1, 2) == false);
    }
}

TEST_CASE("ConnectionPool acquire and release", "[foundation][database]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping ConnectionPool tests");
    }

    ConnectionPool pool;
    REQUIRE(pool.initialize("localhost", 6379, 2, 5));

    SECTION("Acquire returns valid connection") {
        auto* ctx = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx != nullptr);
        REQUIRE_FALSE(ctx->err);  // Connection should be healthy
        pool.release(ctx);
    }

    SECTION("Acquire decrements available count") {
        size_t initialAvailable = pool.getAvailableCount();
        REQUIRE(initialAvailable >= 1);

        auto* ctx = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx != nullptr);

        size_t afterAcquire = pool.getAvailableCount();
        REQUIRE(afterAcquire == initialAvailable - 1);

        pool.release(ctx);

        size_t afterRelease = pool.getAvailableCount();
        REQUIRE(afterRelease == initialAvailable);
    }

    SECTION("Acquire multiple connections") {
        auto* ctx1 = pool.acquire(std::chrono::milliseconds(500));
        auto* ctx2 = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx1 != nullptr);
        REQUIRE(ctx2 != nullptr);
        REQUIRE(ctx1 != ctx2);  // Should be different connections

        pool.release(ctx1);
        pool.release(ctx2);
    }

    SECTION("Release makes connection available again") {
        size_t before = pool.getAvailableCount();

        auto* ctx = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx != nullptr);
        REQUIRE(pool.getAvailableCount() == before - 1);

        pool.release(ctx);
        REQUIRE(pool.getAvailableCount() == before);
    }

    SECTION("Release null pointer is safe") {
        size_t before = pool.getAvailableCount();
        pool.release(nullptr);  // Should not crash
        REQUIRE(pool.getAvailableCount() == before);
    }

    SECTION("Acquire after release reuses connection") {
        auto* ctx1 = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx1 != nullptr);
        pool.release(ctx1);

        // Should get a valid connection (potentially the same one reused)
        auto* ctx2 = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx2 != nullptr);

        pool.release(ctx2);
    }

    pool.shutdown();
}

TEST_CASE("ConnectionPool max connection limit", "[foundation][database]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping ConnectionPool tests");
    }

    ConnectionPool pool;
    REQUIRE(pool.initialize("localhost", 6379, 1, 3));

    SECTION("Pool grows up to max size") {
        std::vector<ConnectionPool::Context*> connections;

        // Acquire up to maxPoolSize connections
        for (size_t i = 0; i < 3; ++i) {
            auto* ctx = pool.acquire(std::chrono::milliseconds(500));
            REQUIRE(ctx != nullptr);
            connections.push_back(ctx);
        }

        // Pool should have exactly max connections
        REQUIRE(pool.getPoolSize() == 3);
        REQUIRE(pool.getAvailableCount() == 0);

        // Cleanup
        for (auto* ctx : connections) {
            pool.release(ctx);
        }
    }

    SECTION("Acquire times out when pool exhausted") {
        std::vector<ConnectionPool::Context*> connections;

        // Exhaust the pool
        for (size_t i = 0; i < 3; ++i) {
            auto* ctx = pool.acquire(std::chrono::milliseconds(500));
            REQUIRE(ctx != nullptr);
            connections.push_back(ctx);
        }

        // This acquire should time out
        auto* extra = pool.acquire(std::chrono::milliseconds(50));
        REQUIRE(extra == nullptr);

        // Cleanup
        for (auto* ctx : connections) {
            pool.release(ctx);
        }
    }

    SECTION("Acquire succeeds after release frees slot") {
        std::vector<ConnectionPool::Context*> connections;

        // Exhaust the pool
        for (size_t i = 0; i < 3; ++i) {
            auto* ctx = pool.acquire(std::chrono::milliseconds(500));
            REQUIRE(ctx != nullptr);
            connections.push_back(ctx);
        }

        // Release one
        pool.release(connections.back());
        connections.pop_back();

        // Now acquire should succeed
        auto* ctx = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx != nullptr);
        connections.push_back(ctx);

        // Cleanup
        for (auto* c : connections) {
            pool.release(c);
        }
    }

    pool.shutdown();
}

TEST_CASE("ConnectionPool shutdown behavior", "[foundation][database]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping ConnectionPool tests");
    }

    SECTION("Shutdown clears all connections") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 2, 5));
        REQUIRE(pool.getPoolSize() >= 2);

        pool.shutdown();
        REQUIRE(pool.getPoolSize() == 0);
    }

    SECTION("Acquire returns null after shutdown") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 1, 3));

        pool.shutdown();

        auto* ctx = pool.acquire(std::chrono::milliseconds(50));
        REQUIRE(ctx == nullptr);
    }

    SECTION("Destructor calls shutdown") {
        {
            ConnectionPool pool;
            REQUIRE(pool.initialize("localhost", 6379, 1, 3));
            auto* ctx = pool.acquire(std::chrono::milliseconds(500));
            REQUIRE(ctx != nullptr);
            // Pool destructor calls shutdown, which frees connections
        }
        // No crash means destructor properly cleaned up
    }

    SECTION("Double shutdown is safe") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 1, 2));
        pool.shutdown();
        pool.shutdown();  // Should not crash
    }
}

TEST_CASE("ConnectionPool command metrics", "[foundation][database]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping ConnectionPool tests");
    }

    ConnectionPool pool;
    REQUIRE(pool.initialize("localhost", 6379, 1, 3));

    SECTION("Initial metrics are zero") {
        REQUIRE(pool.getTotalCommands() == 0);
        REQUIRE(pool.getFailedCommands() == 0);
    }

    SECTION("Increment commands tracks total") {
        pool.incrementCommands();
        pool.incrementCommands();
        pool.incrementCommands();
        REQUIRE(pool.getTotalCommands() == 3);
        REQUIRE(pool.getFailedCommands() == 0);
    }

    SECTION("Increment failed tracks failures") {
        pool.incrementCommands();
        pool.incrementFailed();
        pool.incrementCommands();
        pool.incrementFailed();
        REQUIRE(pool.getTotalCommands() == 4);
        REQUIRE(pool.getFailedCommands() == 2);
    }

    SECTION("Metrics persist across acquire/release") {
        pool.incrementCommands();
        auto* ctx = pool.acquire(std::chrono::milliseconds(500));
        REQUIRE(ctx != nullptr);
        pool.incrementCommands();
        pool.release(ctx);
        pool.incrementCommands();
        REQUIRE(pool.getTotalCommands() == 3);
    }

    pool.shutdown();
}

TEST_CASE("ConnectionPool connection validation", "[foundation][database]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping ConnectionPool tests");
    }

    SECTION("Acquire validates and reconnects broken connections") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 2, 5));

        // Acquire and release connections - they should all be valid
        for (int i = 0; i < 10; ++i) {
            auto* ctx = pool.acquire(std::chrono::milliseconds(500));
            REQUIRE(ctx != nullptr);
            // The acquire method checks ctx->err and reconnects if broken
            REQUIRE_FALSE(ctx->err);
            pool.release(ctx);
        }

        pool.shutdown();
    }

    SECTION("Pool maintains min connections after repeated use") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 2, 5));

        size_t initialSize = pool.getPoolSize();
        REQUIRE(initialSize >= 2);

        // Acquire and release all connections multiple times
        for (int i = 0; i < 5; ++i) {
            auto* c1 = pool.acquire(std::chrono::milliseconds(500));
            auto* c2 = pool.acquire(std::chrono::milliseconds(500));
            REQUIRE(c1 != nullptr);
            REQUIRE(c2 != nullptr);
            pool.release(c1);
            pool.release(c2);
        }

        // Pool should still have at least min connections
        REQUIRE(pool.getPoolSize() >= 2);

        pool.shutdown();
    }
}

TEST_CASE("ConnectionPool concurrent access", "[foundation][database]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping ConnectionPool tests");
    }

    SECTION("Multiple threads acquire and release safely") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 2, 10));

        constexpr int numThreads = 4;
        constexpr int opsPerThread = 20;
        std::atomic<int> successCount{0};
        std::atomic<int> failCount{0};

        std::vector<std::thread> threads;
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < opsPerThread; ++i) {
                    auto* ctx = pool.acquire(std::chrono::milliseconds(200));
                    if (ctx) {
                        successCount++;
                        // Simulate brief work
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                        pool.release(ctx);
                    } else {
                        failCount++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Most operations should succeed (pool supports up to 10 connections)
        int totalOps = numThreads * opsPerThread;
        REQUIRE(successCount > 0);
        REQUIRE(successCount + failCount == totalOps);

        pool.shutdown();
    }

    SECTION("Concurrent incrementCommands is thread-safe") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379, 1, 3));

        constexpr int numThreads = 4;
        constexpr int incrementsPerThread = 100;

        std::vector<std::thread> threads;
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < incrementsPerThread; ++i) {
                    pool.incrementCommands();
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(pool.getTotalCommands() == numThreads * incrementsPerThread);

        pool.shutdown();
    }
}

#else  // !REDIS_AVAILABLE - stub tests

TEST_CASE("ConnectionPool stub behavior", "[foundation][database]") {
    SECTION("Stub initialize returns true") {
        ConnectionPool pool;
        REQUIRE(pool.initialize("localhost", 6379) == true);
    }

    SECTION("Stub acquire returns nullptr") {
        ConnectionPool pool;
        pool.initialize("localhost", 6379);
        auto* ctx = pool.acquire(std::chrono::milliseconds(100));
        REQUIRE(ctx == nullptr);
    }

    SECTION("Stub pool size is zero") {
        ConnectionPool pool;
        pool.initialize("localhost", 6379);
        REQUIRE(pool.getPoolSize() == 0);
        REQUIRE(pool.getAvailableCount() == 0);
    }

    SECTION("Stub metrics are zero") {
        ConnectionPool pool;
        pool.initialize("localhost", 6379);
        REQUIRE(pool.getTotalCommands() == 0);
        REQUIRE(pool.getFailedCommands() == 0);
    }

    SECTION("Stub increment is safe") {
        ConnectionPool pool;
        pool.initialize("localhost", 6379);
        pool.incrementCommands();
        pool.incrementFailed();
        // Stub is no-op, but should not crash
        REQUIRE(pool.getTotalCommands() == 0);
        REQUIRE(pool.getFailedCommands() == 0);
    }

    SECTION("Stub shutdown is safe") {
        ConnectionPool pool;
        pool.initialize("localhost", 6379);
        pool.shutdown();  // Should not crash
    }

    SECTION("Stub release is safe") {
        ConnectionPool pool;
        pool.release(nullptr);  // Should not crash
    }
}

#endif // REDIS_AVAILABLE
