#include "obm/OrderBook.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <utility>

namespace obm {

namespace {
uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}
} // anonymous namespace

// ── Construction / move ───────────────────────────────────────────────────────
OrderBook::OrderBook(Symbol symbol, MemoryPool<Order>& pool)
    : symbol_(symbol), pool_(pool) {}

OrderBook::OrderBook(OrderBook&& o) noexcept
    : symbol_(o.symbol_), pool_(o.pool_),
      bids_(std::move(o.bids_)), asks_(std::move(o.asks_)),
      order_map_(std::move(o.order_map_)),
      stop_buy_(std::move(o.stop_buy_)), stop_sell_(std::move(o.stop_sell_)),
      sequence_(o.sequence_), last_trade_price_(o.last_trade_price_),
      fill_cb_(std::move(o.fill_cb_)) {}

OrderBook& OrderBook::operator=(OrderBook&& o) noexcept {
    if (this != &o) {
        bids_ = std::move(o.bids_); asks_ = std::move(o.asks_);
        order_map_ = std::move(o.order_map_);
        stop_buy_ = std::move(o.stop_buy_); stop_sell_ = std::move(o.stop_sell_);
        sequence_ = o.sequence_; last_trade_price_ = o.last_trade_price_;
        fill_cb_ = std::move(o.fill_cb_);
    }
    return *this;
}

// ── make_order ────────────────────────────────────────────────────────────────
Order* OrderBook::make_order(const OrderEvent& ev) {
    Order* o      = pool_.allocate();
    o->id         = ev.order_id;
    o->client_order_id = ev.client_order_id;
    o->symbol     = ev.symbol;
    o->side       = ev.side;
    o->type       = ev.order_type;
    o->tif        = ev.tif;
    o->state      = OrderState::NEW;
    o->limit_price  = ev.price;
    o->stop_price   = ev.stop_price;
    o->original_qty = ev.qty;
    o->remaining_qty = ev.qty;
    o->filled_qty   = 0;
    o->timestamp_ns  = (ev.timestamp_ns != 0) ? ev.timestamp_ns : now_ns();
    o->sequence_num  = ++sequence_;
    o->prev = o->next = o->pool_next = nullptr;

    if (ev.order_type == OrderType::ICEBERG) {
        o->visible_qty = std::min(ev.visible_qty > 0 ? ev.visible_qty : ev.qty / 10, ev.qty);
        o->reserve_qty = ev.qty - o->visible_qty;
        // ICEBERG and STOP are mutually exclusive — reuse stop_price to store
        // the original peak size so we can reload to the same peak on each refresh.
        o->stop_price  = static_cast<Price>(o->visible_qty);
    } else {
        o->visible_qty = ev.qty;
        o->reserve_qty = 0;
    }
    return o;
}

// ── add_order ─────────────────────────────────────────────────────────────────
void OrderBook::add_order(const OrderEvent& ev) {
    Order* o = make_order(ev);

    switch (o->type) {
    case OrderType::STOP: {
        // Register trigger — do not match now.
        // Prepend to intrusive list via pool_next (safe: allocated orders
        // don't use pool_next for the freelist until they are deallocated).
        auto& stop_map = (o->side == Side::BUY) ? stop_buy_ : stop_sell_;
        Order*& head = stop_map[o->stop_price];
        o->pool_next = head;
        head = o;
        order_map_[o->id] = o;
        return;
    }

    case OrderType::FOK: {
        // Pre-check: if we can't fill the full qty, reject without touching book
        Quantity avail = available_qty(o);
        if (avail < o->original_qty) {
            o->state = OrderState::REJECTED;
            if (reject_cb_) reject_cb_(o->id, "FOK: insufficient liquidity");
            pool_.deallocate(o);
            return;
        }
        match_limit(o); // guaranteed to fill fully
        pool_.deallocate(o);
        return;
    }

    case OrderType::MARKET:
        match_market(o);
        if (o->remaining_qty > 0) o->state = OrderState::CANCELLED;
        pool_.deallocate(o);
        return;

    case OrderType::IOC:
        match_limit(o);
        // Cancel whatever didn't fill — never rests in book
        if (o->remaining_qty > 0) o->state = OrderState::CANCELLED;
        pool_.deallocate(o);
        return;

    case OrderType::LIMIT:
    case OrderType::ICEBERG:
        match_limit(o);
        if (o->remaining_qty > 0) {
            place_in_book(o);
        } else {
            pool_.deallocate(o);
        }
        return;
    }
}

