#pragma once
#include "memory/MemoryPool.hpp"
#include <cstddef>

namespace DarkAges {
namespace Memory {

// [PERFORMANCE_AGENT] RAII leak detector for scope-based verification
class LeakDetector {
public:
    explicit LeakDetector(const char* scopeName);
    ~LeakDetector();
    
    // Verify no allocations occurred in scope
    bool verifyNoLeaks();
    
    // Get allocation delta
    size_t getAllocationDelta() const { return (totalAllocatedEnd_ - totalFreedEnd_) - (totalAllocatedStart_ - totalFreedStart_); }

private:
    const char* scopeName_;
    size_t totalAllocatedStart_;
    size_t totalFreedStart_;
    size_t totalAllocatedEnd_;
    size_t totalFreedEnd_;
    bool checked_;
};

// Macro for easy usage
#define SCOPED_LEAK_CHECK() \
    DarkAges::Memory::LeakDetector _leak_check_##__LINE__(__FUNCTION__)

} // namespace Memory
} // namespace DarkAges
