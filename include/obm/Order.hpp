#pragma once

#include "Types.hpp"

namespace obm {

// Order is 128 bytes (2 cache lines). The intrusive prev/next pointers allow
// O(1) removal from any PriceLevel without scanning the FIFO queue.
// pool_next is reused by MemoryPool when the order is not in the book.
struct alignas(CACHE_LINE) Order {
    // ── Identity ──────────────────────────────────────────────────────────────
    OrderId   id;
    OrderId   client_order_id;
    Symbol    symbol;
    uint32_t  _pad0;

    // ── Classification ────────────────────────────────────────────────────────
    Side        side;
    OrderType   type;
    OrderState  state;
    TimeInForce tif;
    uint32_t    _pad1;

    // ── Pricing ───────────────────────────────────────────────────────────────
    Price    limit_price;
    Price    stop_price;

    // ── Quantities ────────────────────────────────────────────────────────────
    Quantity original_qty;
    Quantity remaining_qty;
    Quantity filled_qty;
    Quantity visible_qty;   // ICEBERG: currently exposed peak
    Quantity reserve_qty;   // ICEBERG: hidden portion

    // ── Time priority (lower sequence = higher priority at same price) ─────────
    uint64_t timestamp_ns;
    uint64_t sequence_num;

    // ── Intrusive doubly-linked list (within PriceLevel FIFO) ────────────────
    Order* prev;
    Order* next;

    // ── Pool freelist link (used by MemoryPool when order not in book) ────────
    Order* pool_next;

    // ── Helpers ───────────────────────────────────────────────────────────────
    [[nodiscard]] bool is_buy()    const noexcept { return side == Side::BUY; }
    [[nodiscard]] bool is_sell()   const noexcept { return side == Side::SELL; }
    [[nodiscard]] bool is_active() const noexcept {
        return state == OrderState::NEW || state == OrderState::PARTIALLY_FILLED;
    }
    // For ICEBERG, only the visible peak participates in matching at any time.
    [[nodiscard]] Quantity executable_qty() const noexcept {
        return (type == OrderType::ICEBERG) ? visible_qty : remaining_qty;
    }
};

// 128 bytes = 2 cache lines. Pool stride keeps sequential orders on adjacent lines.
static_assert(sizeof(Order) == 128, "Order must be 128 bytes");

} // namespace obm