// ── match_limit ───────────────────────────────────────────────────────────────
void OrderBook::match_limit(Order* agg) {
    while (agg->remaining_qty > 0) {
        if (agg->is_buy()) {
            if (asks_.empty()) break;
            auto it = asks_.begin();
            if (agg->limit_price < it->first) break;  // no cross
            PriceLevel& level = it->second;
            while (agg->remaining_qty > 0 && !level.empty()) {
                Order* pas = level.front();
                Quantity qty = std::min(agg->remaining_qty, pas->executable_qty());
                execute_fill(agg, pas, qty);
                if (pas->type == OrderType::ICEBERG && pas->visible_qty == 0) {
                    if (pas->reserve_qty > 0) {
                        // Reload iceberg: new sequence = goes to back of queue.
                        // Peak size is stored in stop_price (ICEBERG and STOP are
                        // mutually exclusive, so stop_price is free for this use).
                        level.pop_front();
                        Quantity peak   = static_cast<Quantity>(pas->stop_price);
                        Quantity reload = std::min(peak, pas->reserve_qty);
                        pas->reserve_qty  -= reload;
                        pas->visible_qty   = reload;
                        pas->remaining_qty = reload + pas->reserve_qty;
                        pas->sequence_num  = ++sequence_;
                        pas->prev = pas->next = nullptr;
                        level.push_back(pas);
                    } else {
                        level.pop_front();
                        order_map_.erase(pas->id);
                        pool_.deallocate(pas);
                    }
                } else if (pas->remaining_qty == 0) {
                    level.pop_front();
                    order_map_.erase(pas->id);
                    pool_.deallocate(pas);
                }
            }
            if (level.empty()) asks_.erase(it);
        } else {
            // SELL aggressor vs BID side
            if (bids_.empty()) break;
            auto it = bids_.begin(); // highest bid
            if (agg->limit_price > it->first) break;
            PriceLevel& level = it->second;
            while (agg->remaining_qty > 0 && !level.empty()) {
                Order* pas = level.front();
                Quantity qty = std::min(agg->remaining_qty, pas->executable_qty());
                execute_fill(agg, pas, qty);
                if (pas->type == OrderType::ICEBERG && pas->visible_qty == 0) {
                    if (pas->reserve_qty > 0) {
                        level.pop_front();
                        Quantity peak   = static_cast<Quantity>(pas->stop_price);
                        Quantity reload = std::min(peak, pas->reserve_qty);
                        pas->reserve_qty  -= reload;
                        pas->visible_qty   = reload;
                        pas->remaining_qty = reload + pas->reserve_qty;
                        pas->sequence_num  = ++sequence_;
                        pas->prev = pas->next = nullptr;
                        level.push_back(pas);
                    } else {
                        level.pop_front();
                        order_map_.erase(pas->id);
                        pool_.deallocate(pas);
                    }
                } else if (pas->remaining_qty == 0) {
                    level.pop_front();
                    order_map_.erase(pas->id);
                    pool_.deallocate(pas);
                }
            }
            if (level.empty()) bids_.erase(it);
        }
    }
}

