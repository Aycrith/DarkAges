#include "memory/LeakDetector.hpp"
#include <iostream>
#include <cstddef>

namespace DarkAges {
namespace Memory {

LeakDetector::LeakDetector(const char* scopeName)
    : scopeName_(scopeName)
    , totalAllocatedStart_(AllocationTracker::instance().getTotalAllocated())
    , totalFreedStart_(AllocationTracker::instance().getTotalFreed())
    , totalAllocatedEnd_(0)
    , totalFreedEnd_(0)
    , checked_(false) {
}

LeakDetector::~LeakDetector() {
    if (!checked_) {
        verifyNoLeaks();
    }
}

bool LeakDetector::verifyNoLeaks() {
    checked_ = true;
    
    totalAllocatedEnd_ = AllocationTracker::instance().getTotalAllocated();
    totalFreedEnd_ = AllocationTracker::instance().getTotalFreed();
    
    size_t allocatedInScope = totalAllocatedEnd_ - totalAllocatedStart_;
    size_t freedInScope = totalFreedEnd_ - totalFreedStart_;
    size_t leaked = allocatedInScope - freedInScope;
    
    if (leaked > 0) {
        std::cerr << "[MEMORY] Leak detected in " << scopeName_ 
                  << ": " << leaked << " bytes leaked\n";
        
        // Print active allocations for debugging
        auto active = AllocationTracker::instance().getActiveAllocations();
        std::cerr << "Active allocations at end of scope:\n";
        for (const auto& info : active) {
            std::cerr << "  " << info.file << ":" << info.line 
                      << " " << info.function << " " << info.size << " bytes\n";
        }
        
        return false;
    }
    
    return true;
}

} // namespace Memory
} // namespace DarkAges
