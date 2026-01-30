// [PERFORMANCE_AGENT] Unit tests for memory pool allocators

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "memory/MemoryPool.hpp"
#include "memory/FixedVector.hpp"
#include "memory/LeakDetector.hpp"
#include <vector>

using namespace DarkAges::Memory;

TEST_CASE("MemoryPool basic operations", "[memory]") {
    SECTION("Allocate and deallocate single block") {
        MemoryPool<64, 100> pool;
        
        REQUIRE(pool.getFreeBlocks() == 100);
        REQUIRE(pool.getUsedBlocks() == 0);
        
        void* ptr = pool.allocate();
        REQUIRE(ptr != nullptr);
        REQUIRE(pool.getFreeBlocks() == 99);
        REQUIRE(pool.getUsedBlocks() == 1);
        
        pool.deallocate(ptr);
        REQUIRE(pool.getFreeBlocks() == 100);
        REQUIRE(pool.getUsedBlocks() == 0);
    }
    
    SECTION("Allocate multiple blocks") {
        MemoryPool<64, 10> pool;
        std::vector<void*> ptrs;
        
        for (int i = 0; i < 10; ++i) {
            void* ptr = pool.allocate();
            REQUIRE(ptr != nullptr);
            ptrs.push_back(ptr);
        }
        
        REQUIRE(pool.getFreeBlocks() == 0);
        
        // Should fail when exhausted
        void* extra = pool.allocate();
        REQUIRE(extra == nullptr);
        
        // Deallocate all
        for (void* ptr : ptrs) {
            pool.deallocate(ptr);
        }
        
        REQUIRE(pool.getFreeBlocks() == 10);
    }
    
    SECTION("Pool reset") {
        MemoryPool<64, 100> pool;
        
        void* ptr = pool.allocate();
        REQUIRE(ptr != nullptr);
        REQUIRE(pool.getUsedBlocks() == 1);
        
        pool.reset();
        REQUIRE(pool.getFreeBlocks() == 100);
        REQUIRE(pool.getUsedBlocks() == 0);
        
        // Can allocate again after reset
        void* ptr2 = pool.allocate();
        REQUIRE(ptr2 != nullptr);
    }
    
    SECTION("Deallocate null pointer") {
        MemoryPool<64, 100> pool;
        
        // Should not crash
        pool.deallocate(nullptr);
        REQUIRE(pool.getFreeBlocks() == 100);
    }
    
    SECTION("Deallocate foreign pointer") {
        MemoryPool<64, 100> pool;
        int x = 42;
        
        // Should not crash, should not affect pool
        pool.deallocate(&x);
        REQUIRE(pool.getFreeBlocks() == 100);
    }
}

TEST_CASE("ObjectPool basic operations", "[memory]") {
    struct TestObject {
        int x, y, z;
        TestObject() : x(0), y(0), z(0) {}
        TestObject(int a, int b, int c) : x(a), y(b), z(c) {}
    };
    
    SECTION("Acquire and release") {
        ObjectPool<TestObject, 10> pool;
        
        REQUIRE(pool.getAvailable() == 10);
        REQUIRE(pool.getInUse() == 0);
        
        TestObject* obj = pool.acquire();
        REQUIRE(obj != nullptr);
        REQUIRE(pool.getInUse() == 1);
        
        pool.release(obj);
        REQUIRE(pool.getAvailable() == 10);
        REQUIRE(pool.getInUse() == 0);
    }
    
    SECTION("Acquire with constructor arguments") {
        ObjectPool<TestObject, 10> pool;
        
        TestObject* obj = pool.acquire(1, 2, 3);
        REQUIRE(obj != nullptr);
        REQUIRE(obj->x == 1);
        REQUIRE(obj->y == 2);
        REQUIRE(obj->z == 3);
        
        pool.release(obj);
    }
    
    SECTION("Pool exhaustion") {
        ObjectPool<TestObject, 5> pool;
        std::vector<TestObject*> objs;
        
        for (int i = 0; i < 5; ++i) {
            TestObject* obj = pool.acquire();
            REQUIRE(obj != nullptr);
            objs.push_back(obj);
        }
        
        REQUIRE(pool.getAvailable() == 0);
        
        TestObject* extra = pool.acquire();
        REQUIRE(extra == nullptr);
        
        for (TestObject* obj : objs) {
            pool.release(obj);
        }
    }
}

