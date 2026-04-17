// [SECURITY_AGENT] Tests for Packet Integrity System
// Covers sequence tracking, replay protection, and packet signing

#include <catch2/catch_test_macros.hpp>
#include "security/PacketIntegrity.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <future>
#include <atomic>

using namespace DarkAges;
using namespace DarkAges::Security;

// ============================================================================
// PacketSequenceTracker Tests
// ============================================================================

TEST_CASE("PacketSequenceTracker basic operations", "[security][packet]") {
    PacketSequenceTracker::Config config;
    config.tolerance = 5;
    config.maxResyncGap = 100;
    
    PacketSequenceTracker tracker(config);
    const std::uint32_t connId = 1;
    
    SECTION("In-order packets accepted") {
        REQUIRE(tracker.validateSequence(connId, 0) == 
                PacketSequenceTracker::ValidationResult::ACCEPTED);
        REQUIRE(tracker.validateSequence(connId, 1) == 
                PacketSequenceTracker::ValidationResult::ACCEPTED);
        REQUIRE(tracker.validateSequence(connId, 2) == 
                PacketSequenceTracker::ValidationResult::ACCEPTED);
        
        REQUIRE(tracker.getExpectedSequence(connId) == 3);
        REQUIRE(tracker.getHighestSequence(connId) == 2);
    }
    
    SECTION("Duplicate packets rejected") {
        tracker.validateSequence(connId, 0);
        tracker.validateSequence(connId, 1);
        tracker.validateSequence(connId, 2);
        
        // Duplicate of already processed packet
        REQUIRE(tracker.validateSequence(connId, 1) == 
                PacketSequenceTracker::ValidationResult::REJECTED_DUPLICATE);
    }
    
    SECTION("Out-of-order within tolerance reordered") {
        tracker.validateSequence(connId, 0);
        tracker.validateSequence(connId, 1);
        // Skip to 4, then 2 and 3 arrive late
        REQUIRE(tracker.validateSequence(connId, 4) == 
                PacketSequenceTracker::ValidationResult::REORDERED);
        REQUIRE(tracker.validateSequence(connId, 3) == 
                PacketSequenceTracker::ValidationResult::REORDERED);
        // 2 arrives and is accepted, which triggers 3 and 4 to be processed
        REQUIRE(tracker.validateSequence(connId, 2) == 
                PacketSequenceTracker::ValidationResult::ACCEPTED);
        // 5 should be accepted since expected is now 5
        REQUIRE(tracker.validateSequence(connId, 5) == 
                PacketSequenceTracker::ValidationResult::ACCEPTED);
    }
    
    SECTION("Out-of-order beyond tolerance rejected") {
        tracker.validateSequence(connId, 0);
        // Jump way ahead
        REQUIRE(tracker.validateSequence(connId, 20) == 
                PacketSequenceTracker::ValidationResult::REJECTED_TOO_OLD);
        // Expected should not change
        REQUIRE(tracker.getExpectedSequence(connId) == 1);
    }
    
    SECTION("Large gap triggers resync") {
        tracker.validateSequence(connId, 0);
        // Jump past maxResyncGap
        REQUIRE(tracker.validateSequence(connId, 200) == 
                PacketSequenceTracker::ValidationResult::REJECTED_GAP);
        // Should now expect next sequence
        REQUIRE(tracker.getExpectedSequence(connId) == 201);
    }
}

TEST_CASE("PacketSequenceTracker connection management", "[security][packet]") {
    PacketSequenceTracker tracker;
    
    SECTION("New connection starts at sequence 0") {
        REQUIRE_FALSE(tracker.hasConnection(1));
        tracker.validateSequence(1, 0);
        REQUIRE(tracker.hasConnection(1));
        REQUIRE(tracker.getExpectedSequence(1) == 1);
    }
    
    SECTION("Reset clears sequence state") {
        tracker.validateSequence(1, 0);
        tracker.validateSequence(1, 1);
        tracker.validateSequence(1, 2);
        
        tracker.reset(1);
        REQUIRE_FALSE(tracker.hasConnection(1));
        
        // New sequence starts at 0 again
        tracker.validateSequence(1, 0);
        REQUIRE(tracker.getExpectedSequence(1) == 1);
    }
    
    SECTION("Remove connection clears all state") {
        tracker.validateSequence(1, 0);
        tracker.validateSequence(1, 1);
        
        tracker.removeConnection(1);
        REQUIRE_FALSE(tracker.hasConnection(1));
        
        // Stats should be empty
        auto stats = tracker.getStats(1);
        REQUIRE(stats.packetsReceived == 0);
    }
}

