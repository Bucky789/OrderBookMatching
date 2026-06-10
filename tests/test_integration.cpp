#include "obm/MatchingEngine.hpp"
#include <gtest/gtest.h>
#include <random>
#include <thread>

using namespace obm;

static constexpr Symbol SYM = 4;

TEST(Integration, SingleThreadHighVolume) {
    MatchingEngine eng;
    uint64_t fill_count = 0;
    uint64_t fill_volume = 0;
    eng.on_fill([&](const Fill& f) {
        ++fill_count;
        fill_volume += f.fill_qty;
    });

    std::mt19937_64 rng(1234);
    constexpr int N = 10'000;

    for (int i = 0; i < N; ++i) {
        OrderEvent ev{};
        ev.type       = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side       = (rng() % 2 == 0) ? Side::BUY : Side::SELL;
        ev.tif        = TimeInForce::GTC;
        ev.symbol     = SYM;
        ev.order_id   = static_cast<OrderId>(i + 500000);
        ev.price      = double_to_price(99.0 + (rng() % 200) * 0.01);
        ev.qty        = static_cast<Quantity>(1 + rng() % 100);
        ASSERT_TRUE(eng.submit(ev));
    }
    eng.run_once();

    auto stats = eng.stats();
    EXPECT_EQ(stats.orders_received, static_cast<uint64_t>(N));
    EXPECT_EQ(stats.fills_generated, fill_count);
    EXPECT_EQ(stats.volume_traded, fill_volume);
    EXPECT_GT(fill_count, 0u) << "Expected some fills with random orders";

    auto* book = eng.book_for(SYM);
    ASSERT_NE(book, nullptr);
    EXPECT_TRUE(book->is_consistent());
}

TEST(Integration, GatewayEngineThread) {
    MatchingEngine eng;
    std::atomic<uint64_t> fill_count{0};
    eng.on_fill([&](const Fill&) { fill_count.fetch_add(1, std::memory_order_relaxed); });

    // Engine on dedicated thread
    std::thread engine_thread([&] { eng.run(); });

    std::mt19937_64 rng(9999);
    constexpr int N = 5'000;

    for (int i = 0; i < N; ++i) {
        OrderEvent ev{};
        ev.type       = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side       = (rng() % 2 == 0) ? Side::BUY : Side::SELL;
        ev.tif        = TimeInForce::GTC;
        ev.symbol     = SYM + 1;
        ev.order_id   = static_cast<OrderId>(i + 600000);
        ev.price      = double_to_price(99.0 + (rng() % 200) * 0.01);
        ev.qty        = static_cast<Quantity>(1 + rng() % 100);
        while (!eng.submit(ev)) {} // spin if ring full
    }

    // Send shutdown
    OrderEvent shutdown{};
    shutdown.type = OrderEvent::Type::SHUTDOWN;
    while (!eng.submit(shutdown)) {}

    engine_thread.join();

    auto stats = eng.stats();
    EXPECT_EQ(stats.orders_received, static_cast<uint64_t>(N));
    EXPECT_EQ(stats.fills_generated, fill_count.load());
    EXPECT_GT(fill_count.load(), 0u);
}

TEST(Integration, MemoryBalanced) {
    // Pool allocated == pool freed after all orders processed
    MemoryPool<Order> pool(2);
    OrderBook book(SYM + 2, pool);

    std::vector<Fill> fills;
    book.set_fill_callback([&](const Fill& f) { fills.push_back(f); });

    std::mt19937_64 rng(77);
    std::vector<OrderId> live_ids;
    OrderId next = 700000;

    for (int i = 0; i < 200; ++i) {
        // Occasionally cancel a live order
        if (!live_ids.empty() && rng() % 5 == 0) {
            OrderId cancel_id = live_ids[rng() % live_ids.size()];
            OrderEvent cev{};
            cev.type     = OrderEvent::Type::CANCEL_ORDER;
            cev.symbol   = SYM + 2;
            cev.order_id = cancel_id;
            book.cancel_order(cancel_id);
            live_ids.erase(std::find(live_ids.begin(), live_ids.end(), cancel_id));
            continue;
        }

        OrderEvent ev{};
        ev.type       = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side       = (rng() % 2 == 0) ? Side::BUY : Side::SELL;
        ev.tif        = TimeInForce::GTC;
        ev.symbol     = SYM + 2;
        ev.order_id   = next++;
        ev.price      = double_to_price(99.0 + (rng() % 100) * 0.01);
        ev.qty        = static_cast<Quantity>(1 + rng() % 50);

        book.add_order(ev);
        // Always track — cancel_order is idempotent for already-filled orders.
        // Edge case: aggressive fills one passive (net pool change = 0) but itself
        // rests in book. Without always tracking, we'd miss freeing it at the end.
        live_ids.push_back(ev.order_id);
    }

    // Cancel all remaining orders (cancel_order is a no-op for already-filled orders)
    for (OrderId id : live_ids) book.cancel_order(id);

    EXPECT_EQ(pool.allocated(), 0u) << "Memory leak: " << pool.allocated() << " orders still allocated";
}
