#pragma once
#include "memory/MemoryPool.hpp"
#include <memory>
#include <limits>
#include <cstddef>

namespace DarkAges {
namespace Memory {

// [PERFORMANCE_AGENT] STL-compatible allocator using MemoryPool
// Allows STL containers to use pre-allocated memory pools

template<typename T, size_t BlockSize, size_t BlockCount>
class PooledAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;
    
    // Rebind allocator to type U
    template<typename U>
    struct rebind {
        using other = PooledAllocator<U, BlockSize, BlockCount>;
    };

    PooledAllocator() noexcept = default;
    
    template<typename U>
    PooledAllocator(const PooledAllocator<U, BlockSize, BlockCount>&) noexcept {}
    
    PooledAllocator(const PooledAllocator&) noexcept = default;
    PooledAllocator(PooledAllocator&&) noexcept = default;
    PooledAllocator& operator=(const PooledAllocator&) noexcept = default;
    PooledAllocator& operator=(PooledAllocator&&) noexcept = default;
    
    ~PooledAllocator() = default;

    // Allocate memory from pool
    T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        
        std::size_t bytes = n * sizeof(T);
        
        // If fits in pool block, use pool
        if (bytes <= BlockSize) {
            void* ptr = pool_.allocate();
            if (!ptr) {
                throw std::bad_alloc();
            }
            return static_cast<T*>(ptr);
        }
        
        // Otherwise fall back to heap
        return static_cast<T*>(::operator new(bytes));
    }

    // Deallocate memory
    void deallocate(T* ptr, std::size_t n) noexcept {
        if (!ptr) return;
        
        std::size_t bytes = n * sizeof(T);
        
        // Check if pointer is in pool
        if (bytes <= BlockSize) {
            pool_.deallocate(ptr);
        } else {
            ::operator delete(ptr);
        }
    }

    // Construct object
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }

    // Destroy object
    template<typename U>
    void destroy(U* p) {
        p->~U();
    }

    // Get pool stats
    static size_t getPoolFreeBlocks() { return pool_.getFreeBlocks(); }
    static size_t getPoolTotalBlocks() { return pool_.getTotalBlocks(); }
    static size_t getPoolUsedBlocks() { return pool_.getUsedBlocks(); }

private:
    // Static pool shared by all allocators of this type
    static MemoryPool<BlockSize, BlockCount> pool_;
};

// Static member definition
template<typename T, size_t BlockSize, size_t BlockCount>
MemoryPool<BlockSize, BlockCount> PooledAllocator<T, BlockSize, BlockCount>::pool_;

// Comparison operators
template<typename T, typename U, size_t BlockSize, size_t BlockCount>
bool operator==(const PooledAllocator<T, BlockSize, BlockCount>&,
                const PooledAllocator<U, BlockSize, BlockCount>&) noexcept {
    return true;
}

template<typename T, typename U, size_t BlockSize, size_t BlockCount>
bool operator!=(const PooledAllocator<T, BlockSize, BlockCount>&,
                const PooledAllocator<U, BlockSize, BlockCount>&) noexcept {
    return false;
}

// Convenience type aliases
using SmallPooledAllocator = PooledAllocator<char, 64, 10000>;
using MediumPooledAllocator = PooledAllocator<char, 256, 5000>;
using LargePooledAllocator = PooledAllocator<char, 1024, 1000>;

} // namespace Memory
} // namespace DarkAges
