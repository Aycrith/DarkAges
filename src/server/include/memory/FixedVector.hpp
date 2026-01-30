#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace DarkAges {
namespace Memory {

// [PERFORMANCE_AGENT] Vector with fixed maximum capacity
// No heap allocations, stack-friendly
template<typename T, size_t MaxCapacity>
class FixedVector {
public:
    using iterator = T*;
    using const_iterator = const T*;
    
    FixedVector() = default;
    ~FixedVector() = default;
    
    // No copy (expensive)
    FixedVector(const FixedVector&) = delete;
    FixedVector& operator=(const FixedVector&) = delete;
    
    // Move allowed
    FixedVector(FixedVector&&) = default;
    FixedVector& operator=(FixedVector&&) = default;
    
    // Element access
    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }
    
    T& front() { return data_[0]; }
    const T& front() const { return data_[0]; }
    
    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }
    
    T* data() { return data_.data(); }
    const T* data() const { return data_.data(); }
    
    // Iterators
    iterator begin() { return data_.data(); }
    iterator end() { return data_.data() + size_; }
    const_iterator begin() const { return data_.data(); }
    const_iterator end() const { return data_.data() + size_; }
    
    // Capacity
    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    size_t capacity() const { return MaxCapacity; }
    
    // Modifiers
    void clear() { size_ = 0; }
    
    void push_back(const T& value) {
        if (size_ >= MaxCapacity) {
            throw std::runtime_error("FixedVector capacity exceeded");
        }
        data_[size_++] = value;
    }
    
    void push_back(T&& value) {
        if (size_ >= MaxCapacity) {
            throw std::runtime_error("FixedVector capacity exceeded");
        }
        data_[size_++] = std::move(value);
    }
    
    template<typename... Args>
    T& emplace_back(Args&&... args) {
        if (size_ >= MaxCapacity) {
            throw std::runtime_error("FixedVector capacity exceeded");
        }
        data_[size_] = T(std::forward<Args>(args)...);
        return data_[size_++];
    }
    
    void pop_back() {
        if (size_ > 0) --size_;
    }
    
    void resize(size_t newSize) {
        if (newSize > MaxCapacity) {
            throw std::runtime_error("FixedVector capacity exceeded");
        }
        size_ = newSize;
    }
    
    // Find element
    iterator find(const T& value) {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i] == value) {
                return &data_[i];
            }
        }
        return end();
    }
    
    // Erase element at position
    iterator erase(iterator pos) {
        if (pos < begin() || pos >= end()) {
            return end();
        }
        
        // Move all elements after pos one position back
        for (iterator it = pos; it + 1 < end(); ++it) {
            *it = std::move(*(it + 1));
        }
        --size_;
        return pos;
    }
    
    // Erase element by value
    bool erase_value(const T& value) {
        iterator it = find(value);
        if (it != end()) {
            erase(it);
            return true;
        }
        return false;
    }

private:
    alignas(alignof(T)) std::array<T, MaxCapacity> data_;
    size_t size_{0};
};

} // namespace Memory
} // namespace DarkAges