TEST_CASE("PacketSequenceTracker statistics", "[security][packet]") {
    PacketSequenceTracker::Config config;
    config.tolerance = 3;
    
    PacketSequenceTracker tracker(config);
    const std::uint32_t connId = 1;
    
    SECTION("Statistics track correctly") {
        // In order packets
        tracker.validateSequence(connId, 0);
        tracker.validateSequence(connId, 1);
        tracker.validateSequence(connId, 2);
        
        // Duplicate of already processed packet
        tracker.validateSequence(connId, 1);
        
        // Out of order (within tolerance) - arrives ahead
        tracker.validateSequence(connId, 5);
        
        // Too far ahead
        tracker.validateSequence(connId, 100);
        
        auto stats = tracker.getStats(connId);
        REQUIRE(stats.packetsReceived == 6);
        REQUIRE(stats.packetsDropped == 2);  // Duplicate + too far
        // 5 is pending, not yet out of order count
        REQUIRE(stats.packetsOutOfOrder == 0);
    }
}

// ============================================================================
// ReplayProtection Tests
// ============================================================================

TEST_CASE("ReplayProtection basic operations", "[security][packet]") {
    ReplayProtection::Config config;
    config.windowDurationMs = 100;  // Short for testing
    config.maxWindowSize = 100;
    
    ReplayProtection protection(config);
    
    SECTION("Unique packets accepted") {
        REQUIRE(protection.checkPacket(12345) == 
                ReplayProtection::ReplayStatus::UNIQUE);
        REQUIRE(protection.checkPacket(67890) == 
                ReplayProtection::ReplayStatus::UNIQUE);
        REQUIRE(protection.checkPacket(11111) == 
                ReplayProtection::ReplayStatus::UNIQUE);
    }
    
    SECTION("Duplicate packets rejected") {
        protection.checkPacket(12345);
        REQUIRE(protection.checkPacket(12345) == 
                ReplayProtection::ReplayStatus::DUPLICATE);
    }
    
    SECTION("Different hashes don't interfere") {
        protection.checkPacket(11111);
        protection.checkPacket(22222);
        protection.checkPacket(33333);
        
        REQUIRE(protection.checkPacket(11111) == 
                ReplayProtection::ReplayStatus::DUPLICATE);
        REQUIRE(protection.checkPacket(22222) == 
                ReplayProtection::ReplayStatus::DUPLICATE);
        REQUIRE(protection.checkPacket(33333) == 
                ReplayProtection::ReplayStatus::DUPLICATE);
    }
}

TEST_CASE("ReplayProtection window expiry", "[security][packet]") {
    ReplayProtection::Config config;
    config.windowDurationMs = 50;  // 50ms window
    config.maxWindowSize = 100;
    
    ReplayProtection protection(config);
    
    SECTION("Hash expires after window") {
        protection.checkPacket(12345);
        REQUIRE(protection.contains(12345));
        
        // Wait for expiry
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        protection.cleanupExpired();
        
        REQUIRE_FALSE(protection.contains(12345));
        // Should be accepted again
        REQUIRE(protection.checkPacket(12345) == 
                ReplayProtection::ReplayStatus::UNIQUE);
    }
    
    SECTION("Window size tracked correctly") {
        REQUIRE(protection.getWindowSize() == 0);
        protection.checkPacket(1);
        protection.checkPacket(2);
        protection.checkPacket(3);
        REQUIRE(protection.getWindowSize() == 3);
    }
}