TEST_CASE("StackAllocator basic operations", "[memory]") {
    SECTION("Allocate and reset") {
        StackAllocator alloc(1024);
        
        REQUIRE(alloc.getCapacity() == 1024);
        REQUIRE(alloc.getUsed() == 0);
        
        void* ptr = alloc.allocate(100);
        REQUIRE(ptr != nullptr);
        REQUIRE(alloc.getUsed() >= 100);
        
        void* ptr2 = alloc.allocate(200);
        REQUIRE(ptr2 != nullptr);
        
        alloc.reset();
        REQUIRE(alloc.getUsed() == 0);
    }
    
    SECTION("Aligned allocation") {
        StackAllocator alloc(1024);
        
        void* ptr1 = alloc.allocate(10, 8);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr1) % 8 == 0);
        
        void* ptr2 = alloc.allocate(10, 64);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr2) % 64 == 0);
    }
    
    SECTION("Out of memory") {
        StackAllocator alloc(100);
        
        void* ptr = alloc.allocate(200);
        REQUIRE(ptr == nullptr);
    }
    
    SECTION("LIFO behavior") {
        StackAllocator alloc(1024);
        
        void* ptr1 = alloc.allocate(100);
        void* ptr2 = alloc.allocate(100);
        
        // ptr2 should be after ptr1
        REQUIRE(ptr2 > ptr1);
    }
}

TEST_CASE("FixedVector basic operations", "[memory]") {
    SECTION("Push and access") {
        FixedVector<int, 10> vec;
        
        REQUIRE(vec.empty());
        REQUIRE(vec.size() == 0);
        REQUIRE(vec.capacity() == 10);
        
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
        
        REQUIRE(vec.size() == 3);
        REQUIRE(!vec.empty());
        REQUIRE(vec[0] == 1);
        REQUIRE(vec[1] == 2);
        REQUIRE(vec[2] == 3);
        REQUIRE(vec.front() == 1);
        REQUIRE(vec.back() == 3);
    }
    
    SECTION("Emplace back") {
        FixedVector<std::pair<int, int>, 10> vec;
        
        auto& p = vec.emplace_back(1, 2);
        REQUIRE(p.first == 1);
        REQUIRE(p.second == 2);
        REQUIRE(vec.size() == 1);
    }
    
    SECTION("Clear") {
        FixedVector<int, 10> vec;
        vec.push_back(1);
        vec.push_back(2);
        
        vec.clear();
        REQUIRE(vec.empty());
        REQUIRE(vec.size() == 0);
    }
    
    SECTION("Pop back") {
        FixedVector<int, 10> vec;
        vec.push_back(1);
        vec.push_back(2);
        
        vec.pop_back();
        REQUIRE(vec.size() == 1);
        REQUIRE(vec.back() == 1);
    }
    
    SECTION("Capacity exceeded throws") {
        FixedVector<int, 3> vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
        
        REQUIRE_THROWS_AS(vec.push_back(4), std::runtime_error);
    }
    
    SECTION("Iterators") {
        FixedVector<int, 10> vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
        
        int sum = 0;
        for (int x : vec) {
            sum += x;
        }
        REQUIRE(sum == 6);
    }
    
    SECTION("Find and erase") {
        FixedVector<int, 10> vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
        
        auto it = vec.find(2);
        REQUIRE(it != vec.end());
        
        vec.erase(it);
        REQUIRE(vec.size() == 2);
        REQUIRE(vec[0] == 1);
        REQUIRE(vec[1] == 3);
    }
    
    SECTION("Erase value") {
        FixedVector<int, 10> vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
        
        bool found = vec.erase_value(2);
        REQUIRE(found);
        REQUIRE(vec.size() == 2);
        
        bool notFound = vec.erase_value(99);
        REQUIRE(!notFound);
    }
}

