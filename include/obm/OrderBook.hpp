#pragma once

#include "Order.hpp"
#include "PriceLevel.hpp"
#include "MemoryPool.hpp"
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace obm {

class OrderBook {
public:
    // Bids descending (highest first), asks ascending (lowest first)
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;

    // Callback type for fill notifications
    using FillCallback = std::function<void(const Fill&)>;

    explicit OrderBook(Symbol symbol, MemoryPool<Order>& pool);

    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&)                 noexcept;
    OrderBook& operator=(OrderBook&&)      noexcept;

    // ── Primary interface ─────────────────────────────────────────────────────
    // Submit a new order. Fires fill_cb_ for each generated fill.
    void add_order(const OrderEvent& ev);

    // Cancel by order ID. Returns true if found and cancelled.
    bool cancel_order(OrderId id);

    // ── Query ─────────────────────────────────────────────────────────────────
    [[nodiscard]] Price    best_bid()      const noexcept;
    [[nodiscard]] Price    best_ask()      const noexcept;
    [[nodiscard]] Quantity bid_qty_at(Price p) const noexcept;
    [[nodiscard]] Quantity ask_qty_at(Price p) const noexcept;
    [[nodiscard]] std::size_t bid_levels() const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_levels() const noexcept { return asks_.size(); }
    [[nodiscard]] std::size_t order_count()const noexcept { return order_map_.size(); }
    [[nodiscard]] Price    last_trade()    const noexcept { return last_trade_price_; }
    [[nodiscard]] bool     is_consistent() const noexcept;

    void set_fill_callback(FillCallback cb) { fill_cb_ = std::move(cb); }

    void print_top(int n, std::ostream& os) const;

private:
    Symbol             symbol_;
    MemoryPool<Order>& pool_;
    BidMap             bids_;
    AskMap             asks_;
    // O(1) cancel: OrderId → raw pointer into pool
    std::unordered_map<OrderId, Order*> order_map_;
    // Stop orders: price → orders waiting for trigger
    std::map<Price, std::vector<Order*>> stop_buy_;   // trigger when price rises >= stop_price
    std::map<Price, std::vector<Order*>> stop_sell_;  // trigger when price falls <= stop_price

    uint64_t     sequence_      = 0;
    Price        last_trade_price_ = INVALID_PRICE;
    FillCallback fill_cb_;

    // ── Internal matching ─────────────────────────────────────────────────────
    void match_limit(Order* aggressor);
    void match_market(Order* aggressor);
    Quantity available_qty(const Order* aggressor) const noexcept; // FOK pre-check
    void place_in_book(Order* o);
    void remove_from_book(Order* o);
    void execute_fill(Order* aggressor, Order* passive, Quantity qty);
    void trigger_stops();
    Order* make_order(const OrderEvent& ev);
};

} // namespace obm
