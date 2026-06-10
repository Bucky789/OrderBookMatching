#include "obm/MatchingEngine.hpp"

namespace obm {

MatchingEngine::MatchingEngine(Config cfg)
    : cfg_(cfg),
      pool_(cfg.pool_initial_slabs),
      ring_(std::make_unique<OrderRingBuffer>()) {}

MatchingEngine::~MatchingEngine() {
    if (running_.load()) shutdown();
}

bool MatchingEngine::submit(const OrderEvent& ev) noexcept {
    return ring_->try_push(ev);
}

void MatchingEngine::run() {
    running_.store(true, std::memory_order_release);
    OrderEvent ev;
    while (true) {
        if (ring_->try_pop(ev)) {
            if (ev.type == OrderEvent::Type::SHUTDOWN) break;
            process_event(ev);
        }
    }
    running_.store(false, std::memory_order_release);
}

void MatchingEngine::run_once() {
    OrderEvent ev;
    while (ring_->try_pop(ev)) {
        if (ev.type == OrderEvent::Type::SHUTDOWN) break;
        process_event(ev);
    }
}

void MatchingEngine::shutdown() {
    OrderEvent ev{};
    ev.type = OrderEvent::Type::SHUTDOWN;
    while (!ring_->try_push(ev)) {}
    while (running_.load(std::memory_order_acquire)) {}
}

void MatchingEngine::process_event(const OrderEvent& ev) {
    ++stats_.orders_received;
    OrderBook& book = get_or_create_book(ev.symbol);

    if (ev.type == OrderEvent::Type::CANCEL_ORDER) {
        bool ok = book.cancel_order(ev.order_id);
        if (ok) ++stats_.orders_cancelled;
        return;
    }

    book.add_order(ev);
}

OrderBook& MatchingEngine::get_or_create_book(Symbol sym) {
    auto it = books_.find(sym);
    if (it != books_.end()) return it->second;

    auto [ins, ok] = books_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sym),
        std::forward_as_tuple(sym, pool_)
    );
    OrderBook& book = ins->second;
    book.set_fill_callback([this](const Fill& f) {
        ++stats_.fills_generated;
        stats_.volume_traded += f.fill_qty;
        ++stats_.orders_matched;
        if (fill_cb_) fill_cb_(f);
    });
    return book;
}

void MatchingEngine::on_fill(FillCallback cb)    { fill_cb_   = std::move(cb); }
void MatchingEngine::on_reject(RejectCallback cb) { reject_cb_ = std::move(cb); }

MatchingEngine::Stats MatchingEngine::stats() const noexcept { return stats_; }

OrderBook* MatchingEngine::book_for(Symbol sym) {
    auto it = books_.find(sym);
    return (it != books_.end()) ? &it->second : nullptr;
}

} // namespace obm
