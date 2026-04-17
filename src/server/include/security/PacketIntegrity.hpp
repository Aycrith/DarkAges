#pragma once
// [SECURITY_AGENT] Packet integrity system for DarkAges MMO
// Implements sequence tracking, replay protection, and packet signing

#include <cstdint>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <string>
#include <vector>
#include <functional>

namespace DarkAges {
namespace Security {

// ============================================================================
// PacketIntegrity Types
// ============================================================================

// Packet hash type (FNV-1a 64-bit hash)
using PacketHash = std::uint64_t;

// Sequence number type
using SequenceNumber = std::uint32_t;

// Shared secret type for HMAC
using SharedSecret = std::string;

// ============================================================================
// Configuration Structures (defined before classes for C++20 compliance)
// ============================================================================

// [SECURITY_AGENT] Configuration for PacketSequenceTracker
struct SequenceTrackerConfig {
    // Maximum allowed out-of-order packets
    // Packets arriving more than this many ahead are rejected
    std::uint32_t tolerance{10};
    
    // Maximum gap before resync (prevents runaway tracking)
    std::uint32_t maxResyncGap{1000};
    
    SequenceTrackerConfig() = default;
};

// [SECURITY_AGENT] Configuration for ReplayProtection
struct ReplayProtectionConfig {
    // Time window for replay detection (milliseconds)
    // Packets with hashes seen within this window are rejected
    std::uint64_t windowDurationMs{5000};
    
    // Maximum entries in the window (memory bound)
    std::size_t maxWindowSize{10000};
    
    ReplayProtectionConfig() = default;
};

// [SECURITY_AGENT] Configuration for PacketSigner
struct PacketSignerConfig {
    // Shared secret for signing
    SharedSecret secret{"DarkAges-Default-Secret"};
    
    PacketSignerConfig() = default;
};

// ============================================================================
// PacketSequenceTracker
// ============================================================================

// [SECURITY_AGENT] Per-player packet sequence tracking
// Detects gaps, reordering, and replay attacks at the sequence level
class PacketSequenceTracker {
public:
    using Config = SequenceTrackerConfig;

    enum class ValidationResult {
        ACCEPTED,           // Packet accepted in order
        ACCEPTED_LATE,      // Packet accepted but late (within tolerance)
        REJECTED_DUPLICATE, // Duplicate sequence number
        REJECTED_TOO_OLD,   // Sequence too far behind
        REJECTED_GAP,       // Gap too large, requires resync
        REORDERED           // Accepted after reordering
    };

public:
    explicit PacketSequenceTracker(const Config& config = Config());
    
    // Validate and update sequence for a player connection
    // Returns validation result
    ValidationResult validateSequence(std::uint32_t connectionId,
                                      SequenceNumber sequence);
    
    // Get current expected sequence for connection
    SequenceNumber getExpectedSequence(std::uint32_t connectionId) const;
    
    // Get highest received sequence for connection
    SequenceNumber getHighestSequence(std::uint32_t connectionId) const;
    
    // Reset tracking for a connection (on reconnect)
    void reset(std::uint32_t connectionId);
    
    // Remove connection tracking (on disconnect)
    void removeConnection(std::uint32_t connectionId);
    
    // Get statistics for connection
    struct ConnectionStats {
        std::uint64_t packetsReceived{0};
        std::uint64_t packetsOutOfOrder{0};
        std::uint64_t packetsDropped{0};
        std::uint64_t gapResyncs{0};
    };
    ConnectionStats getStats(std::uint32_t connectionId) const;
    
    // Check if connection has sequence tracking
    bool hasConnection(std::uint32_t connectionId) const;

private:
    struct ConnectionState {
        SequenceNumber expected{0};
        SequenceNumber highest{0};
        std::unordered_set<SequenceNumber> pendingOutOfOrder;
        ConnectionStats stats;
    };
    
    Config config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, ConnectionState> connections_;
};

// ============================================================================
// ReplayProtection
// ============================================================================

// [SECURITY_AGENT] Sliding window replay protection
// Tracks seen packet hashes within a configurable time window
class ReplayProtection {
public:
    using Config = ReplayProtectionConfig;

    enum class ReplayStatus {
        UNIQUE,     // Packet hash not seen before
        DUPLICATE   // Packet hash already seen within window
    };

public:
    explicit ReplayProtection(const Config& config = Config());
    
    // Check if a packet hash has been seen (and record it if unique)
    // Returns UNIQUE if packet should be processed, DUPLICATE if replay
    ReplayStatus checkPacket(PacketHash hash);
    
    // Check without recording (for inspection)
    ReplayStatus peekPacket(PacketHash hash) const;
    
    // Record a hash without checking (for external validation)
    void recordHash(PacketHash hash);
    
