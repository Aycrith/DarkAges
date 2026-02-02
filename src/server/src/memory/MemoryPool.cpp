#include "memory/MemoryPool.hpp"
#include <cstring>
#include <iostream>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <vector>
#include <cstdlib>  // For aligned_alloc/free

#ifdef _WIN32
#include <malloc.h>  // For _aligned_malloc/_aligned_free
#endif

namespace DarkAges {
namespace Memory {

// ============================================================================
// StackAllocator Implementation
// ============================================================================

StackAllocator::StackAllocator(size_t capacity) 
    : capacity_(capacity), offset_(0) {
    // Allocate with extra space for alignment
    // Use alignas or aligned_alloc for proper alignment
    #ifdef _WIN32
        buffer_ = static_cast<uint8_t*>(_aligned_malloc(capacity, 64));
    #else
        buffer_ = static_cast<uint8_t*>(std::aligned_alloc(64, capacity));
    #endif
}

StackAllocator::~StackAllocator() {
    #ifdef _WIN32
        _aligned_free(buffer_);
    #else
        std::free(buffer_);
    #endif
}

void* StackAllocator::allocate(size_t size, size_t alignment) {
    // Align offset
    size_t alignedOffset = (offset_ + alignment - 1) & ~(alignment - 1);
    
    if (alignedOffset + size > capacity_) {
        return nullptr;  // Out of memory
    }
    
    void* result = buffer_ + alignedOffset;
    offset_ = alignedOffset + size;
    
    return result;
}

void StackAllocator::reset() {
    offset_ = 0;
}

// ============================================================================
// AllocationTracker Implementation
// ============================================================================

AllocationTracker& AllocationTracker::instance() {
    static AllocationTracker instance;
    return instance;
}

void AllocationTracker::trackAllocation(void* ptr, size_t size, 
                                       const char* file, int line, 
                                       const char* function) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    AllocationInfo info;
    info.ptr = ptr;
    info.size = size;
    info.file = file;
    info.line = line;
    info.function = function;
    info.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    allocations_[ptr] = info;
    totalAllocated_ += size;
}

void AllocationTracker::untrackAllocation(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        totalFreed_ += it->second.size;
        allocations_.erase(it);
    }
}

std::vector<AllocationTracker::AllocationInfo> 
AllocationTracker::getActiveAllocations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<AllocationInfo> result;
    for (const auto& [ptr, info] : allocations_) {
        result.push_back(info);
    }
    
    return result;
}

void AllocationTracker::printLeakReport() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (allocations_.empty()) {
        std::cout << "[MEMORY] No leaks detected\n";
        return;
    }
    
    std::cerr << "[MEMORY] Leak Report: " << allocations_.size() 
              << " allocations, " << (totalAllocated_ - totalFreed_) 
              << " bytes\n";
    std::cerr << "================================================\n";
    
    for (const auto& [ptr, info] : allocations_) {
        std::cerr << info.file << ":" << info.line 
                  << " " << info.function
                  << " " << info.size << " bytes\n";
    }
}

void AllocationTracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    allocations_.clear();
    totalAllocated_ = 0;
    totalFreed_ = 0;
}

// ============================================================================
// Explicit template instantiations
// ============================================================================

template class MemoryPool<64, 10000>;
template class MemoryPool<256, 5000>;
template class MemoryPool<1024, 1000>;

} // namespace Memory
} // namespace DarkAges
