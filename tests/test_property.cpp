#include "obm/MatchingEngine.hpp"
#include <gtest/gtest.h>
#include <random>

using namespace obm;

static constexpr Symbol SYM = 3;

TEST(PropertyTest, VolumeConservation) {
    // Every fill qty appears once on each side.
    // Sum of fill_qty * 2 == total filled qty on both sides.
    MatchingEngine eng;
    uint64_t total_fill_volume = 0;
    eng.on_fill([&](const Fill& f) { total_fill_volume += f.fill_qty; });

    std::mt19937_64 rng(42);
    auto rand_price = [&]{ return 99.0 + (rng() % 300) * 0.01; };
    auto rand_qty   = [&]{ return static_cast<Quantity>(1 + rng() % 200); };

    for (int i = 0; i < 2000; ++i) {
        OrderEvent ev{};
        ev.type       = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side       = (rng() % 2 == 0) ? Side::BUY : Side::SELL;
        ev.tif        = TimeInForce::GTC;
        ev.symbol     = SYM;
        ev.order_id   = static_cast<OrderId>(i + 100000);
        ev.price      = double_to_price(rand_price());
        ev.qty        = rand_qty();
        ASSERT_TRUE(eng.submit(ev));
    }
    eng.run_once();

    auto stats = eng.stats();
    EXPECT_EQ(stats.volume_traded, total_fill_volume);
}

// Separate test to check no phantom fills (filled qty <= original qty)
TEST(PropertyTest, FilledQtyNeverExceedsOriginal) {
    MatchingEngine eng;
    std::unordered_map<OrderId, Quantity> original_qty;
    std::unordered_map<OrderId, Quantity> filled_qty;

    eng.on_fill([&](const Fill& f) {
        filled_qty[f.aggressive_order_id] += f.fill_qty;
        filled_qty[f.passive_order_id]    += f.fill_qty;
    });

    std::mt19937_64 rng(99);
    for (int i = 0; i < 500; ++i) {
        OrderEvent ev{};
        ev.type       = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side       = (rng() % 2 == 0) ? Side::BUY : Side::SELL;
        ev.tif        = TimeInForce::GTC;
        ev.symbol     = SYM;
        ev.order_id   = static_cast<OrderId>(i + 200000);
        ev.price      = double_to_price(99.0 + (rng() % 200) * 0.01);
        ev.qty        = static_cast<Quantity>(1 + rng() % 100);
        original_qty[ev.order_id] = ev.qty;
        ASSERT_TRUE(eng.submit(ev));
    }
    eng.run_once();

    for (auto& [id, fq] : filled_qty) {
        if (original_qty.count(id)) {
            EXPECT_LE(fq, original_qty[id]) << "Order " << id << " over-filled";
        }
    }
}

TEST(PropertyTest, BookNeverCrossed) {
    MatchingEngine eng;
    std::mt19937_64 rng(7);

    for (int i = 0; i < 1000; ++i) {
        OrderEvent ev{};
        ev.type       = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side       = (rng() % 2 == 0) ? Side::BUY : Side::SELL;
        ev.tif        = TimeInForce::GTC;
        ev.symbol     = SYM;
        ev.order_id   = static_cast<OrderId>(i + 300000);
        ev.price      = double_to_price(99.0 + (rng() % 400) * 0.01);
        ev.qty        = static_cast<Quantity>(1 + rng() % 50);
        ASSERT_TRUE(eng.submit(ev));
        eng.run_once();

        auto* book = eng.book_for(SYM);
        if (book) EXPECT_TRUE(book->is_consistent()) << "Book crossed at step " << i;
    }
}
