#include "obm/RiskEngine.hpp"

namespace obm {

RiskEngine::RiskEngine(MatchingEngine& engine, Limits limits)
    : engine_(engine), limits_(limits) {}

bool RiskEngine::submit(const OrderEvent& ev) noexcept {
    if (ev.type == OrderEvent::Type::CANCEL_ORDER) {
        // Cancels always pass risk; track them for cancel-to-trade ratio.
        ++stats_[ev.client_order_id].cancels;
        return engine_.submit(ev);
    }
    if (ev.type != OrderEvent::Type::NEW_ORDER &&
        ev.type != OrderEvent::Type::MODIFY_ORDER) {
        return engine_.submit(ev);
    }
    if (!check(ev)) return false;
    return engine_.submit(ev);
}

bool RiskEngine::check(const OrderEvent& ev) noexcept {
    // Max order qty
    if (ev.qty > limits_.max_order_qty) {
        reject(ev.order_id, "order qty exceeds max_order_qty");
        return false;
    }

    // Max notional (price * qty)
    if (limits_.max_order_notional > 0) {
        // Approximate: use limit price; market orders have price=0 (skip)
        if (ev.price > 0) {
            Price notional = ev.price * static_cast<Price>(ev.qty);
            if (notional > limits_.max_order_notional) {
                reject(ev.order_id, "order notional exceeds max_order_notional");
                return false;
            }
        }
    }

    // Net position limit
    if (limits_.max_net_position > 0) {
        auto& ts = stats_[ev.client_order_id];
        int64_t signed_qty = (ev.side == Side::BUY)
            ? static_cast<int64_t>(ev.qty)
            : -static_cast<int64_t>(ev.qty);
        int64_t projected = ts.net_position + signed_qty;
        if (projected >  static_cast<int64_t>(limits_.max_net_position) ||
            projected < -static_cast<int64_t>(limits_.max_net_position)) {
            reject(ev.order_id, "order would breach max_net_position");
            return false;
        }
        ts.net_position = projected;  // commit projected position on acceptance
    }

    // Cancel-to-trade ratio
    if (limits_.max_cancel_ratio > 0.0) {
        auto& ts = stats_[ev.client_order_id];
        if (ts.fills > 0) {
            double ratio = static_cast<double>(ts.cancels) /
                           static_cast<double>(ts.fills);
            if (ratio > limits_.max_cancel_ratio) {
                reject(ev.order_id, "cancel-to-trade ratio too high");
                return false;
            }
        }
    }

    return true;
}

void RiskEngine::on_fill(const Fill& f) {
    // Update position for both sides of the fill.
    auto& agg = stats_[f.aggressive_order_id];
    auto& pas = stats_[f.passive_order_id];
    // We don't know sides from Fill alone; use signed qty tracked in check().
    // Increment fill counts only (position already tracked at submit time).
    ++agg.fills;
    ++pas.fills;
}

void RiskEngine::reject(OrderId id, std::string_view reason) noexcept {
    if (reject_cb_) reject_cb_(id, reason);
}

const RiskEngine::TraderStats*
RiskEngine::trader_stats(OrderId trader_id) const noexcept {
    auto it = stats_.find(trader_id);
    return (it != stats_.end()) ? &it->second : nullptr;
}

} // namespace obm
