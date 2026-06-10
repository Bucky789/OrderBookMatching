#pragma once

#include "Types.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace obm {

// Single-Producer Single-Consumer lock-free ring buffer.
//
// Cache-line padding separates head_ (written by producer) and tail_ (written
// by consumer) to prevent false sharing. Power-of-2 capacity enables bitmask
// instead of modulo for index wrapping.
//
// Memory ordering: acquire/release pairs on the cross-thread reads. On x86 TSO
// this compiles to no extra fence instructions — just compiler barriers.
template<typename T, std::size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>);

    static constexpr std::size_t MASK = Capacity - 1;

    alignas(CACHE_LINE) std::atomic<std::size_t> head_{0}; // producer writes
    alignas(CACHE_LINE) std::atomic<std::size_t> tail_{0}; // consumer writes
    alignas(CACHE_LINE) std::array<T, Capacity>  buffer_;

public:
    SPSCRingBuffer() = default;

    SPSCRingBuffer(const SPSCRingBuffer&)            = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    // Producer: returns false if full.
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const std::size_t cur_head = head_.load(std::memory_order_relaxed);
        const std::size_t next     = (cur_head + 1) & MASK;
        if (next == tail_.load(std::memory_order_acquire)) return false; // full
        buffer_[cur_head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: returns false if empty.
    [[nodiscard]] bool try_pop(T& item) noexcept {
        const std::size_t cur_tail = tail_.load(std::memory_order_relaxed);
        if (cur_tail == head_.load(std::memory_order_acquire)) return false; // empty
        item = buffer_[cur_tail];
        tail_.store((cur_tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool   empty()       const noexcept { return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire); }
    [[nodiscard]] bool   full()        const noexcept { return ((head_.load(std::memory_order_acquire) + 1) & MASK) == tail_.load(std::memory_order_acquire); }
    [[nodiscard]] std::size_t size_approx() const noexcept {
        return (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed)) & MASK;
    }
};

// 64K slots × 64 bytes = 4 MB — fits in L3 on most HFT machines
using OrderRingBuffer = SPSCRingBuffer<OrderEvent, 65536>;

} // namespace obm
