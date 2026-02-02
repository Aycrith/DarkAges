// [DATABASE_AGENT] Redis performance benchmarks
// Tracks baseline metrics for regression detection

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "db/RedisManager.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace DarkAges;

static bool isRedisAvailable() {
    RedisManager redis;
    return redis.initialize("localhost", 6379);
}

TEST_CASE("Redis Performance Baseline", "[.benchmark][redis][performance]") {
    if (!isRedisAvailable()) {
        SKIP("Redis not available - skipping benchmarks");
    }

    RedisManager redis;
    REQUIRE(redis.initialize("localhost", 6379));
    
    // Warmup
    std::atomic<int> warmupDone{0};
    for (int i = 0; i < 100; ++i) {
        redis.set("warmup:" + std::to_string(i), "value", 60,
                 [&warmupDone](bool) { warmupDone++; });
    }
    while (warmupDone < 100) {
        redis.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Redis Performance Baseline" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    SECTION("SET Operations") {
        std::atomic<bool> done{false};
        auto startTime = std::chrono::high_resolution_clock::now();
        
        BENCHMARK("Single SET operation") {
            done = false;
            redis.set("bench:set", "test_value", 60, [&done](bool success) {
                done = success;
            });
            while (!done) {
                redis.update();
            }
        };
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        std::cout << "SET Operation:" << std::endl;
        std::cout << "  Average latency: " << (duration.count() / 1.0) << " μs" << std::endl;
        std::cout << "  Target: < 5000 μs" << std::endl;
        std::cout << "  Status: " << (duration.count() < 5000 ? "✓ PASS" : "✗ FAIL") << std::endl;
        std::cout << std::endl;
    }
    
    SECTION("GET Operations") {
        // Pre-set a key
        std::atomic<bool> setDone{false};
        redis.set("bench:get", "test_value", 60, [&setDone](bool) { setDone = true; });
        while (!setDone) {
            redis.update();
        }
        
        std::atomic<bool> done{false};
        auto startTime = std::chrono::high_resolution_clock::now();
        
        BENCHMARK("Single GET operation") {
            done = false;
            redis.get("bench:get", [&done](const AsyncResult<std::string>&) {
                done = true;
            });
            while (!done) {
                redis.update();
            }
        };
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        std::cout << "GET Operation:" << std::endl;
        std::cout << "  Average latency: " << (duration.count() / 1.0) << " μs" << std::endl;
        std::cout << "  Target: < 5000 μs" << std::endl;
        std::cout << "  Status: " << (duration.count() < 5000 ? "✓ PASS" : "✗ FAIL") << std::endl;
        std::cout << std::endl;
    }
    
    SECTION("DEL Operations") {
        // Pre-set a key
        std::atomic<bool> setDone{false};
        redis.set("bench:del", "test_value", 60, [&setDone](bool) { setDone = true; });
        while (!setDone) {
            redis.update();
        }
        
        std::atomic<bool> done{false};
        auto startTime = std::chrono::high_resolution_clock::now();
        
        BENCHMARK("Single DEL operation") {
            // Set a fresh key for each iteration
            redis.set("bench:del_temp", "value", 60, [](bool){});
            redis.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            done = false;
            redis.del("bench:del_temp", [&done](bool) { done = true; });
            while (!done) {
                redis.update();
            }
        };
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        std::cout << "DEL Operation:" << std::endl;
        std::cout << "  Average latency: " << (duration.count() / 1.0) << " μs" << std::endl;
        std::cout << "  Target: < 5000 μs" << std::endl;
        std::cout << "  Status: " << (duration.count() < 5000 ? "✓ PASS" : "✗ FAIL") << std::endl;
        std::cout << std::endl;
    }
    
    SECTION("Throughput Test") {
        const int NUM_OPERATIONS = 1000;
        std::atomic<int> completed{0};
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < NUM_OPERATIONS; ++i) {
            redis.set("throughput:key" + std::to_string(i), "value", 60,
                     [&completed](bool success) {
                         if (success) completed++;
                     });
        }
        
        while (completed < NUM_OPERATIONS) {
            redis.update();
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        double opsPerSec = (NUM_OPERATIONS * 1000.0) / duration.count();
        
        std::cout << "Throughput Test (" << NUM_OPERATIONS << " operations):" << std::endl;
        std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
        std::cout << "  Operations/sec: " << static_cast<int>(opsPerSec) << std::endl;
        std::cout << "  Target: > 10,000 ops/sec" << std::endl;
        std::cout << "  Status: " << (opsPerSec > 10000 ? "✓ PASS" : "✗ FAIL") << std::endl;
        std::cout << std::endl;
    }
    
    SECTION("Redis Streams Performance") {
        std::atomic<bool> done{false};
        
        BENCHMARK("XADD operation") {
            done = false;
            std::unordered_map<std::string, std::string> fields{
                {"type", "benchmark"},
                {"value", "test"}
            };
            redis.xadd("bench:stream", "*", fields,
                      [&done](const AsyncResult<std::string>&) { done = true; });
            while (!done) {
                redis.update();
            }
        };
        
        std::cout << "XADD Operation: Baseline captured" << std::endl;
        std::cout << std::endl;
    }
    
    SECTION("Pipeline Performance") {
        const int BATCH_SIZE = 100;
        std::vector<std::pair<std::string, std::string>> batch;
        
        for (int i = 0; i < BATCH_SIZE; ++i) {
            batch.emplace_back("pipeline:key" + std::to_string(i), "value");
        }
        
        std::atomic<bool> done{false};
        auto startTime = std::chrono::high_resolution_clock::now();
        
        redis.pipelineSet(batch, 60, [&done](bool) { done = true; });
        
        while (!done) {
            redis.update();
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        double avgLatencyPerOp = duration.count() / static_cast<double>(BATCH_SIZE);
        
        std::cout << "Pipeline Performance (" << BATCH_SIZE << " operations):" << std::endl;
        std::cout << "  Total time: " << duration.count() << " μs" << std::endl;
        std::cout << "  Avg per operation: " << avgLatencyPerOp << " μs" << std::endl;
        std::cout << "  Target: < 1000 μs per op (with pipelining)" << std::endl;
        std::cout << "  Status: " << (avgLatencyPerOp < 1000 ? "✓ PASS" : "✗ FAIL") << std::endl;
        std::cout << std::endl;
    }
    
    // Print final metrics
    std::cout << "========================================" << std::endl;
    std::cout << "Redis Manager Metrics" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Commands sent: " << redis.getCommandsSent() << std::endl;
    std::cout << "  Commands completed: " << redis.getCommandsCompleted() << std::endl;
    std::cout << "  Commands failed: " << redis.getCommandsFailed() << std::endl;
    std::cout << "  Average latency: " << redis.getAverageLatencyMs() << " ms" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    redis.shutdown();
}