    // Clear expired entries from the window
    // Should be called periodically
    void cleanupExpired();
    
    // Force clear all entries (for testing or reset)
    void clear();
    
    // Get current window statistics
    std::size_t getWindowSize() const;
    
    // Check if hash is currently in window
    bool contains(PacketHash hash) const;

private:
    struct HashEntry {
        PacketHash hash;
        std::uint64_t timestampMs;
    };
    
    Config config_;
    mutable std::mutex mutex_;
    std::deque<HashEntry> window_;  // Ordered by time
    std::unordered_set<PacketHash> hashSet_;  // Fast lookup
    
    std::uint64_t getCurrentTimeMs() const;
    void removeOldest();
};

// ============================================================================
// PacketSigner
// ============================================================================

// [SECURITY_AGENT] Packet signing using FNV-1a hash with HMAC-like construction
// Provides game-level integrity verification (not cryptographic security)
class PacketSigner {
public:
    using Config = PacketSignerConfig;
    using Signature = std::uint64_t;

public:
    explicit PacketSigner(const Config& config = Config());
    
    // Sign packet data
    // Returns signature that can be appended to packet
    Signature sign(const void* data, std::size_t length) const;
    
    // Sign with connection-specific context
    Signature signWithContext(const void* data, std::size_t length,
                              std::uint32_t connectionId,
                              SequenceNumber sequence) const;
    
    // Verify packet signature
    bool verify(const void* data, std::size_t length, Signature signature) const;
    
    // Verify with connection-specific context
    bool verifyWithContext(const void* data, std::size_t length,
                           Signature signature,
                           std::uint32_t connectionId,
                           SequenceNumber sequence) const;
    
    // Compute FNV-1a hash of data
    static PacketHash computeHash(const void* data, std::size_t length);
    
    // Update shared secret (for key rotation)
    void setSecret(const SharedSecret& secret);
    
    // Get current secret hash (for verification)
    std::uint64_t getSecretHash() const;

private:
    Config config_;
    mutable std::mutex mutex_;
    
    // FNV-1a constants
    static constexpr std::uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;
    static constexpr std::uint64_t FNV_PRIME = 0x100000001b3ULL;
    
    // Internal hash with seed
    static std::uint64_t fnv1aHash(const void* data, std::size_t length,
                                    std::uint64_t seed);
};

// ============================================================================
// PacketIntegrityManager
// ============================================================================

// [SECURITY_AGENT] Combined packet integrity manager
// Provides unified interface for all integrity checks
class PacketIntegrityManager {
public:
    struct Config {
        SequenceTrackerConfig sequenceConfig;
        ReplayProtectionConfig replayConfig;
        PacketSignerConfig signerConfig;
        
        Config() = default;
    };
    
    struct IntegrityResult {
        bool accepted{false};
        PacketSequenceTracker::ValidationResult sequenceResult;
        ReplayProtection::ReplayStatus replayStatus;
        bool signatureValid{false};
        
        // Convenience check
        bool isFullyValid() const {
            return accepted && 
                   sequenceResult == PacketSequenceTracker::ValidationResult::ACCEPTED &&
                   replayStatus == ReplayProtection::ReplayStatus::UNIQUE &&
                   signatureValid;
        }
    };

public:
    explicit PacketIntegrityManager(const Config& config = Config());
    
    // Full integrity check for a packet
    // Validates sequence, checks replay, verifies signature
    IntegrityResult validatePacket(std::uint32_t connectionId,
                                    SequenceNumber sequence,
                                    const void* data, std::size_t length,
                                    PacketSigner::Signature signature);
    
    // Sign a packet with full context
    PacketSigner::Signature signPacket(const void* data, std::size_t length,
                                        std::uint32_t connectionId,
                                        SequenceNumber sequence) const;
    
    // Access subsystems directly
    PacketSequenceTracker& getSequenceTracker() { return sequenceTracker_; }
    const PacketSequenceTracker& getSequenceTracker() const { return sequenceTracker_; }
    
    ReplayProtection& getReplayProtection() { return replayProtection_; }
    const ReplayProtection& getReplayProtection() const { return replayProtection_; }
    
    PacketSigner& getSigner() { return signer_; }
    const PacketSigner& getSigner() const { return signer_; }
    
    // Reset all state for a connection
    void resetConnection(std::uint32_t connectionId);
    
    // Remove connection from all tracking
    void removeConnection(std::uint32_t connectionId);
    
    // Periodic maintenance (call every N ticks)
    void update();

private:
    PacketSequenceTracker sequenceTracker_;
    ReplayProtection replayProtection_;
    PacketSigner signer_;
};

} // namespace Security
} // namespace DarkAges