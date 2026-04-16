#pragma once
#include <string>
#include <mutex>
#include <cstdint>

namespace DarkAges {
namespace Security {

// [SECURITY_AGENT] Circuit breaker for external services
class CircuitBreaker {
public:
    enum class State {
        CLOSED,      // Normal operation
        OPEN,        // Failing, rejecting requests
        HALF_OPEN    // Testing if service recovered
    };
    
    struct Config {
        uint32_t failureThreshold = 5;        // Open after 5 failures
        uint32_t successThreshold = 3;        // Close after 3 successes
        uint32_t timeoutMs = 30000;           // Try again after 30s
        uint32_t halfOpenMaxCalls = 3;        // Max calls in half-open
        
        Config() = default;
    };

public:
    explicit CircuitBreaker(const std::string& name, const Config& config);
    CircuitBreaker(const std::string& name);  // Default config
    
    // Call before attempting operation
    // Returns true if operation should proceed
    bool allowRequest();
    
    // Call on success
    void recordSuccess();
    
    // Call on failure
    void recordFailure();
    
    // Get current state
    State getState() const { return state_; }
    const char* getStateString() const;
    
    // Force state (for testing)
    void forceState(State state);

private:
    std::string name_;
    Config config_;
    
    State state_{State::CLOSED};
    uint32_t failureCount_{0};
    uint32_t successCount_{0};
    uint32_t halfOpenCalls_{0};
    uint64_t lastFailureTime_{0};
    
    mutable std::mutex mutex_;
};

} // namespace Security
} // namespace DarkAges