TEST_CASE("ReplayProtection window size limit", "[security][packet]") {
    ReplayProtection::Config config;
    config.windowDurationMs = 10000;  // Long window
    config.maxWindowSize = 5;
    
    ReplayProtection protection(config);
    
    SECTION("Window evicts old entries when full") {
        for (int i = 0; i < 10; ++i) {
            protection.checkPacket(static_cast<PacketHash>(i));
        }
        
        // Should only have 5 entries
        REQUIRE(protection.getWindowSize() <= 5);
        
        // Oldest entries should be gone
        REQUIRE_FALSE(protection.contains(0));
        REQUIRE_FALSE(protection.contains(1));
        REQUIRE_FALSE(protection.contains(2));
        
        // Recent entries should still exist
        REQUIRE(protection.contains(8));
        REQUIRE(protection.contains(9));
    }
}

TEST_CASE("ReplayProtection peek vs check", "[security][packet]") {
    ReplayProtection protection;
    
    SECTION("Peek doesn't record") {
        REQUIRE(protection.peekPacket(12345) == 
                ReplayProtection::ReplayStatus::UNIQUE);
        REQUIRE_FALSE(protection.contains(12345));
        
        // Check records
        protection.checkPacket(12345);
        REQUIRE(protection.contains(12345));
        
        // Peek shows duplicate
        REQUIRE(protection.peekPacket(12345) == 
                ReplayProtection::ReplayStatus::DUPLICATE);
    }
}

TEST_CASE("ReplayProtection manual operations", "[security][packet]") {
    ReplayProtection protection;
    
    SECTION("Manual hash recording") {
        protection.recordHash(11111);
        protection.recordHash(22222);
        
        REQUIRE(protection.contains(11111));
        REQUIRE(protection.contains(22222));
        
        // Clear all
        protection.clear();
        REQUIRE(protection.getWindowSize() == 0);
        REQUIRE_FALSE(protection.contains(11111));
    }
}

// ============================================================================
// PacketSigner Tests
// ============================================================================

TEST_CASE("PacketSigner basic signing", "[security][packet]") {
    PacketSigner::Config config;
    config.secret = "TestSecret123";
    
    PacketSigner signer(config);
    
    SECTION("Sign and verify roundtrip") {
        const std::string data = "Hello, World!";
        auto signature = signer.sign(data.data(), data.size());
        
        REQUIRE(signer.verify(data.data(), data.size(), signature));
    }
    
    SECTION("Different data produces different signatures") {
        const std::string data1 = "Message 1";
        const std::string data2 = "Message 2";
        
        auto sig1 = signer.sign(data1.data(), data1.size());
        auto sig2 = signer.sign(data2.data(), data2.size());
        
        REQUIRE(sig1 != sig2);
    }
    
    SECTION("Same data produces same signature") {
        const std::string data = "Consistent data";
        
        auto sig1 = signer.sign(data.data(), data.size());
        auto sig2 = signer.sign(data.data(), data.size());
        
        REQUIRE(sig1 == sig2);
    }
}

TEST_CASE("PacketSigner tampered payload detection", "[security][packet]") {
    PacketSigner signer;
    
    SECTION("Modified data fails verification") {
        const std::string original = "Original message";
        auto signature = signer.sign(original.data(), original.size());
        
        std::string tampered = "Tampered message";
        REQUIRE_FALSE(signer.verify(tampered.data(), tampered.size(), signature));
    }
    
    SECTION("Wrong signature fails verification") {
        const std::string data = "Some data";
        auto wrongSig = signer.sign("wrong", 5);
        
        REQUIRE_FALSE(signer.verify(data.data(), data.size(), wrongSig));
    }
}

