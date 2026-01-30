// [SECURITY_AGENT] Tests for DDoS Protection and Rate Limiting

#include <catch2/catch_test_macros.hpp>
#include "security/DDoSProtection.hpp"
#include "security/RateLimiter.hpp"
#include "ecs/CoreTypes.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace DarkAges;
using namespace DarkAges::Security;

TEST_CASE("DDoSProtection basic operations", "[security][ddos]") {
    DDoSProtection::Config config;
    config.maxConnectionsPerIP = 3;
    config.maxPacketsPerSecond = 10;
    config.blockDurationSeconds = 1;  // Short for testing
    config.violationThreshold = 2;
    
    DDoSProtection protection(config);
    REQUIRE(protection.initialize());
    
    SECTION("Connection acceptance within limits") {
        REQUIRE(protection.shouldAcceptConnection("192.168.1.1"));
        REQUIRE(protection.shouldAcceptConnection("192.168.1.1"));
        REQUIRE(protection.shouldAcceptConnection("192.168.1.1"));
        // Fourth connection from same IP should fail
        REQUIRE_FALSE(protection.shouldAcceptConnection("192.168.1.1"));
    }
    
    SECTION("Different IPs don't interfere") {
        REQUIRE(protection.shouldAcceptConnection("192.168.1.1"));
        REQUIRE(protection.shouldAcceptConnection("192.168.1.2"));
        REQUIRE(protection.shouldAcceptConnection("192.168.1.3"));
        // Each IP at 1 connection, should all accept more
        REQUIRE(protection.shouldAcceptConnection("192.168.1.1"));
        REQUIRE(protection.shouldAcceptConnection("192.168.1.2"));
        REQUIRE(protection.shouldAcceptConnection("192.168.1.3"));
    }
    
    SECTION("Emergency mode blocks connections") {
        REQUIRE(protection.shouldAcceptConnection("192.168.1.1"));
        protection.setEmergencyMode(true);
        REQUIRE_FALSE(protection.shouldAcceptConnection("192.168.1.2"));
        protection.setEmergencyMode(false);
        REQUIRE(protection.shouldAcceptConnection("192.168.1.2"));
    }
    
    SECTION("Whitelist bypasses limits") {
        protection.whitelistIP("192.168.1.100");
        REQUIRE(protection.isWhitelisted("192.168.1.100"));
        // Should accept even in emergency mode
        protection.setEmergencyMode(true);
        REQUIRE(protection.shouldAcceptConnection("192.168.1.100"));
    }
}

TEST_CASE("DDoSProtection packet rate limiting", "[security][ddos]") {
    DDoSProtection::Config config;
    config.maxPacketsPerSecond = 5;
    config.burstSize = 2;
    config.violationThreshold = 3;
    config.blockDurationSeconds = 1;
    
    DDoSProtection protection(config);
    protection.initialize();
    
    const std::string ip = "192.168.1.1";
    uint32_t currentTime = 0;
    
    SECTION("Packets within rate limit accepted") {
        // 5 packets in first second (within limit)
        for (uint32_t i = 0; i < 5; ++i) {
            REQUIRE(protection.processPacket(1, ip, 100, currentTime));
        }
    }
    
    SECTION("Burst packets accepted then limited") {
        // 7 packets (5 + 2 burst) should be accepted
        for (uint32_t i = 0; i < 7; ++i) {
            REQUIRE(protection.processPacket(1, ip, 100, currentTime));
        }
        // 8th packet should be dropped
        REQUIRE_FALSE(protection.processPacket(1, ip, 100, currentTime));
    }
    
    SECTION("Connection cleanup works") {
        protection.processPacket(1, ip, 100, currentTime);
        REQUIRE(protection.getActiveConnectionCount() == 1);
        
        protection.onConnectionClosed(1);
        REQUIRE(protection.getActiveConnectionCount() == 0);
    }
}

TEST_CASE("DDoSProtection IP blocking", "[security][ddos]") {
    DDoSProtection::Config config;
    config.blockDurationSeconds = 1;  // 1 second for testing
    
    DDoSProtection protection(config);
    protection.initialize();
    
    SECTION("Manual IP blocking") {
        const std::string ip = "192.168.1.1";
        REQUIRE_FALSE(protection.isIPBlocked(ip));
        
        protection.blockIP(ip, 1, "Test block");
        REQUIRE(protection.isIPBlocked(ip));
        REQUIRE_FALSE(protection.shouldAcceptConnection(ip));
    }
    
    SECTION("IP unblocking") {
        const std::string ip = "192.168.1.1";
        protection.blockIP(ip, 1, "Test block");
        REQUIRE(protection.isIPBlocked(ip));
        
        protection.unblockIP(ip);
        REQUIRE_FALSE(protection.isIPBlocked(ip));
        REQUIRE(protection.shouldAcceptConnection(ip));
    }
}

