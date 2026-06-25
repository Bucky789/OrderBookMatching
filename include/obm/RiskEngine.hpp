#pragma once

#include "MatchingEngine.hpp"
#include <string>
#include <string_view>
#include <unordered_map>

namespace obm {

// Pre-trade risk wrapper around MatchingEngine.
// All submit() calls are gated by configurable limits before being forwarded.
// Operates on the gateway thread — zero latency impact on the matching core.
class RiskEngine {
public:
    struct Limits {
        Quantity max_order_qty      = 10'000;       // per single order
        Price    max_order_notional = 0;             // 0 = unlimited (in Price units)
        Quantity max_net_position   = 100'000;       // per symbol per trader (buy - sell)
        double   max_cancel_ratio   = 10.0;          // cancels / fills; 0 = unlimited
    };

    struct TraderStats {
        int64_t  net_position = 0;   // signed: positive = long, negative = short
        uint64_t fills        = 0;
        uint64_t cancels      = 0;
    };

    using RejectCallback = std::function<void(OrderId, std::string_view)>;

    explicit RiskEngine(MatchingEngine& engine, Limits limits = {});

    void set_reject_callback(RejectCallback cb) { reject_cb_ = std::move(cb); }

    // Submit with pre-trade checks. Returns false if ring full OR risk rejected.
    [[nodiscard]] bool submit(const OrderEvent& ev) noexcept;

    // Notify risk engine of a fill so position tracking stays accurate.
    void on_fill(const Fill& f);

    // Query per-trader stats.
    [[nodiscard]] const TraderStats* trader_stats(OrderId trader_id) const noexcept;

private:
    MatchingEngine& engine_;
    Limits          limits_;
    RejectCallback  reject_cb_;

    // Keyed by client_order_id (used as trader ID in this model)
    std::unordered_map<OrderId, TraderStats> stats_;

    bool check(const OrderEvent& ev) noexcept;
    void reject(OrderId id, std::string_view reason) noexcept;
};

} // namespace obm