TEST_CASE("PacketSigner context-based signing", "[security][packet]") {
    PacketSigner signer;
    
    SECTION("Different contexts produce different signatures") {
        const std::string data = "Same data";
        
        auto sig1 = signer.signWithContext(data.data(), data.size(), 1, 100);
        auto sig2 = signer.signWithContext(data.data(), data.size(), 2, 100);
        auto sig3 = signer.signWithContext(data.data(), data.size(), 1, 101);
        
        REQUIRE(sig1 != sig2);  // Different connection
        REQUIRE(sig1 != sig3);  // Different sequence
    }
    
    SECTION("Context verification") {
        const std::string data = "Game packet";
        auto signature = signer.signWithContext(data.data(), data.size(), 42, 500);
        
        // Correct context verifies
        REQUIRE(signer.verifyWithContext(data.data(), data.size(), 
                                         signature, 42, 500));
        
        // Wrong connection fails
        REQUIRE_FALSE(signer.verifyWithContext(data.data(), data.size(),
                                               signature, 43, 500));
        
        // Wrong sequence fails
        REQUIRE_FALSE(signer.verifyWithContext(data.data(), data.size(),
                                               signature, 42, 501));
    }
}

TEST_CASE("PacketSigner secret management", "[security][packet]") {
    PacketSigner signer;
    
    SECTION("Secret rotation invalidates old signatures") {
        const std::string data = "Important packet";
        
        signer.setSecret("OldSecret");
        auto oldSig = signer.sign(data.data(), data.size());
        
        signer.setSecret("NewSecret");
        REQUIRE_FALSE(signer.verify(data.data(), data.size(), oldSig));
        
        // New signature works
        auto newSig = signer.sign(data.data(), data.size());
        REQUIRE(signer.verify(data.data(), data.size(), newSig));
    }
    
    SECTION("Secret hash changes on rotation") {
        auto hash1 = signer.getSecretHash();
        signer.setSecret("DifferentSecret");
        auto hash2 = signer.getSecretHash();
        
        REQUIRE(hash1 != hash2);
    }
}

TEST_CASE("PacketSigner static hash", "[security][packet]") {
    SECTION("FNV-1a hash consistency") {
        const std::string data = "Test data for hashing";
        
        auto hash1 = PacketSigner::computeHash(data.data(), data.size());
        auto hash2 = PacketSigner::computeHash(data.data(), data.size());
        
        REQUIRE(hash1 == hash2);
    }
    
    SECTION("Different data produces different hashes") {
        auto hash1 = PacketSigner::computeHash("abc", 3);
        auto hash2 = PacketSigner::computeHash("abd", 3);
        
        REQUIRE(hash1 != hash2);
    }
}

// ============================================================================
// PacketIntegrityManager Integration Tests
// ============================================================================

TEST_CASE("PacketIntegrityManager full validation", "[security][packet]") {
    PacketIntegrityManager::Config config;
    config.sequenceConfig.tolerance = 5;
    config.replayConfig.windowDurationMs = 1000;
    config.signerConfig.secret = "GameServerSecret";
    
    PacketIntegrityManager manager(config);
    const std::uint32_t connId = 1;
    
    SECTION("Valid packet accepted") {
        const std::string data = "Game state update";
        SequenceNumber seq = 0;
        
        auto signature = manager.signPacket(data.data(), data.size(), 
                                            connId, seq);
        
        auto result = manager.validatePacket(connId, seq, 
                                             data.data(), data.size(),
                                             signature);
        
        REQUIRE(result.accepted);
        REQUIRE(result.isFullyValid());
        REQUIRE(result.sequenceResult == 
                PacketSequenceTracker::ValidationResult::ACCEPTED);
        REQUIRE(result.replayStatus == 
                ReplayProtection::ReplayStatus::UNIQUE);
        REQUIRE(result.signatureValid);
    }
    
    SECTION("Replay attack detected") {
        const std::string data = "Replay attack packet";
        SequenceNumber seq = 0;
        
        auto signature = manager.signPacket(data.data(), data.size(),
                                            connId, seq);
        
        // First time should work
        auto result1 = manager.validatePacket(connId, seq,
                                              data.data(), data.size(),
                                              signature);
        REQUIRE(result1.accepted);
        
        // Replay should fail (same hash)
        auto result2 = manager.validatePacket(connId, 1,
                                              data.data(), data.size(),
                                              signature);
        REQUIRE_FALSE(result2.accepted);
        REQUIRE(result2.replayStatus == 
                ReplayProtection::ReplayStatus::DUPLICATE);
    }
    
    SECTION("Tampered payload detected") {
        const std::string original = "Legitimate data";
        SequenceNumber seq = 0;
        
        auto signature = manager.signPacket(original.data(), original.size(),
                                            connId, seq);
        
        std::string tampered = "Tampered data!";
        auto result = manager.validatePacket(connId, seq,
                                             tampered.data(), tampered.size(),
                                             signature);
        
        REQUIRE_FALSE(result.accepted);
        REQUIRE_FALSE(result.signatureValid);
    }
    
    SECTION("Sequence violation detected") {
        const std::string data = "Out of order packet";
        
        // Start with sequence 0
        auto sig0 = manager.signPacket(data.data(), data.size(), connId, 0);
        manager.validatePacket(connId, 0, data.data(), data.size(), sig0);
        
        // Try to skip too far ahead
        auto sig100 = manager.signPacket(data.data(), data.size(), connId, 100);
        auto result = manager.validatePacket(connId, 100,
                                             data.data(), data.size(),
                                             sig100);
        // Sequence should be rejected if beyond tolerance
        REQUIRE(result.sequenceResult == 
                PacketSequenceTracker::ValidationResult::REJECTED_TOO_OLD);
    }
}

