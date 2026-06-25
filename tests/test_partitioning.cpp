#include "obm/MatchingEngine.hpp"
#include <gtest/gtest.h>
#include <thread>

using namespace obm;

static MatchingEngine::Config cfg4() {
    MatchingEngine::Config c;
    c.n_partitions = 4;
    return c;
}

static std::atomic<OrderId> g_part_id{300000};

static OrderEvent limit_ev(Symbol sym, Side side, double price, Quantity qty) {
    OrderEvent ev{};
    ev.type       = OrderEvent::Type::NEW_ORDER;
    ev.order_type = OrderType::LIMIT;
    ev.side       = side;
    ev.symbol     = sym;
    ev.order_id   = g_part_id.fetch_add(1);
    ev.price      = double_to_price(price);
    ev.qty        = qty;
    ev.tif        = TimeInForce::GTC;
    return ev;
}

// ── Routing ───────────────────────────────────────────────────────────────────

TEST(Partitioning, SymbolsRoutedToCorrectPartition) {
    MatchingEngine eng(cfg4());
    std::vector<Fill> fills;
    eng.on_fill([&](const Fill& f) { fills.push_back(f); });

    for (Symbol s = 0; s < 8; ++s) {
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::SELL, 100.0, 10)));
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::BUY,  100.0, 10)));
    }
    eng.run_once();
    EXPECT_EQ(fills.size(), 8u);
}

TEST(Partitioning, CrossSymbolNoInteraction) {
    MatchingEngine eng(cfg4());
    std::vector<Fill> fills;
    eng.on_fill([&](const Fill& f) { fills.push_back(f); });

    ASSERT_TRUE(eng.submit(limit_ev(0, Side::BUY,  100.0, 50)));
    ASSERT_TRUE(eng.submit(limit_ev(1, Side::SELL, 100.0, 50)));
    eng.run_once();

    EXPECT_EQ(fills.size(), 0u);

    auto* b0 = eng.book_for(0);
    auto* b1 = eng.book_for(1);
    ASSERT_NE(b0, nullptr);
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b0->best_bid(), double_to_price(100.0));
    EXPECT_EQ(b1->best_ask(), double_to_price(100.0));
}

// ── Stats aggregate across partitions ────────────────────────────────────────

TEST(Partitioning, StatsAggregateAcrossPartitions) {
    MatchingEngine eng(cfg4());
    uint64_t fill_count = 0;
    eng.on_fill([&](const Fill&) { ++fill_count; });

    for (Symbol s = 0; s < 4; ++s) {
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::SELL, 100.0, 5)));
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::BUY,  100.0, 5)));
    }
    eng.run_once();

    auto stats = eng.stats();
    EXPECT_EQ(stats.orders_received, 8u);
    EXPECT_EQ(stats.fills_generated, 4u);
    EXPECT_EQ(stats.volume_traded,   20u);
    EXPECT_EQ(fill_count, 4u);
}

// ── book_for routes correctly ─────────────────────────────────────────────────

TEST(Partitioning, BookForSymbolCorrect) {
    MatchingEngine eng(cfg4());
    ASSERT_TRUE(eng.submit(limit_ev(7, Side::BUY, 99.0, 100)));
    eng.run_once();

    EXPECT_EQ(eng.book_for(7)->best_bid(), double_to_price(99.0));
    EXPECT_EQ(eng.book_for(3), nullptr);
}

// ── run_once drains all partitions ────────────────────────────────────────────

TEST(Partitioning, RunOnceDrainsAllPartitions) {
    MatchingEngine eng(cfg4());
    std::vector<Fill> fills;
    eng.on_fill([&](const Fill& f) { fills.push_back(f); });

    for (Symbol s = 0; s < 4; ++s) {
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::SELL, 100.0, 1)));
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::BUY,  100.0, 1)));
    }
    eng.run_once();
    EXPECT_EQ(fills.size(), 4u);
}

// ── Multi-threaded run() ──────────────────────────────────────────────────────

TEST(Partitioning, ThreadedRunShutdown) {
    MatchingEngine eng(cfg4());
    std::atomic<uint64_t> fill_count{0};
    eng.on_fill([&](const Fill&) { fill_count.fetch_add(1); });

    std::thread t([&] { eng.run(); });

    for (int i = 0; i < 100; ++i) {
        Symbol s = static_cast<Symbol>(i % 4);
        while (!eng.submit(limit_ev(s, Side::SELL, 100.0, 1))) {}
        while (!eng.submit(limit_ev(s, Side::BUY,  100.0, 1))) {}
    }

    OrderEvent shutdown{};
    shutdown.type = OrderEvent::Type::SHUTDOWN;
    while (!eng.submit(shutdown)) {}

    t.join();

    auto stats = eng.stats();
    EXPECT_EQ(stats.orders_received, 200u);
    EXPECT_EQ(fill_count.load(), 100u);
    EXPECT_EQ(stats.fills_generated, fill_count.load());
}

// ── Single-partition config ───────────────────────────────────────────────────

TEST(Partitioning, SinglePartitionAllSymbols) {
    MatchingEngine::Config c;
    c.n_partitions = 1;
    MatchingEngine eng(c);
    std::vector<Fill> fills;
    eng.on_fill([&](const Fill& f) { fills.push_back(f); });

    for (Symbol s = 0; s < 10; ++s) {
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::SELL, 100.0, 1)));
        ASSERT_TRUE(eng.submit(limit_ev(s, Side::BUY,  100.0, 1)));
    }
    eng.run_once();
    EXPECT_EQ(fills.size(), 10u);
}

// ── Memory balanced — cancels route to correct partition via symbol ───────────

TEST(Partitioning, MemoryBalancedAfterCancels) {
    MatchingEngine eng(cfg4());

    // Track (symbol, order_id) so we cancel in the right partition
    struct Entry { Symbol sym; OrderId id; };
    std::vector<Entry> entries;

    for (Symbol s = 0; s < 4; ++s) {
        for (int i = 0; i < 25; ++i) {
            auto ev = limit_ev(s, Side::BUY, 99.0, 10);
            entries.push_back({s, ev.order_id});
            ASSERT_TRUE(eng.submit(ev));
        }
    }
    eng.run_once();

    // Cancel each order using its original symbol (routes to correct partition+book)
    for (auto& e : entries) {
        OrderEvent cev{};
        cev.type     = OrderEvent::Type::CANCEL_ORDER;
        cev.symbol   = e.sym;
        cev.order_id = e.id;
        ASSERT_TRUE(eng.submit(cev));
    }
    eng.run_once();

    for (Symbol s = 0; s < 4; ++s) {
        auto* bk = eng.book_for(s);
        ASSERT_NE(bk, nullptr);
        EXPECT_EQ(bk->order_count(), 0u);
        EXPECT_TRUE(bk->is_consistent());
    }
}
