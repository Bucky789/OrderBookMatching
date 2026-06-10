#pragma once

#include "Order.hpp"
#include <cstdint>

namespace obm {

// Intrusive doubly-linked FIFO queue of Orders at one price point.
// All operations are O(1):
//   push_back  — new order arrives
//   pop_front  — oldest order fully filled
//   remove     — cancel any order via its intrusive prev/next pointers
// total_qty_ tracks sum of remaining_qty for fast depth-of-market queries.
class PriceLevel {
public:
    PriceLevel()  noexcept = default;
    ~PriceLevel() noexcept = default;

    PriceLevel(const PriceLevel&)            = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
    PriceLevel(PriceLevel&&)                 noexcept;
    PriceLevel& operator=(PriceLevel&&)      noexcept;

    // Add new order at back of queue (newest = lowest time priority)
    void push_back(Order* o) noexcept;

    // Peek at head (oldest = highest time priority). Undefined if empty.
    [[nodiscard]] Order* front() noexcept { return head_; }
    [[nodiscard]] const Order* front() const noexcept { return head_; }

    // Remove oldest order. Undefined if empty.
    void pop_front() noexcept;

    // Remove arbitrary order in O(1) using its intrusive pointers.
    void remove(Order* o) noexcept;

    [[nodiscard]] bool     empty()       const noexcept { return head_ == nullptr; }
    [[nodiscard]] Quantity total_qty()   const noexcept { return total_qty_; }
    [[nodiscard]] uint32_t order_count() const noexcept { return count_; }

    // Adjust total_qty after a partial fill on a passive order
    void reduce_qty(Quantity delta) noexcept { total_qty_ -= delta; }
    void add_qty(Quantity delta)    noexcept { total_qty_ += delta; }

private:
    Order*   head_      = nullptr;
    Order*   tail_      = nullptr;
    Quantity total_qty_ = 0;
    uint32_t count_     = 0;
};

} // namespace obm