TEST_CASE("PacketIntegrityManager connection management", "[security][packet]") {
    PacketIntegrityManager manager;
    
    SECTION("Reset clears connection state") {
        const std::string data = "Test data";
        const std::uint32_t connId = 1;
        
        auto sig = manager.signPacket(data.data(), data.size(), connId, 0);
        manager.validatePacket(connId, 0, data.data(), data.size(), sig);
        
        manager.resetConnection(connId);
        
        // Should start fresh
        REQUIRE(manager.getSequenceTracker().getExpectedSequence(connId) == 0);
    }
    
    SECTION("Remove connection") {
        manager.getSequenceTracker().validateSequence(1, 0);
        manager.getSequenceTracker().validateSequence(2, 0);
        
        manager.removeConnection(1);
        
        REQUIRE_FALSE(manager.getSequenceTracker().hasConnection(1));
        REQUIRE(manager.getSequenceTracker().hasConnection(2));
    }
}

TEST_CASE("PacketIntegrityManager update cleanup", "[security][packet]") {
    PacketIntegrityManager::Config config;
    config.replayConfig.windowDurationMs = 50;
    
    PacketIntegrityManager manager(config);
    
    SECTION("Update cleans expired entries") {
        const std::string data = "Expire me";
        auto hash = PacketSigner::computeHash(data.data(), data.size());
        
        manager.getReplayProtection().checkPacket(hash);
        REQUIRE(manager.getReplayProtection().getWindowSize() == 1);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        manager.update();
        
        REQUIRE(manager.getReplayProtection().getWindowSize() == 0);
    }
}

// ============================================================================
// Concurrent Access Tests
// ============================================================================

TEST_CASE("PacketSequenceTracker thread safety", "[security][packet][concurrent]") {
    PacketSequenceTracker tracker;
    const int numThreads = 4;
    const int packetsPerThread = 100;
    std::atomic<int> acceptedCount{0};
    
    std::vector<std::future<void>> futures;
    
    for (int t = 0; t < numThreads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            for (int i = 0; i < packetsPerThread; ++i) {
                auto result = tracker.validateSequence(
                    static_cast<std::uint32_t>(t),
                    static_cast<SequenceNumber>(i)
                );
                if (result == PacketSequenceTracker::ValidationResult::ACCEPTED ||
                    result == PacketSequenceTracker::ValidationResult::ACCEPTED_LATE) {
                    acceptedCount.fetch_add(1);
                }
            }
        }));
    }
    
    for (auto& f : futures) {
        f.wait();
    }
    
    // Each thread should have all packets accepted (different connections)
    REQUIRE(acceptedCount.load() == numThreads * packetsPerThread);
}