// ── match_market ──────────────────────────────────────────────────────────────
void OrderBook::match_market(Order* agg) {
    // Market orders have no price limit — take whatever is available
    if (agg->is_buy()) {
        while (agg->remaining_qty > 0 && !asks_.empty()) {
            auto it = asks_.begin();
            PriceLevel& level = it->second;
            while (agg->remaining_qty > 0 && !level.empty()) {
                Order* pas = level.front();
                Quantity qty = std::min(agg->remaining_qty, pas->executable_qty());
                execute_fill(agg, pas, qty);
                if (pas->type == OrderType::ICEBERG && pas->visible_qty == 0) {
                    if (pas->reserve_qty > 0) {
                        level.pop_front();
                        Quantity peak   = static_cast<Quantity>(pas->stop_price);
                        Quantity reload = std::min(peak, pas->reserve_qty);
                        pas->reserve_qty  -= reload;
                        pas->visible_qty   = reload;
                        pas->remaining_qty = reload + pas->reserve_qty;
                        pas->sequence_num  = ++sequence_;
                        pas->prev = pas->next = nullptr;
                        level.push_back(pas);
                    } else {
                        level.pop_front();
                        order_map_.erase(pas->id);
                        pool_.deallocate(pas);
                    }
                } else if (pas->remaining_qty == 0) {
                    level.pop_front();
                    order_map_.erase(pas->id);
                    pool_.deallocate(pas);
                }
            }
            if (level.empty()) asks_.erase(it);
        }
    } else {
        while (agg->remaining_qty > 0 && !bids_.empty()) {
            auto it = bids_.begin();
            PriceLevel& level = it->second;
            while (agg->remaining_qty > 0 && !level.empty()) {
                Order* pas = level.front();
                Quantity qty = std::min(agg->remaining_qty, pas->executable_qty());
                execute_fill(agg, pas, qty);
                if (pas->type == OrderType::ICEBERG && pas->visible_qty == 0) {
                    if (pas->reserve_qty > 0) {
                        level.pop_front();
                        Quantity peak   = static_cast<Quantity>(pas->stop_price);
                        Quantity reload = std::min(peak, pas->reserve_qty);
                        pas->reserve_qty  -= reload;
                        pas->visible_qty   = reload;
                        pas->remaining_qty = reload + pas->reserve_qty;
                        pas->sequence_num  = ++sequence_;
                        pas->prev = pas->next = nullptr;
                        level.push_back(pas);
                    } else {
                        level.pop_front();
                        order_map_.erase(pas->id);
                        pool_.deallocate(pas);
                    }
                } else if (pas->remaining_qty == 0) {
                    level.pop_front();
                    order_map_.erase(pas->id);
                    pool_.deallocate(pas);
                }
            }
            if (level.empty()) bids_.erase(it);
        }
    }
}

// ── available_qty (FOK pre-check) ─────────────────────────────────────────────
Quantity OrderBook::available_qty(const Order* agg) const noexcept {
    Quantity total = 0;
    if (agg->is_buy()) {
        for (auto& [price, level] : asks_) {
            if (agg->limit_price < price) break;
            total += level.total_qty();
            if (total >= agg->original_qty) return total;
        }
    } else {
        for (auto& [price, level] : bids_) {
            if (agg->limit_price > price) break;
            total += level.total_qty();
            if (total >= agg->original_qty) return total;
        }
    }
    return total;
}

// ── execute_fill ──────────────────────────────────────────────────────────────
void OrderBook::execute_fill(Order* agg, Order* pas, Quantity qty) {
    assert(qty > 0);
    Price fill_price = pas->limit_price;

    agg->remaining_qty -= qty;
    agg->filled_qty    += qty;
    agg->state = (agg->remaining_qty == 0) ? OrderState::FILLED : OrderState::PARTIALLY_FILLED;

    if (pas->type == OrderType::ICEBERG) {
        pas->visible_qty   -= qty;
        // remaining_qty tracks total (visible + reserve)
        pas->remaining_qty -= qty;
        pas->filled_qty    += qty;
    } else {
        pas->remaining_qty -= qty;
        pas->filled_qty    += qty;
    }
    if (pas->remaining_qty == 0) pas->state = OrderState::FILLED;
    else                         pas->state = OrderState::PARTIALLY_FILLED;

    // Update level's total qty tracker
    if (agg->is_buy()) {
        auto it = asks_.find(fill_price);
        if (it != asks_.end()) { it->second.reduce_qty(qty); }
    } else {
        auto it = bids_.find(fill_price);
        if (it != bids_.end()) { it->second.reduce_qty(qty); }
    }

    last_trade_price_ = fill_price;

    if (fill_cb_) {
        Fill f;
        f.aggressive_order_id = agg->id;
        f.passive_order_id    = pas->id;
        f.symbol              = symbol_;
        f.fill_price          = fill_price;
        f.fill_qty            = qty;
        f.timestamp_ns        = now_ns();
        fill_cb_(f);
    }

    trigger_stops();
}

