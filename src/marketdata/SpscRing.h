#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace marketdata {

template <typename T>
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity)
        : capacity_(capacity + 1), slots_(capacity_) {
        if (capacity < 2) {
            throw std::invalid_argument("SpscRing capacity must be at least 2");
        }
    }

    bool push(const T& value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        slots_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        value = slots_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

    std::size_t capacity() const {
        return capacity_ - 1;
    }

private:
    std::size_t increment(std::size_t value) const {
        return (value + 1) % capacity_;
    }

    const std::size_t capacity_;
    std::vector<T> slots_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace marketdata