TEST_CASE("CircuitBreaker state machine", "[security][circuit]") {
    CircuitBreaker::Config config;
    config.failureThreshold = 3;
    config.successThreshold = 2;
    config.timeoutMs = 100;  // Short for testing
    
    CircuitBreaker breaker("test-service", config);
    
    SECTION("Initial state is CLOSED") {
        REQUIRE(breaker.getState() == CircuitBreaker::State::CLOSED);
        REQUIRE(breaker.allowRequest());
    }
    
    SECTION("Opens after threshold failures") {
        breaker.recordFailure();
        breaker.recordFailure();
        REQUIRE(breaker.getState() == CircuitBreaker::State::CLOSED);
        
        breaker.recordFailure();
        REQUIRE(breaker.getState() == CircuitBreaker::State::OPEN);
        REQUIRE_FALSE(breaker.allowRequest());
    }
    
    SECTION("Closes after threshold successes in half-open") {
        // Open the circuit
        for (int i = 0; i < 3; ++i) {
            breaker.recordFailure();
        }
        REQUIRE(breaker.getState() == CircuitBreaker::State::OPEN);
        
        // Wait for timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        REQUIRE(breaker.allowRequest());  // Should enter HALF_OPEN
        
        // Record successes
        breaker.recordSuccess();
        breaker.recordSuccess();
        REQUIRE(breaker.getState() == CircuitBreaker::State::CLOSED);
    }
    
    SECTION("Returns to OPEN on failure in HALF_OPEN") {
        // Open the circuit
        for (int i = 0; i < 3; ++i) {
            breaker.recordFailure();
        }
        
        // Wait for timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        REQUIRE(breaker.allowRequest());
        
        // Fail in half-open
        breaker.recordFailure();
        REQUIRE(breaker.getState() == CircuitBreaker::State::OPEN);
    }
}

TEST_CASE("InputValidator validation", "[security][validation]") {
    SECTION("Position validation") {
        // Valid position
        Position validPos;
        validPos.x = static_cast<Constants::Fixed>(0 * Constants::FLOAT_TO_FIXED);
        validPos.y = static_cast<Constants::Fixed>(10 * Constants::FLOAT_TO_FIXED);
        validPos.z = static_cast<Constants::Fixed>(0 * Constants::FLOAT_TO_FIXED);
        REQUIRE(InputValidator::isValidPosition(validPos));
        
        // Invalid position (outside world bounds)
        Position invalidPos;
        invalidPos.x = static_cast<Constants::Fixed>(10000 * Constants::FLOAT_TO_FIXED);
        invalidPos.y = 0;
        invalidPos.z = 0;
        REQUIRE_FALSE(InputValidator::isValidPosition(invalidPos));
    }
    
    SECTION("Rotation validation") {
        Rotation validRot;
        validRot.yaw = 1.0f;
        validRot.pitch = 0.5f;
        REQUIRE(InputValidator::isValidRotation(validRot));
        
        Rotation invalidRot;
        invalidRot.yaw = 10.0f;  // > 2π
        invalidRot.pitch = 0.0f;
        REQUIRE_FALSE(InputValidator::isValidRotation(invalidRot));
    }
    
    SECTION("Packet size validation") {
        REQUIRE(InputValidator::isValidPacketSize(100));
        REQUIRE(InputValidator::isValidPacketSize(1400));
        REQUIRE_FALSE(InputValidator::isValidPacketSize(0));
        REQUIRE_FALSE(InputValidator::isValidPacketSize(1500));
    }
    
    SECTION("Protocol version validation") {
        REQUIRE(InputValidator::isValidProtocolVersion(Constants::PROTOCOL_VERSION));
        REQUIRE_FALSE(InputValidator::isValidProtocolVersion(999));
    }
    
    SECTION("String sanitization") {
        std::string dirty = "Hello\x01\x02World\n!";
        std::string clean = InputValidator::sanitizeString(dirty, 256);
        REQUIRE(clean == "HelloWorld!");
        
        // Test max length
        std::string longStr(300, 'a');
        std::string truncated = InputValidator::sanitizeString(longStr, 10);
        REQUIRE(truncated.length() == 10);
    }
    
    SECTION("Input sequence validation") {
        REQUIRE(InputValidator::isValidInputSequence(10, 5));
        REQUIRE_FALSE(InputValidator::isValidInputSequence(5, 10));
        REQUIRE_FALSE(InputValidator::isValidInputSequence(5, 5));  // Duplicate
    }
}

