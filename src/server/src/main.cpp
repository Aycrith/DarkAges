// DarkAges MMO Server - Main Entry Point
// Minimal version for initial build verification

#include <iostream>
#include <cstdlib>

// Only include what we know compiles
#include "physics/SpatialHash.hpp"
#include "ecs/CoreTypes.hpp"
#include "Constants.hpp"

using namespace DarkAges;

int main() {
    std::cout << "========================================\n";
    std::cout << "DarkAges MMO Server\n";
    std::cout << "Version: 0.1.0-alpha\n";
    std::cout << "========================================\n\n";
    
    // Basic sanity check - test that core types work
    std::cout << "Testing core systems...\n";
    
    try {
        // Test SpatialHash
        SpatialHash hash(10.0f);
        hash.insert(static_cast<EntityID>(1), 5.0f, 5.0f);
        auto results = hash.query(5.0f, 5.0f, 1.0f);
        
        if (results.size() == 1) {
            std::cout << "✓ SpatialHash: OK\n";
        } else {
            std::cout << "✗ SpatialHash: FAILED\n";
            return 1;
        }
        
        // Test Position
        Position pos{1000, 2000, 3000};  // 1.0, 2.0, 3.0
        auto vec = pos.toVec3();
        if (vec.x > 0.9f && vec.x < 1.1f) {
            std::cout << "✓ Position: OK\n";
        } else {
            std::cout << "✗ Position: FAILED\n";
            return 1;
        }
        
        // Test Constants
        if (Constants::TICK_RATE_HZ == 60) {
            std::cout << "✓ Constants: OK\n";
        } else {
            std::cout << "✗ Constants: FAILED\n";
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "ERROR: Unknown exception\n";
        return 1;
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Basic verification passed!\n";
    std::cout << "========================================\n";
    std::cout << "\nNOTE: This is a minimal build for testing.\n";
    std::cout << "Full server features require additional dependencies.\n";
    
    return 0;
}