TEST_CASE("ReplayProtection thread safety", "[security][packet][concurrent]") {
    ReplayProtection protection;
    const int numThreads = 4;
    const int checksPerThread = 100;
    std::atomic<int> uniqueCount{0};
    
    std::vector<std::future<void>> futures;
    
    for (int t = 0; t < numThreads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            for (int i = 0; i < checksPerThread; ++i) {
                // Each thread uses unique hashes
                PacketHash hash = static_cast<PacketHash>(t * 10000 + i);
                if (protection.checkPacket(hash) == 
                    ReplayProtection::ReplayStatus::UNIQUE) {
                    uniqueCount.fetch_add(1);
                }
            }
        }));
    }
    
    for (auto& f : futures) {
        f.wait();
    }
    
    // All should be unique since hashes are different
    REQUIRE(uniqueCount.load() == numThreads * checksPerThread);
}

TEST_CASE("PacketSigner thread safety", "[security][packet][concurrent]") {
    PacketSigner signer;
    const int numThreads = 4;
    const int signPerThread = 100;
    std::atomic<int> verifiedCount{0};
    
    std::vector<std::future<void>> futures;
    
    for (int t = 0; t < numThreads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            for (int i = 0; i < signPerThread; ++i) {
                std::string data = "Thread " + std::to_string(t) + 
                                   " packet " + std::to_string(i);
                auto sig = signer.sign(data.data(), data.size());
                if (signer.verify(data.data(), data.size(), sig)) {
                    verifiedCount.fetch_add(1);
                }
            }
        }));
    }
    
    for (auto& f : futures) {
        f.wait();
    }
    
    REQUIRE(verifiedCount.load() == numThreads * signPerThread);
}

TEST_CASE("PacketIntegrityManager concurrent validation", 
          "[security][packet][concurrent]") {
    PacketIntegrityManager manager;
    const int numThreads = 4;
    const int packetsPerThread = 50;
    std::atomic<int> acceptedCount{0};
    
    std::vector<std::future<void>> futures;
    
    for (int t = 0; t < numThreads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            auto connId = static_cast<std::uint32_t>(t);
            for (int i = 0; i < packetsPerThread; ++i) {
                // Include thread ID in data to make each packet unique
                std::string data = "Thread " + std::to_string(t) + 
                                   " packet " + std::to_string(i);
                auto seq = static_cast<SequenceNumber>(i);
                
                auto sig = manager.signPacket(data.data(), data.size(),
                                              connId, seq);
                auto result = manager.validatePacket(connId, seq,
                                                     data.data(), data.size(),
                                                     sig);
                if (result.accepted) {
                    acceptedCount.fetch_add(1);
                }
            }
        }));
    }
    
    for (auto& f : futures) {
        f.wait();
    }
    
    // Each connection should have all packets accepted
    REQUIRE(acceptedCount.load() == numThreads * packetsPerThread);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("PacketIntegrity edge cases", "[security][packet]") {
    SECTION("Empty data handling") {
        PacketSigner signer;
        auto sig = signer.sign(nullptr, 0);
        REQUIRE(signer.verify(nullptr, 0, sig));
    }
    
    SECTION("Maximum sequence number") {
        PacketSequenceTracker::Config config;
        config.tolerance = 5;
        config.maxResyncGap = 100;
        
        PacketSequenceTracker tracker(config);
        const std::uint32_t connId = 1;
        
        // Start at a high sequence (would trigger gap resync)
        SequenceNumber highSeq = 0xFFFFFF00;
        REQUIRE(tracker.validateSequence(connId, highSeq) == 
                PacketSequenceTracker::ValidationResult::REJECTED_GAP);
        // After resync, should expect next sequence
        REQUIRE(tracker.getExpectedSequence(connId) == highSeq + 1);
        
        // Continue with normal sequence
        REQUIRE(tracker.validateSequence(connId, highSeq + 1) == 
                PacketSequenceTracker::ValidationResult::ACCEPTED);
    }
    
    SECTION("Zero hash value") {
        ReplayProtection protection;
        REQUIRE(protection.checkPacket(0) == 
                ReplayProtection::ReplayStatus::UNIQUE);
        REQUIRE(protection.checkPacket(0) == 
                ReplayProtection::ReplayStatus::DUPLICATE);
    }
}