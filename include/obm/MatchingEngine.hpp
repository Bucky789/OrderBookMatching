#pragma once

#include "OrderBook.hpp"
#include "RingBuffer.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace obm {

// Multi-symbol partitioned matching engine.
//
// Gateway thread calls submit(ev); events are routed to partition[symbol % N].
// Each partition owns one SPSC ring, one memory pool, and one worker thread.
// No cross-partition synchronization on the hot path — mechanical sympathy.
class MatchingEngine {
public:
    struct Config {
        std::size_t pool_initial_slabs = 4;
        std::size_t n_partitions       = 4;   // one ring + thread per partition
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
    // Route event to partition[ev.symbol % n_partitions]. Non-blocking.
    [[nodiscard]] bool submit(const OrderEvent& ev) noexcept;

    // ── Engine thread API ─────────────────────────────────────────────────────
    // Launch all partition threads; block until all stop.
    // A SHUTDOWN event sent to ANY partition stops ALL partitions after draining.
    void run();

    // Drain all partition rings once without spawning threads. For tests.
    void run_once();

    // Set stop flag and join all running workers.
    void shutdown();

    // ── Callbacks (register before calling run/run_once) ─────────────────────
    void on_fill(FillCallback cb);
    void on_reject(RejectCallback cb);

    // ── Stats ─────────────────────────────────────────────────────────────────
    [[nodiscard]] Stats stats() const noexcept;

    // ── Direct book access (for testing / inspection) ─────────────────────────
    [[nodiscard]] OrderBook* book_for(Symbol sym);
    [[nodiscard]] const std::unordered_map<Symbol, OrderBook>*
    books_for_partition(std::size_t idx) const noexcept;

private:
    struct Partition {
        MemoryPool<Order>                     pool;
        std::unique_ptr<OrderRingBuffer>      ring;
        std::unordered_map<Symbol, OrderBook> books;
        Stats                                 stats{};

        explicit Partition(std::size_t slabs)
            : pool(slabs), ring(std::make_unique<OrderRingBuffer>()) {}

        Partition(const Partition&)            = delete;
        Partition& operator=(const Partition&) = delete;
        Partition(Partition&&)                 = delete;
        Partition& operator=(Partition&&)      = delete;
    };

    Config                                  cfg_;
    std::vector<std::unique_ptr<Partition>> partitions_;
    std::vector<std::thread>                workers_;
    FillCallback                            fill_cb_;
    RejectCallback                          reject_cb_;
    std::atomic<bool>                       stop_flag_{false};

    Partition&  partition_for(Symbol sym) noexcept;
    void        run_partition(Partition& p);
    void        process_event(Partition& p, const OrderEvent& ev);
    OrderBook&  get_or_create_book(Partition& p, Symbol sym);
};

} // namespace obm
