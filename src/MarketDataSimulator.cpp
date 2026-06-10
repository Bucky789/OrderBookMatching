#include "obm/MarketDataSimulator.hpp"
#include <algorithm>
#include <cmath>

namespace obm {

MarketDataSimulator::MarketDataSimulator(SimConfig cfg)
    : cfg_(cfg),
      rng_(cfg.seed),
      drift_(0.0, cfg.volatility),
      mid_price_(cfg.initial_mid) {}

Price MarketDataSimulator::snap_to_tick(double p) const noexcept {
    double ticks = std::round(p / cfg_.tick_size);
    return double_to_price(ticks * cfg_.tick_size);
}

OrderEvent MarketDataSimulator::next_event() {
    // Step mid-price (geometric Brownian motion, discrete)
    mid_price_ *= std::exp(drift_(rng_));
    if (mid_price_ < cfg_.tick_size) mid_price_ = cfg_.tick_size;

    double r = uniform_(rng_);
    if (r < cfg_.cancel_prob &&
        (!live_buy_ids_.empty() || !live_sell_ids_.empty())) {
        return make_cancel();
    }
    return make_new_order();
}

OrderEvent MarketDataSimulator::make_new_order() {
    OrderEvent ev{};
    ev.type      = OrderEvent::Type::NEW_ORDER;
    ev.symbol    = cfg_.symbol;
    ev.order_id  = ++seq_ + 1'000'000;

    double r = uniform_(rng_);
    ev.side = (r < 0.5) ? Side::BUY : Side::SELL;

    double spread = cfg_.spread_ticks * cfg_.tick_size;

    if (uniform_(rng_) < cfg_.market_order_prob) {
        ev.order_type = OrderType::MARKET;
        ev.price      = 0;
    } else if (uniform_(rng_) < cfg_.iceberg_prob) {
        ev.order_type = OrderType::ICEBERG;
        double base = (ev.side == Side::BUY)
                      ? mid_price_ - spread * 0.5 - uniform_(rng_) * spread * 2
                      : mid_price_ + spread * 0.5 + uniform_(rng_) * spread * 2;
        ev.price = snap_to_tick(base);
        Quantity total = cfg_.min_qty + static_cast<Quantity>(
            uniform_(rng_) * (cfg_.max_qty - cfg_.min_qty));
        ev.qty         = total;
        ev.visible_qty = std::max(Quantity(1), total / 5); // 20% visible
    } else {
        ev.order_type = OrderType::LIMIT;
        ev.tif        = TimeInForce::GTC;
        double base = (ev.side == Side::BUY)
                      ? mid_price_ - spread * 0.5 - uniform_(rng_) * spread * 3
                      : mid_price_ + spread * 0.5 + uniform_(rng_) * spread * 3;
        ev.price = snap_to_tick(base);
    }

    if (ev.qty == 0) {
        ev.qty = cfg_.min_qty + static_cast<Quantity>(
            uniform_(rng_) * (cfg_.max_qty - cfg_.min_qty));
    }

    if (ev.side == Side::BUY) live_buy_ids_.push_back(ev.order_id);
    else                      live_sell_ids_.push_back(ev.order_id);

    return ev;
}

OrderEvent MarketDataSimulator::make_cancel() {
    OrderEvent ev{};
    ev.type   = OrderEvent::Type::CANCEL_ORDER;
    ev.symbol = cfg_.symbol;

    auto& ids = (!live_buy_ids_.empty() && (live_sell_ids_.empty() || uniform_(rng_) < 0.5))
                ? live_buy_ids_ : live_sell_ids_;

    std::size_t idx = static_cast<std::size_t>(uniform_(rng_) * ids.size()) % ids.size();
    ev.order_id = ids[idx];
    ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(idx));
    ++seq_;
    return ev;
}

void MarketDataSimulator::generate_batch(std::vector<OrderEvent>& out, std::size_t n) {
    out.reserve(out.size() + n);
    for (std::size_t i = 0; i < n; ++i) out.push_back(next_event());
}

} // namespace obm
