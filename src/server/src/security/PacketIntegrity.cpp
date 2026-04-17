// [SECURITY_AGENT] Packet integrity system implementation
// Sequence tracking, replay protection, and packet signing

#include "security/PacketIntegrity.hpp"
#include <algorithm>
#include <cstring>

namespace DarkAges {
namespace Security {

// ============================================================================
// PacketSequenceTracker Implementation
// ============================================================================

PacketSequenceTracker::PacketSequenceTracker(const Config& config)
    : config_(config) {}

PacketSequenceTracker::ValidationResult 
PacketSequenceTracker::validateSequence(std::uint32_t connectionId,
                                         SequenceNumber sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& state = connections_[connectionId];
    state.stats.packetsReceived++;
    
    // Check for duplicate
    if (sequence < state.expected) {
        // Check if this is in pending out-of-order
        if (state.pendingOutOfOrder.count(sequence) > 0) {
            // Remove from pending since we're processing it
            state.pendingOutOfOrder.erase(sequence);
            state.stats.packetsOutOfOrder++;
            return ValidationResult::ACCEPTED_LATE;
        }
        state.stats.packetsDropped++;
        return ValidationResult::REJECTED_DUPLICATE;
    }
    
    // Exact match - in order
    if (sequence == state.expected) {
        state.expected = sequence + 1;
        
        // Check if any pending out-of-order packets can now be processed
        while (state.pendingOutOfOrder.count(state.expected) > 0) {
            state.pendingOutOfOrder.erase(state.expected);
            state.expected++;
        }
        
        if (sequence > state.highest) {
            state.highest = sequence;
        }
        return ValidationResult::ACCEPTED;
    }
    
    // Sequence is ahead of expected
    std::uint32_t gap = sequence - state.expected;
    
    // Check if gap is too large
    if (gap > config_.maxResyncGap) {
        // Resync to this sequence
        state.expected = sequence + 1;
        state.highest = sequence;
        state.pendingOutOfOrder.clear();
        state.stats.gapResyncs++;
        return ValidationResult::REJECTED_GAP;
    }
    
    // Check if within tolerance for out-of-order
    if (gap <= config_.tolerance) {
        // Store for later processing
        state.pendingOutOfOrder.insert(sequence);
        if (sequence > state.highest) {
            state.highest = sequence;
        }
        return ValidationResult::REORDERED;
    }
    
    // Too far ahead
    state.stats.packetsDropped++;
    return ValidationResult::REJECTED_TOO_OLD;
}

SequenceNumber PacketSequenceTracker::getExpectedSequence(
    std::uint32_t connectionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) {
        return 0;
    }
    return it->second.expected;
}

SequenceNumber PacketSequenceTracker::getHighestSequence(
    std::uint32_t connectionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) {
        return 0;
    }
    return it->second.highest;
}

void PacketSequenceTracker::reset(std::uint32_t connectionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(connectionId);
}

void PacketSequenceTracker::removeConnection(std::uint32_t connectionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(connectionId);
}

PacketSequenceTracker::ConnectionStats 
PacketSequenceTracker::getStats(std::uint32_t connectionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) {
        return ConnectionStats{};
    }
    return it->second.stats;
}

bool PacketSequenceTracker::hasConnection(std::uint32_t connectionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.count(connectionId) > 0;
}

// ============================================================================
// ReplayProtection Implementation
// ============================================================================

ReplayProtection::ReplayProtection(const Config& config)
    : config_(config) {}

ReplayProtection::ReplayStatus ReplayProtection::checkPacket(PacketHash hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clean up expired entries
    std::uint64_t now = getCurrentTimeMs();
    std::uint64_t cutoff = now - config_.windowDurationMs;
    
    while (!window_.empty() && window_.front().timestampMs < cutoff) {
        hashSet_.erase(window_.front().hash);
        window_.pop_front();
    }
    
    // Check for duplicate
    if (hashSet_.count(hash) > 0) {
        return ReplayStatus::DUPLICATE;
    }
    
    // Ensure we don't exceed max window size
    while (hashSet_.size() >= config_.maxWindowSize && !window_.empty()) {
        hashSet_.erase(window_.front().hash);
        window_.pop_front();
    }
    
    // Record this hash
    window_.push_back({hash, now});
    hashSet_.insert(hash);
    
    return ReplayStatus::UNIQUE;
}

ReplayProtection::ReplayStatus ReplayProtection::peekPacket(PacketHash hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (hashSet_.count(hash) > 0) {
        return ReplayStatus::DUPLICATE;
    }
    return ReplayStatus::UNIQUE;
}

void ReplayProtection::recordHash(PacketHash hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::uint64_t now = getCurrentTimeMs();
    
    // Ensure we don't exceed max window size
    while (hashSet_.size() >= config_.maxWindowSize && !window_.empty()) {
        hashSet_.erase(window_.front().hash);
        window_.pop_front();
    }
    
    if (hashSet_.count(hash) == 0) {
        window_.push_back({hash, now});
        hashSet_.insert(hash);
    }
}

