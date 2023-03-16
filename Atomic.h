#pragma once
#include <atomic>

namespace Toastbox {

// Atomic: std::atomic with copy support
template<typename T>
struct Atomic : std::atomic<T> {
    Atomic() = default;
    constexpr Atomic(T x) : std::atomic<T>(x) {}
    constexpr Atomic(const Atomic<T>& x) : Atomic(x.load(std::memory_order_relaxed)) {}
    Atomic& operator=(const Atomic<T>& other) {
        this->store(other.load(std::memory_order_acquire), std::memory_order_release);
        return *this;
    }
};

} // namespace Toastbox
