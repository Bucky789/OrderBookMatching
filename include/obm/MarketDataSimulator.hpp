#pragma once

#include "Types.hpp"
#include <cstdint>
#include <random>
#include <vector>

namespace obm {

struct SimConfig {
    Symbol   symbol           = 1;
    double   initial_mid      = 100.0;   // starting mid-price
    double   tick_size        = 0.01;    // minimum price increment
    double   volatility       = 0.0005;  // per-event mid drift std-dev
    double   cancel_prob      = 0.20;    // fraction of events that are cancels
    double   market_order_prob = 0.05;   // fraction that are market orders
    double   iceberg_prob     = 0.02;    // fraction that are icebergs
    int      spread_ticks     = 2;       // typical bid-ask spread in ticks
    Quantity min_qty          = 1;
    Quantity max_qty          = 500;
    uint32_t seed             = 42;
};

class MarketDataSimulator {
public:
    explicit MarketDataSimulator(SimConfig cfg = {});

    // Generate next OrderEvent
    OrderEvent next_event();

    // Generate n events into a vector
    void generate_batch(std::vector<OrderEvent>& out, std::size_t n);

    [[nodiscard]] double   current_mid()   const noexcept { return mid_price_; }
    [[nodiscard]] uint64_t event_count()   const noexcept { return seq_; }

private:
    SimConfig              cfg_;
    std::mt19937_64        rng_;
    std::normal_distribution<double>       drift_;
    std::uniform_real_distribution<double> uniform_{0.0, 1.0};

    double   mid_price_;
    uint64_t seq_ = 0;
    std::vector<OrderId> live_buy_ids_;
    std::vector<OrderId> live_sell_ids_;

    OrderEvent make_new_order();
    OrderEvent make_cancel();
    Price snap_to_tick(double p) const noexcept;
};

} // namespace obm