void ReplayProtection::cleanupExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::uint64_t now = getCurrentTimeMs();
    std::uint64_t cutoff = now - config_.windowDurationMs;
    
    while (!window_.empty() && window_.front().timestampMs < cutoff) {
        hashSet_.erase(window_.front().hash);
        window_.pop_front();
    }
}

void ReplayProtection::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    window_.clear();
    hashSet_.clear();
}

std::size_t ReplayProtection::getWindowSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hashSet_.size();
}

bool ReplayProtection::contains(PacketHash hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hashSet_.count(hash) > 0;
}

std::uint64_t ReplayProtection::getCurrentTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void ReplayProtection::removeOldest() {
    if (!window_.empty()) {
        hashSet_.erase(window_.front().hash);
        window_.pop_front();
    }
}

// ============================================================================
// PacketSigner Implementation
// ============================================================================

PacketSigner::PacketSigner(const Config& config)
    : config_(config) {}

PacketSigner::Signature PacketSigner::sign(const void* data, 
                                            std::size_t length) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Hash the secret first, then combine with data
    std::uint64_t secretHash = fnv1aHash(
        config_.secret.data(), 
        config_.secret.size(),
        FNV_OFFSET_BASIS
    );
    
    return fnv1aHash(data, length, secretHash);
}

PacketSigner::Signature PacketSigner::signWithContext(
    const void* data, std::size_t length,
    std::uint32_t connectionId, SequenceNumber sequence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Build context data
    struct ContextData {
        std::uint32_t connectionId;
        SequenceNumber sequence;
    } context{connectionId, sequence};
    
    // Hash context with secret as seed
    std::uint64_t seed = fnv1aHash(
        config_.secret.data(),
        config_.secret.size(),
        FNV_OFFSET_BASIS
    );
    seed = fnv1aHash(&context, sizeof(context), seed);
    
    return fnv1aHash(data, length, seed);
}

bool PacketSigner::verify(const void* data, std::size_t length,
                           Signature signature) const {
    Signature computed = sign(data, length);
    return computed == signature;
}

bool PacketSigner::verifyWithContext(const void* data, std::size_t length,
                                      Signature signature,
                                      std::uint32_t connectionId,
                                      SequenceNumber sequence) const {
    Signature computed = signWithContext(data, length, connectionId, sequence);
    return computed == signature;
}

PacketHash PacketSigner::computeHash(const void* data, std::size_t length) {
    return fnv1aHash(data, length, FNV_OFFSET_BASIS);
}

void PacketSigner::setSecret(const SharedSecret& secret) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.secret = secret;
}

std::uint64_t PacketSigner::getSecretHash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fnv1aHash(
        config_.secret.data(),
        config_.secret.size(),
        FNV_OFFSET_BASIS
    );
}

std::uint64_t PacketSigner::fnv1aHash(const void* data, std::size_t length,
                                        std::uint64_t seed) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    
    for (std::size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    
    return hash;
}

// ============================================================================
// PacketIntegrityManager Implementation
// ============================================================================

PacketIntegrityManager::PacketIntegrityManager(const Config& config)
    : sequenceTracker_(config.sequenceConfig)
    , replayProtection_(config.replayConfig)
    , signer_(config.signerConfig) {}

PacketIntegrityManager::IntegrityResult 
PacketIntegrityManager::validatePacket(std::uint32_t connectionId,
                                        SequenceNumber sequence,
                                        const void* data, std::size_t length,
                                        PacketSigner::Signature signature) {
    IntegrityResult result;
    
    // Step 1: Validate sequence
    result.sequenceResult = sequenceTracker_.validateSequence(connectionId, sequence);
    
    // Step 2: Compute packet hash and check replay
    PacketHash hash = PacketSigner::computeHash(data, length);
    result.replayStatus = replayProtection_.checkPacket(hash);
    
    // Step 3: Verify signature
    result.signatureValid = signer_.verifyWithContext(
        data, length, signature, connectionId, sequence
    );
    
    // Determine if packet should be accepted
    // Accept if sequence is valid (or late), not a replay, and signature matches
    result.accepted = (
        (result.sequenceResult == PacketSequenceTracker::ValidationResult::ACCEPTED ||
         result.sequenceResult == PacketSequenceTracker::ValidationResult::ACCEPTED_LATE ||
         result.sequenceResult == PacketSequenceTracker::ValidationResult::REORDERED) &&
        result.replayStatus == ReplayProtection::ReplayStatus::UNIQUE &&
        result.signatureValid
    );
    
    return result;
}

PacketSigner::Signature 
PacketIntegrityManager::signPacket(const void* data, std::size_t length,
                                    std::uint32_t connectionId,
                                    SequenceNumber sequence) const {
    return signer_.signWithContext(data, length, connectionId, sequence);
}

void PacketIntegrityManager::resetConnection(std::uint32_t connectionId) {
    sequenceTracker_.reset(connectionId);
}

void PacketIntegrityManager::removeConnection(std::uint32_t connectionId) {
    sequenceTracker_.removeConnection(connectionId);
}

void PacketIntegrityManager::update() {
    replayProtection_.cleanupExpired();
}

} // namespace Security
} // namespace DarkAges