TEST_CASE("TokenBucketRateLimiter basic operations", "[security][ratelimiter]") {
    TokenBucketRateLimiter<std::string>::Config config;
    config.maxTokens = 5;
    config.tokensPerSecond = 10;
    
    TokenBucketRateLimiter<std::string> limiter(config);
    const std::string key = "test-key";
    
    SECTION("Initial bucket is full") {
        REQUIRE(limiter.wouldAllow(key));
        REQUIRE(limiter.getRemainingTokens(key) == 5);
    }
    
    SECTION("Tokens are consumed") {
        for (int i = 0; i < 5; ++i) {
            REQUIRE(limiter.allow(key));
        }
        REQUIRE(limiter.getRemainingTokens(key) == 0);
        REQUIRE_FALSE(limiter.allow(key));
    }
    
    SECTION("Bucket refills over time") {
        // Consume all tokens
        for (int i = 0; i < 5; ++i) {
            limiter.allow(key);
        }
        REQUIRE_FALSE(limiter.allow(key));
        
        // Wait for refill (100ms at 10 tokens/sec = 1 token)
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        REQUIRE(limiter.allow(key));
    }
}

TEST_CASE("SlidingWindowRateLimiter basic operations", "[security][ratelimiter]") {
    SlidingWindowRateLimiter<std::string>::Config config;
    config.maxRequests = 3;
    config.windowMs = 100;  // Short for testing
    
    SlidingWindowRateLimiter<std::string> limiter(config);
    const std::string key = "test-key";
    
    SECTION("Requests within window allowed") {
        REQUIRE(limiter.allow(key));
        REQUIRE(limiter.allow(key));
        REQUIRE(limiter.allow(key));
        REQUIRE_FALSE(limiter.allow(key));
    }
    
    SECTION("Window slides over time") {
        REQUIRE(limiter.allow(key));
        REQUIRE(limiter.allow(key));
        REQUIRE(limiter.allow(key));
        REQUIRE_FALSE(limiter.allow(key));
        
        // Wait for window to slide
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        REQUIRE(limiter.allow(key));
    }
    
    SECTION("Remaining requests calculated correctly") {
        REQUIRE(limiter.getRemaining(key) == 3);
        limiter.allow(key);
        REQUIRE(limiter.getRemaining(key) == 2);
        limiter.allow(key);
        REQUIRE(limiter.getRemaining(key) == 1);
        limiter.allow(key);
        REQUIRE(limiter.getRemaining(key) == 0);
    }
}

TEST_CASE("NetworkRateLimiter integration", "[security][ratelimiter]") {
    NetworkRateLimiter limiter;
    const std::string ip = "192.168.1.1";
    
    SECTION("Connection rate limiting") {
        // Should allow burst of connections
        REQUIRE(limiter.allowConnection(ip));
        REQUIRE(limiter.allowConnection(ip));
        // Eventually should rate limit
        bool limited = false;
        for (int i = 0; i < 20; ++i) {
            if (!limiter.allowConnection(ip)) {
                limited = true;
                break;
            }
        }
        // With burst of 10 and rate of 2/sec, should eventually limit
        REQUIRE(limited);
    }
    
    SECTION("Packet rate limiting") {
        uint32_t connId = 1;
        // Should allow burst
        for (int i = 0; i < 100; ++i) {
            limiter.allowPacket(connId);
        }
        // Check remaining tokens
        REQUIRE(limiter.getRemainingPacketTokens(connId) < 120);
    }
    
    SECTION("Cleanup on disconnect") {
        uint32_t connId = 1;
        limiter.allowPacket(connId);
        limiter.allowMessage(connId);
        
        limiter.onConnectionClosed(connId);
        // After cleanup, should have fresh tokens (but this depends on implementation)
    }
}

TEST_CASE("DDoSProtection statistics", "[security][ddos]") {
    DDoSProtection::Config config;
    config.maxConnectionsPerIP = 5;
    config.maxPacketsPerSecond = 10;
    
    DDoSProtection protection(config);
    protection.initialize();
    
    SECTION("Connection count tracking") {
        REQUIRE(protection.getActiveConnectionCount() == 0);
        
        protection.processPacket(1, "192.168.1.1", 100, 0);
        REQUIRE(protection.getActiveConnectionCount() == 1);
        
        protection.processPacket(2, "192.168.1.2", 100, 0);
        REQUIRE(protection.getActiveConnectionCount() == 2);
        
        protection.onConnectionClosed(1);
        REQUIRE(protection.getActiveConnectionCount() == 1);
    }
    
    SECTION("Blocked IP count") {
        REQUIRE(protection.getBlockedIPCount() == 0);
        
        protection.blockIP("192.168.1.1", 60, "Test");
        protection.blockIP("192.168.1.2", 60, "Test");
        REQUIRE(protection.getBlockedIPCount() == 2);
        
        protection.unblockIP("192.168.1.1");
        REQUIRE(protection.getBlockedIPCount() == 1);
    }
}
