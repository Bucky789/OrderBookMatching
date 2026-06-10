#pragma once

#include "OrderBook.hpp"
#include "RingBuffer.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace obm {

class MatchingEngine {
public:
    struct Config {
        std::size_t pool_initial_slabs = 4;
        bool        enable_logging     = false;
    };

    struct Stats {
        uint64_t orders_received  = 0;
        uint64_t orders_matched   = 0;
        uint64_t orders_cancelled = 0;
        uint64_t fills_generated  = 0;
        uint64_t volume_traded    = 0;
    };

    using FillCallback   = std::function<void(const Fill&)>;
    using RejectCallback = std::function<void(OrderId, std::string_view)>;

    explicit MatchingEngine(Config cfg = {});
    ~MatchingEngine();

    MatchingEngine(const MatchingEngine&)            = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    // ── Gateway thread API ────────────────────────────────────────────────────
    // Non-blocking push to the SPSC ring buffer. Returns false if ring full.
    [[nodiscard]] bool submit(const OrderEvent& ev) noexcept;

    // ── Engine thread API ─────────────────────────────────────────────────────
    // Blocks until shutdown event received. Run on a dedicated thread.
    void run();

    // Process at most one batch from the ring buffer (non-blocking). For tests.
    void run_once();

    // Submit a SHUTDOWN event and wait for run() to return.
    void shutdown();

    // ── Callbacks (register before calling run/run_once) ─────────────────────
    void on_fill(FillCallback cb);
    void on_reject(RejectCallback cb);

    // ── Stats ─────────────────────────────────────────────────────────────────
    [[nodiscard]] Stats stats() const noexcept;

    // ── Direct book access (for testing) ─────────────────────────────────────
    [[nodiscard]] OrderBook* book_for(Symbol sym);
    [[nodiscard]] const std::unordered_map<Symbol, OrderBook>& books() const { return books_; }

private:
    Config cfg_;
    MemoryPool<Order>  pool_;
    // Ring buffer is 4 MB (65536 × 64 bytes) — heap-allocate to avoid stack overflow.
    std::unique_ptr<OrderRingBuffer> ring_;
    std::unordered_map<Symbol, OrderBook> books_;

    FillCallback   fill_cb_;
    RejectCallback reject_cb_;

    std::atomic<bool> running_{false};
    Stats stats_;

    void process_event(const OrderEvent& ev);
    OrderBook& get_or_create_book(Symbol sym);
};

} // namespace obm
