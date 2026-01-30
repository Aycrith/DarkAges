#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>

namespace DarkAges {
namespace Memory {

// [PERFORMANCE_AGENT] Fixed-size memory pool
// Eliminates heap fragmentation and provides O(1) allocation

template<size_t BlockSize, size_t BlockCount>
class MemoryPool {
public:
    static_assert(BlockSize >= sizeof(void*), "BlockSize must be at least pointer size");
    
    MemoryPool();
    ~MemoryPool();
    
    // Allocate a block
    void* allocate();
    
    // Free a block
    void deallocate(void* ptr);
    
    // Get stats
    size_t getFreeBlocks() const { return freeBlocks_; }
    size_t getTotalBlocks() const { return BlockCount; }
    size_t getUsedBlocks() const { return BlockCount - freeBlocks_; }
    
    // Reset pool (dangerous - only use when all blocks are known free)
    void reset();

private:
    alignas(64) uint8_t buffer_[BlockSize * BlockCount];
    void* freeList_{nullptr};
    size_t freeBlocks_{BlockCount};
    mutable std::mutex mutex_;
};

// Common pool sizes
using SmallPool = MemoryPool<64, 10000>;      // Small components
using MediumPool = MemoryPool<256, 5000>;     // Medium components  
using LargePool = MemoryPool<1024, 1000>;     // Large components

// [PERFORMANCE_AGENT] Object pool for specific types
template<typename T, size_t PoolSize>
class ObjectPool {
public:
    ObjectPool();
    ~ObjectPool();
    
    // Construct object in pool
    template<typename... Args>
    T* acquire(Args&&... args);
    
    // Return object to pool
    void release(T* ptr);
    
    // Get stats
    size_t getAvailable() const;
    size_t getInUse() const;

private:
    MemoryPool<sizeof(T), PoolSize> pool_;
    std::atomic<size_t> inUse_{0};
};

// [PERFORMANCE_AGENT] Stack allocator for temporary allocations
class StackAllocator {
public:
    explicit StackAllocator(size_t capacity);
    ~StackAllocator();
    
    // Allocate from stack
    void* allocate(size_t size, size_t alignment = 8);
    
    // Reset stack (free all allocations)
    void reset();
    
    // Get used bytes
    size_t getUsed() const { return offset_; }
    size_t getCapacity() const { return capacity_; }

private:
    uint8_t* buffer_;
    size_t capacity_;
    size_t offset_{0};
};

// [PERFORMANCE_AGENT] Allocation tracker for debugging
class AllocationTracker {
public:
    struct AllocationInfo {
        void* ptr;
        size_t size;
        const char* file;
        int line;
        const char* function;
        uint64_t timestamp;
    };
    
    static AllocationTracker& instance();
    
    // Track allocation
    void trackAllocation(void* ptr, size_t size, const char* file, 
                        int line, const char* function);
    
    // Untrack allocation
    void untrackAllocation(void* ptr);
    
    // Get active allocations
    std::vector<AllocationInfo> getActiveAllocations() const;
    
    // Print leak report
    void printLeakReport() const;
    
    // Get total allocated bytes
    size_t getTotalAllocated() const { return totalAllocated_; }
    size_t getTotalFreed() const { return totalFreed_; }
    size_t getCurrentUsage() const { return totalAllocated_ - totalFreed_; }
    
    // Reset tracking
    void reset();

private:
    AllocationTracker() = default;
    
    std::unordered_map<void*, AllocationInfo> allocations_;
    mutable std::mutex mutex_;
    size_t totalAllocated_{0};
    size_t totalFreed_{0};
};

// Macros for tracking
#ifdef ENABLE_ALLOCATION_TRACKING
    #define TRACK_ALLOCATION(ptr, size) \
        DarkAges::Memory::AllocationTracker::instance().trackAllocation(\
            ptr, size, __FILE__, __LINE__, __FUNCTION__)
    #define UNTRACK_ALLOCATION(ptr) \
        DarkAges::Memory::AllocationTracker::instance().untrackAllocation(ptr)
#else
    #define TRACK_ALLOCATION(ptr, size) ((void)0)
    #define UNTRACK_ALLOCATION(ptr) ((void)0)
#endif

} // namespace Memory
} // namespace DarkAges

// Include template implementations
#include "memory/MemoryPool.inl"