// ── trigger_stops ─────────────────────────────────────────────────────────────
void OrderBook::trigger_stops() {
    if (last_trade_price_ == INVALID_PRICE) return;

    auto drain_list = [&](Order* head) {
        while (head) {
            Order* nxt = head->pool_next;
            head->pool_next = nullptr;
            head->type = OrderType::MARKET;
            match_market(head);
            if (head->remaining_qty > 0) head->state = OrderState::CANCELLED;
            order_map_.erase(head->id);
            pool_.deallocate(head);
            head = nxt;
        }
    };

    // Buy stops: trigger when market price reaches or exceeds stop_price
    for (auto it = stop_buy_.begin(); it != stop_buy_.end(); ) {
        if (last_trade_price_ >= it->first) {
            Order* head = it->second;
            it = stop_buy_.erase(it);
            drain_list(head);
        } else {
            ++it;
        }
    }

    // Sell stops: trigger when market price falls to or below stop_price
    for (auto it = stop_sell_.begin(); it != stop_sell_.end(); ) {
        if (last_trade_price_ <= it->first) {
            Order* head = it->second;
            it = stop_sell_.erase(it);
            drain_list(head);
        } else {
            ++it;
        }
    }
}

// ── cancel_order ──────────────────────────────────────────────────────────────
bool OrderBook::cancel_order(OrderId id) {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;
    Order* o = it->second;
    order_map_.erase(it);

    // Check stop lists first (intrusive singly-linked list via pool_next)
    auto try_erase_stop = [&](auto& stop_map) -> bool {
        auto sit = stop_map.find(o->stop_price);
        if (sit == stop_map.end()) return false;
        Order** cur = &sit->second;
        while (*cur && *cur != o) cur = &(*cur)->pool_next;
        if (!*cur) return false;
        *cur = o->pool_next;
        if (sit->second == nullptr) stop_map.erase(sit);
        return true;
    };

    if (try_erase_stop(stop_buy_) || try_erase_stop(stop_sell_)) {
        o->state = OrderState::CANCELLED;
        pool_.deallocate(o);
        return true;
    }

    remove_from_book(o);
    o->state = OrderState::CANCELLED;
    pool_.deallocate(o);
    return true;
}

// ── place_in_book / remove_from_book ─────────────────────────────────────────
void OrderBook::place_in_book(Order* o) {
    if (o->is_buy()) {
        bids_[o->limit_price].push_back(o);
    } else {
        asks_[o->limit_price].push_back(o);
    }
    order_map_[o->id] = o;
}

void OrderBook::remove_from_book(Order* o) {
    if (o->is_buy()) {
        auto it = bids_.find(o->limit_price);
        if (it != bids_.end()) {
            it->second.remove(o);
            if (it->second.empty()) bids_.erase(it);
        }
    } else {
        auto it = asks_.find(o->limit_price);
        if (it != asks_.end()) {
            it->second.remove(o);
            if (it->second.empty()) asks_.erase(it);
        }
    }
}

// ── Query ─────────────────────────────────────────────────────────────────────
Price OrderBook::best_bid() const noexcept {
    return bids_.empty() ? INVALID_PRICE : bids_.begin()->first;
}
Price OrderBook::best_ask() const noexcept {
    return asks_.empty() ? INVALID_PRICE : asks_.begin()->first;
}
Quantity OrderBook::bid_qty_at(Price p) const noexcept {
    auto it = bids_.find(p);
    return (it != bids_.end()) ? it->second.total_qty() : 0;
}
Quantity OrderBook::ask_qty_at(Price p) const noexcept {
    auto it = asks_.find(p);
    return (it != asks_.end()) ? it->second.total_qty() : 0;
}

bool OrderBook::is_consistent() const noexcept {
    // Best bid must be strictly below best ask (no crossed book)
    Price bb = best_bid(), ba = best_ask();
    if (bb != INVALID_PRICE && ba != INVALID_PRICE && bb >= ba) return false;
    return true;
}

void OrderBook::print_top(int n, std::ostream& os) const {
    os << "=== Order Book: symbol=" << symbol_ << " ===\n";
    os << "  ASKS:\n";
    int i = 0;
    for (auto it = asks_.rbegin(); it != asks_.rend() && i < n; ++it, ++i) {
        os << "    " << price_to_double(it->first)
           << "  qty=" << it->second.total_qty() << "\n";
    }
    os << "  --- spread ---\n";
    os << "  BIDS:\n";
    i = 0;
    for (auto it = bids_.begin(); it != bids_.end() && i < n; ++it, ++i) {
        os << "    " << price_to_double(it->first)
           << "  qty=" << it->second.total_qty() << "\n";
    }
}

} // namespace obm