TEST_CASE("AllocationTracker basic operations", "[memory]") {
    SECTION("Track and untrack") {
        AllocationTracker& tracker = AllocationTracker::instance();
        tracker.reset();
        
        REQUIRE(tracker.getCurrentUsage() == 0);
        
        int x = 42;
        tracker.trackAllocation(&x, sizeof(x), "test.cpp", 10, "test_func");
        
        REQUIRE(tracker.getCurrentUsage() == sizeof(x));
        REQUIRE(tracker.getTotalAllocated() == sizeof(x));
        
        tracker.untrackAllocation(&x);
        
        REQUIRE(tracker.getCurrentUsage() == 0);
        REQUIRE(tracker.getTotalFreed() == sizeof(x));
    }
    
    SECTION("Get active allocations") {
        AllocationTracker& tracker = AllocationTracker::instance();
        tracker.reset();
        
        int a = 1, b = 2;
        tracker.trackAllocation(&a, sizeof(a), "test.cpp", 10, "test_a");
        tracker.trackAllocation(&b, sizeof(b), "test.cpp", 20, "test_b");
        
        auto active = tracker.getActiveAllocations();
        REQUIRE(active.size() == 2);
        
        tracker.untrackAllocation(&a);
        tracker.untrackAllocation(&b);
    }
    
    SECTION("Reset") {
        AllocationTracker& tracker = AllocationTracker::instance();
        
        int x = 42;
        tracker.trackAllocation(&x, sizeof(x), "test.cpp", 10, "test_func");
        REQUIRE(tracker.getCurrentUsage() > 0);
        
        tracker.reset();
        REQUIRE(tracker.getCurrentUsage() == 0);
        REQUIRE(tracker.getTotalAllocated() == 0);
        REQUIRE(tracker.getTotalFreed() == 0);
    }
}

TEST_CASE("LeakDetector basic operations", "[memory]") {
    SECTION("No leak detected") {
        AllocationTracker& tracker = AllocationTracker::instance();
        tracker.reset();
        
        {
            LeakDetector detector("test_scope");
            // No allocations here
            REQUIRE(detector.verifyNoLeaks());
        }
    }
    
    SECTION("Leak detected") {
        AllocationTracker& tracker = AllocationTracker::instance();
        tracker.reset();
        
        bool noLeaks = false;
        {
            LeakDetector detector("test_scope");
            int* x = new int(42);
            tracker.trackAllocation(x, sizeof(int), "test.cpp", 10, "test_func");
            // Note: not deleting x to simulate leak
            noLeaks = detector.verifyNoLeaks();
        }
        
        REQUIRE_FALSE(noLeaks);
        
        // Cleanup
        auto active = tracker.getActiveAllocations();
        for (const auto& info : active) {
            tracker.untrackAllocation(info.ptr);
        }
    }
}

#ifdef CATCH_CONFIG_ENABLE_BENCHMARKING
TEST_CASE("MemoryPool performance", "[!benchmark][memory]") {
    BENCHMARK("MemoryPool allocate/deallocate") {
        MemoryPool<64, 1000> pool;
        void* ptrs[100];
        
        for (int i = 0; i < 100; ++i) {
            ptrs[i] = pool.allocate();
        }
        for (int i = 0; i < 100; ++i) {
            pool.deallocate(ptrs[i]);
        }
        
        return pool.getUsedBlocks();
    };
    
    BENCHMARK("StackAllocator allocate/reset") {
        StackAllocator alloc(1024 * 1024);
        
        for (int i = 0; i < 100; ++i) {
            alloc.allocate(64);
        }
        alloc.reset();
        
        return alloc.getUsed();
    };
}
#endif
