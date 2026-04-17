#include "security/CircuitBreaker.hpp"
#include <chrono>
#include <iostream>

namespace DarkAges {
namespace Security {

CircuitBreaker::CircuitBreaker(const std::string& name, const Config& config)
    : name_(name), config_(config) {}

CircuitBreaker::CircuitBreaker(const std::string& name)
    : name_(name), config_(Config{}) {}

bool CircuitBreaker::allowRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (state_) {
        case State::CLOSED:
            return true;
            
        case State::OPEN: {
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            if (now - lastFailureTime_ > config_.timeoutMs) {
                // Try half-open
                state_ = State::HALF_OPEN;
                halfOpenCalls_ = 0;
                successCount_ = 0;
                std::cout << "[CIRCUIT] " << name_ << " entering HALF_OPEN\n";
                return true;
            }
            return false;
        }
        
        case State::HALF_OPEN:
            if (halfOpenCalls_ < config_.halfOpenMaxCalls) {
                ++halfOpenCalls_;
                return true;
            }
            return false;
    }
    
    return false;
}

void CircuitBreaker::recordSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (state_) {
        case State::CLOSED:
            failureCount_ = 0;
            break;
            
        case State::HALF_OPEN:
            ++successCount_;
            if (successCount_ >= config_.successThreshold) {
                state_ = State::CLOSED;
                failureCount_ = 0;
                std::cout << "[CIRCUIT] " << name_ << " CLOSED (recovered)\n";
            }
            break;
            
        case State::OPEN:
            break;
    }
}

void CircuitBreaker::recordFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    lastFailureTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    switch (state_) {
        case State::CLOSED:
            ++failureCount_;
            if (failureCount_ >= config_.failureThreshold) {
                state_ = State::OPEN;
                std::cerr << "[CIRCUIT] " << name_ << " OPEN (too many failures)\n";
            }
            break;
            
        case State::HALF_OPEN:
            state_ = State::OPEN;
            std::cerr << "[CIRCUIT] " << name_ << " OPEN (half-open failed)\n";
            break;
            
        case State::OPEN:
            break;
    }
}

const char* CircuitBreaker::getStateString() const {
    switch (state_) {
        case State::CLOSED: return "CLOSED";
        case State::OPEN: return "OPEN";
        case State::HALF_OPEN: return "HALF_OPEN";
    }
    return "UNKNOWN";
}

void CircuitBreaker::forceState(State state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    failureCount_ = 0;
    successCount_ = 0;
    halfOpenCalls_ = 0;
    // Reset lastFailureTime so that OPEN state immediately rejects requests
    // (unless timeout has passed, but this makes the transition cleaner)
    lastFailureTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace Security
} // namespace DarkAges
