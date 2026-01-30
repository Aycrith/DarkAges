// TestMain.cpp - Minimal test entry point
// This file provides a basic test harness that doesn't require Catch2
// Useful for initial bring-up when dependencies aren't available

#include <iostream>
#include <vector>
#include <string>
#include <functional>

// Simple test framework for initial bring-up
struct TestCase {
    std::string name;
    std::function<bool()> test;
};

static std::vector<TestCase> g_tests;

void registerTest(const std::string& name, std::function<bool()> test) {
    g_tests.push_back({name, test});
}

// Include the actual tests
// These will register themselves using the macro below

#define TEST(name) \
    static bool test_##name(); \
    static struct test_##name##_registrar { \
        test_##name##_registrar() { registerTest(#name, test_##name); } \
    } test_##name##_instance; \
    static bool test_##name()

// Actual test implementations
#include "../include/physics/SpatialHash.hpp"
#include "../include/ecs/CoreTypes.hpp"
#include "../include/Constants.hpp"
#include <cmath>
#include <glm/glm.hpp>

using namespace DarkAges;

// ============================================================================
// SpatialHash Tests
// ============================================================================

TEST(SpatialHash_BasicInsertAndQuery) {
    SpatialHash hash(10.0f);  // 10m cells
    
    // Insert an entity
    hash.insert(static_cast<EntityID>(1), 5.0f, 5.0f);
    
    // Query near it
    auto results = hash.query(5.0f, 5.0f, 1.0f);
    
    if (results.size() != 1) {
        std::cerr << "FAIL: Expected 1 result, got " << results.size() << "\n";
        return false;
    }
    if (results[0] != static_cast<EntityID>(1)) {
        std::cerr << "FAIL: Expected entity 1, got " << static_cast<int>(results[0]) << "\n";
        return false;
    }
    
    return true;
}

TEST(SpatialHash_QueryEmptyReturnsEmpty) {
    SpatialHash hash(10.0f);
    
    auto results = hash.query(0.0f, 0.0f, 100.0f);
    
    if (results.size() != 0) {
        std::cerr << "FAIL: Expected 0 results for empty hash\n";
        return false;
    }
    
    return true;
}

TEST(SpatialHash_MultipleEntities) {
    SpatialHash hash(10.0f);
    
    // Insert multiple entities
    for (int i = 1; i <= 10; ++i) {
        hash.insert(static_cast<EntityID>(i), static_cast<float>(i), static_cast<float>(i));
    }
    
    // Query should find nearby entities
    auto results = hash.query(5.0f, 5.0f, 10.0f);
    
    // Should find entities 4, 5, 6 at least
    if (results.size() < 3) {
        std::cerr << "FAIL: Expected at least 3 results, got " << results.size() << "\n";
        return false;
    }
    
    return true;
}

TEST(SpatialHash_Remove) {
    SpatialHash hash(10.0f);
    
    hash.insert(static_cast<EntityID>(42), 5.0f, 5.0f);
    hash.remove(static_cast<EntityID>(42));
    
    auto results = hash.query(5.0f, 5.0f, 1.0f);
    
    if (results.size() != 0) {
        std::cerr << "FAIL: Expected entity to be removed\n";
        return false;
    }
    
    return true;
}

TEST(SpatialHash_UpdatePosition) {
    SpatialHash hash(10.0f);
    
    hash.insert(static_cast<EntityID>(1), 5.0f, 5.0f);
    hash.update(static_cast<EntityID>(1), 5.0f, 5.0f, 50.0f, 50.0f);  // Move far away
    
    // Should not find at old position
    auto oldResults = hash.query(5.0f, 5.0f, 1.0f);
    if (oldResults.size() != 0) {
        std::cerr << "FAIL: Entity should not be at old position\n";
        return false;
    }
    
    // Should find at new position
    auto newResults = hash.query(50.0f, 50.0f, 1.0f);
    if (newResults.size() != 1) {
        std::cerr << "FAIL: Entity should be at new position\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// CoreTypes Tests
// ============================================================================

TEST(Position_DistanceCalculation) {
    Position a{1000, 0, 0};  // 1.0, 0, 0
    Position b{4000, 0, 0};  // 4.0, 0, 0
    
    auto distSq = a.distanceSqTo(b);
    // Should be (3.0)^2 = 9.0
    // But we're in fixed-point, so need to account for precision
    
    float expectedDistSq = 9.0f;  // (4-1)^2
    float actualDistSq = distSq * Constants::FIXED_TO_FLOAT;
    
    if (std::abs(actualDistSq - expectedDistSq) > 0.1f) {
        std::cerr << "FAIL: Distance calculation incorrect. Expected ~" << expectedDistSq 
                  << ", got " << actualDistSq << "\n";
        return false;
    }
    
    return true;
}

TEST(Position_FromVec3) {
    glm::vec3 v(1.5f, 2.5f, 3.5f);
    Position p = Position::fromVec3(v);
    
    // Check conversion back
    glm::vec3 result = p.toVec3();
    
    float tolerance = 0.01f;
    if (std::abs(result.x - v.x) > tolerance ||
        std::abs(result.y - v.y) > tolerance ||
        std::abs(result.z - v.z) > tolerance) {
        std::cerr << "FAIL: Vec3 round-trip conversion failed\n";
        return false;
    }
    
    return true;
}

TEST(Constants_FixedPointPrecision) {
    // Test that fixed-point constants are consistent
    float original = 3.14159f;
    Constants::Fixed fixed = static_cast<Constants::Fixed>(original * Constants::FLOAT_TO_FIXED);
    float recovered = fixed * Constants::FIXED_TO_FLOAT;
    
    float error = std::abs(original - recovered);
    float maxError = 1.0f / Constants::FIXED_PRECISION;  // ~0.001
    
    if (error > maxError) {
        std::cerr << "FAIL: Fixed-point precision error too large: " << error << "\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "DarkAges Server - Unit Tests\n";
    std::cout << "========================================\n\n";
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : g_tests) {
        std::cout << "Running: " << test.name << " ... ";
        std::cout.flush();
        
        try {
            if (test.test()) {
                std::cout << "PASS\n";
                ++passed;
            } else {
                std::cout << "FAIL\n";
                ++failed;
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "UNKNOWN EXCEPTION\n";
            ++failed;
        }
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";
    
    return failed > 0 ? 1 : 0;
}
