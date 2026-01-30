#pragma once
#include "memory/MemoryPool.hpp"
#include <cstring>
#include <new>

namespace DarkAges {
namespace Memory {

// ============================================================================
// MemoryPool Implementation
// ============================================================================

template<size_t BlockSize, size_t BlockCount>
MemoryPool<BlockSize, BlockCount>::MemoryPool() {
    // Initialize free list as linked list
    uint8_t* current = buffer_;
    for (size_t i = 0; i < BlockCount - 1; ++i) {
        void** next = reinterpret_cast<void**>(current);
        *next = current + BlockSize;
        current += BlockSize;
    }
    
    // Last block points to null
    void** last = reinterpret_cast<void**>(current);
    *last = nullptr;
    
    freeList_ = buffer_;
}

template<size_t BlockSize, size_t BlockCount>
MemoryPool<BlockSize, BlockCount>::~MemoryPool() {
    // In debug mode, check for leaks
#ifndef NDEBUG
    if (freeBlocks_ != BlockCount) {
        std::cerr << "[MEMORY] MemoryPool leak detected: " 
                  << (BlockCount - freeBlocks_) << " blocks not freed\n";
    }
#endif
}

template<size_t BlockSize, size_t BlockCount>
void* MemoryPool<BlockSize, BlockCount>::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!freeList_) {
        return nullptr;  // Pool exhausted
    }
    
    void* result = freeList_;
    void** next = reinterpret_cast<void**>(freeList_);
    freeList_ = *next;
    --freeBlocks_;
    
    return result;
}

template<size_t BlockSize, size_t BlockCount>
void MemoryPool<BlockSize, BlockCount>::deallocate(void* ptr) {
    if (!ptr) return;
    
    // Verify pointer is in our buffer
    uint8_t* bytePtr = static_cast<uint8_t*>(ptr);
    if (bytePtr < buffer_ || bytePtr >= buffer_ + BlockSize * BlockCount) {
        return;  // Not our pointer
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    void** node = reinterpret_cast<void**>(ptr);
    *node = freeList_;
    freeList_ = ptr;
    ++freeBlocks_;
}

template<size_t BlockSize, size_t BlockCount>
void MemoryPool<BlockSize, BlockCount>::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint8_t* current = buffer_;
    for (size_t i = 0; i < BlockCount - 1; ++i) {
        void** next = reinterpret_cast<void**>(current);
        *next = current + BlockSize;
        current += BlockSize;
    }
    
    void** last = reinterpret_cast<void**>(current);
    *last = nullptr;
    
    freeList_ = buffer_;
    freeBlocks_ = BlockCount;
}

// ============================================================================
// ObjectPool Implementation
// ============================================================================

template<typename T, size_t PoolSize>
ObjectPool<T, PoolSize>::ObjectPool() = default;

template<typename T, size_t PoolSize>
ObjectPool<T, PoolSize>::~ObjectPool() {
    // Note: Objects should be released before pool is destroyed
    // We don't call destructors here - caller must manage object lifetime
}

template<typename T, size_t PoolSize>
template<typename... Args>
T* ObjectPool<T, PoolSize>::acquire(Args&&... args) {
    void* memory = pool_.allocate();
    if (!memory) {
        return nullptr;  // Pool exhausted
    }
    
    ++inUse_;
    return new (memory) T(std::forward<Args>(args)...);
}

template<typename T, size_t PoolSize>
void ObjectPool<T, PoolSize>::release(T* ptr) {
    if (!ptr) return;
    
    ptr->~T();  // Call destructor
    pool_.deallocate(ptr);
    --inUse_;
}

template<typename T, size_t PoolSize>
size_t ObjectPool<T, PoolSize>::getAvailable() const {
    return PoolSize - inUse_.load();
}

template<typename T, size_t PoolSize>
size_t ObjectPool<T, PoolSize>::getInUse() const {
    return inUse_.load();
}

} // namespace Memory
} // namespace DarkAges
