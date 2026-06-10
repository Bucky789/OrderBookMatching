#pragma once

#include <cstdint>
#include <limits>

namespace obm {

// ── Fundamental types ────────────────────────────────────────────────────────
// Price: fixed-point integer, 8 decimal places. "150.25" → 15025000000.
// Never use double in matching logic — floating-point equality is undefined.
using Price    = int64_t;
using Quantity = uint64_t;
using OrderId  = uint64_t;
using Symbol   = uint32_t;

static constexpr Price   INVALID_PRICE = std::numeric_limits<Price>::min();
static constexpr int     CACHE_LINE    = 64;
static constexpr int64_t PRICE_SCALE   = 100'000'000LL; // 10^8

inline Price  double_to_price(double d)  noexcept { return static_cast<Price>(d * PRICE_SCALE + 0.5); }
inline double price_to_double(Price p)   noexcept { return static_cast<double>(p) / static_cast<double>(PRICE_SCALE); }

// ── Enumerations ─────────────────────────────────────────────────────────────
enum class Side        : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType   : uint8_t { LIMIT=0, MARKET=1, IOC=2, FOK=3, ICEBERG=4, STOP=5 };
enum class OrderState  : uint8_t { NEW=0, PARTIALLY_FILLED=1, FILLED=2, CANCELLED=3, REJECTED=4 };
enum class TimeInForce : uint8_t { DAY=0, GTC=1, IOC=2, FOK=3 };

// ── Fill ──────────────────────────────────────────────────────────────────────
struct Fill {
    OrderId  aggressive_order_id;
    OrderId  passive_order_id;
    Symbol   symbol;
    uint32_t _pad;
    Price    fill_price;
    Quantity fill_qty;
    uint64_t timestamp_ns;
};

// ── OrderEvent ────────────────────────────────────────────────────────────────
// Exactly one cache line (64 bytes) so SPSC ring buffer stores events
// contiguously without false sharing between producer and consumer.
struct alignas(CACHE_LINE) OrderEvent {
    enum class Type : uint8_t { NEW_ORDER=0, CANCEL_ORDER=1, MODIFY_ORDER=2, SHUTDOWN=3 };

    // 8 × 8-byte fields = 64 bytes
    OrderId   order_id;          // 8
    OrderId   client_order_id;   // 8
    Price     price;             // 8
    Price     stop_price;        // 8  (STOP orders only)
    Quantity  qty;               // 8
    Quantity  visible_qty;       // 8  (ICEBERG: peak shown; reserve = qty - visible_qty)
    uint64_t  timestamp_ns;      // 8
    // last 8 bytes: 4 + 4×1
    Symbol    symbol;            // 4
    Type      type;              // 1
    OrderType order_type;        // 1
    Side      side;              // 1
    TimeInForce tif;             // 1
};
static_assert(sizeof(OrderEvent) == CACHE_LINE, "OrderEvent must be exactly one cache line");

} // namespace obm
