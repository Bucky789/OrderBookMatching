#include "obm/MatchingEngine.hpp"

namespace obm {

MatchingEngine::MatchingEngine(Config cfg) : cfg_(cfg) {
    partitions_.reserve(cfg.n_partitions);
    for (std::size_t i = 0; i < cfg.n_partitions; ++i)
        partitions_.emplace_back(std::make_unique<Partition>(cfg.pool_initial_slabs));
}

MatchingEngine::~MatchingEngine() {
    shutdown();
}

bool MatchingEngine::submit(const OrderEvent& ev) noexcept {
    return partition_for(ev.symbol).ring->try_push(ev);
}

MatchingEngine::Partition& MatchingEngine::partition_for(Symbol sym) noexcept {
    return *partitions_[sym % partitions_.size()];
}

void MatchingEngine::run() {
    stop_flag_.store(false, std::memory_order_relaxed);
    workers_.clear();
    workers_.reserve(partitions_.size());
    for (auto& p : partitions_)
        workers_.emplace_back([this, &p] { run_partition(*p); });
    for (auto& t : workers_) t.join();
    workers_.clear();
}

void MatchingEngine::run_partition(Partition& p) {
    OrderEvent ev;
    for (;;) {
        if (p.ring->try_pop(ev)) {
            if (ev.type == OrderEvent::Type::SHUTDOWN) {
                // Broadcast stop; drain remaining events before exiting.
                stop_flag_.store(true, std::memory_order_release);
                while (p.ring->try_pop(ev)) {
                    if (ev.type != OrderEvent::Type::SHUTDOWN)
                        process_event(p, ev);
                }
                return;
            }
            process_event(p, ev);
        } else if (stop_flag_.load(std::memory_order_acquire)) {
            // Another partition received SHUTDOWN; drain then exit.
            while (p.ring->try_pop(ev)) {
                if (ev.type != OrderEvent::Type::SHUTDOWN)
                    process_event(p, ev);
            }
            return;
        }
    }
}

void MatchingEngine::run_once() {
    OrderEvent ev;
    for (auto& p : partitions_) {
        while (p->ring->try_pop(ev)) {
            if (ev.type == OrderEvent::Type::SHUTDOWN) break;
            process_event(*p, ev);
        }
    }
}

void MatchingEngine::shutdown() {
    stop_flag_.store(true, std::memory_order_release);
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

void MatchingEngine::process_event(Partition& p, const OrderEvent& ev) {
    ++p.stats.orders_received;
    OrderBook& book = get_or_create_book(p, ev.symbol);

    if (ev.type == OrderEvent::Type::CANCEL_ORDER) {
        bool ok = book.cancel_order(ev.order_id);
        if (ok) ++p.stats.orders_cancelled;
        return;
    }

    if (ev.type == OrderEvent::Type::MODIFY_ORDER) {
        book.cancel_order(ev.order_id);
        book.add_order(ev);
        return;
    }

    book.add_order(ev);
}

OrderBook& MatchingEngine::get_or_create_book(Partition& p, Symbol sym) {
    auto it = p.books.find(sym);
    if (it != p.books.end()) return it->second;

    auto [ins, ok] = p.books.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sym),
        std::forward_as_tuple(sym, p.pool)
    );
    OrderBook& book = ins->second;
    book.set_fill_callback([this, &p](const Fill& f) {
        ++p.stats.fills_generated;
        p.stats.volume_traded += f.fill_qty;
        ++p.stats.orders_matched;
        if (fill_cb_) fill_cb_(f);
    });
    book.set_reject_callback([this](OrderId id, std::string_view reason) {
        if (reject_cb_) reject_cb_(id, reason);
    });
    return book;
}

void MatchingEngine::on_fill(FillCallback cb)     { fill_cb_   = std::move(cb); }
void MatchingEngine::on_reject(RejectCallback cb) { reject_cb_ = std::move(cb); }

MatchingEngine::Stats MatchingEngine::stats() const noexcept {
    Stats total{};
    for (const auto& p : partitions_) {
        total.orders_received  += p->stats.orders_received;
        total.orders_matched   += p->stats.orders_matched;
        total.orders_cancelled += p->stats.orders_cancelled;
        total.fills_generated  += p->stats.fills_generated;
        total.volume_traded    += p->stats.volume_traded;
    }
    return total;
}

OrderBook* MatchingEngine::book_for(Symbol sym) {
    Partition& p = partition_for(sym);
    auto it = p.books.find(sym);
    return (it != p.books.end()) ? &it->second : nullptr;
}

const std::unordered_map<Symbol, OrderBook>*
MatchingEngine::books_for_partition(std::size_t idx) const noexcept {
    if (idx >= partitions_.size()) return nullptr;
    return &partitions_[idx]->books;
}

} // namespace obm
