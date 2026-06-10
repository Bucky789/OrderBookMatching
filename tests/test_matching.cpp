#include "obm/MatchingEngine.hpp"
#include <gtest/gtest.h>
#include <vector>

using namespace obm;

// ── Helpers ───────────────────────────────────────────────────────────────────
static constexpr Symbol SYM = 1;
static OrderId next_id() {
    static std::atomic<OrderId> ctr{1};
    return ctr.fetch_add(1, std::memory_order_relaxed);
}

static OrderEvent make_limit(Side side, double price, Quantity qty) {
    OrderEvent ev{};
    ev.type       = OrderEvent::Type::NEW_ORDER;
    ev.order_type = OrderType::LIMIT;
    ev.side       = side;
    ev.tif        = TimeInForce::GTC;
    ev.symbol     = SYM;
    ev.order_id   = next_id();
    ev.price      = double_to_price(price);
    ev.qty        = qty;
    return ev;
}
static OrderEvent make_market(Side side, Quantity qty) {
    OrderEvent ev{};
    ev.type       = OrderEvent::Type::NEW_ORDER;
    ev.order_type = OrderType::MARKET;
    ev.side       = side;
    ev.symbol     = SYM;
    ev.order_id   = next_id();
    ev.qty        = qty;
    return ev;
}
static OrderEvent make_ioc(Side side, double price, Quantity qty) {
    auto ev = make_limit(side, price, qty);
    ev.order_type = OrderType::IOC;
    return ev;
}
static OrderEvent make_fok(Side side, double price, Quantity qty) {
    auto ev = make_limit(side, price, qty);
    ev.order_type = OrderType::FOK;
    return ev;
}
static OrderEvent make_iceberg(Side side, double price, Quantity total, Quantity peak) {
    auto ev = make_limit(side, price, total);
    ev.order_type = OrderType::ICEBERG;
    ev.visible_qty = peak;
    return ev;
}
static OrderEvent make_cancel(OrderId id) {
    OrderEvent ev{};
    ev.type     = OrderEvent::Type::CANCEL_ORDER;
    ev.symbol   = SYM;
    ev.order_id = id;
    return ev;
}

struct EngineFixture : ::testing::Test {
    MatchingEngine eng;
    std::vector<Fill> fills;

    EngineFixture() {
        eng.on_fill([this](const Fill& f) { fills.push_back(f); });
    }
    void submit(const OrderEvent& ev) { ASSERT_TRUE(eng.submit(ev)); eng.run_once(); }
    void drain() { eng.run_once(); }
};

// ── Basic matching ────────────────────────────────────────────────────────────
TEST_F(EngineFixture, LimitBuyMatchesSell) {
    submit(make_limit(Side::SELL, 100.0, 100));
    EXPECT_EQ(fills.size(), 0u);

    submit(make_limit(Side::BUY, 100.0, 100));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 100u);
    EXPECT_EQ(fills[0].fill_price, double_to_price(100.0));
}

TEST_F(EngineFixture, NoMatchWhenPricesDoNotCross) {
    submit(make_limit(Side::SELL, 101.0, 100));
    submit(make_limit(Side::BUY,  99.0, 100));
    EXPECT_EQ(fills.size(), 0u);
    auto* book = eng.book_for(SYM);
    ASSERT_NE(book, nullptr);
    EXPECT_EQ(book->best_ask(), double_to_price(101.0));
    EXPECT_EQ(book->best_bid(), double_to_price(99.0));
}

TEST_F(EngineFixture, PriceTimePriority) {
    // Two sell orders at same price — earlier one fills first
    auto ev_a = make_limit(Side::SELL, 100.0, 50);
    auto ev_b = make_limit(Side::SELL, 100.0, 50);
    OrderId id_a = ev_a.order_id;
    submit(ev_a);
    submit(ev_b);

    submit(make_limit(Side::BUY, 100.0, 75));
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].passive_order_id, id_a); // A fills first
    EXPECT_EQ(fills[0].fill_qty, 50u);
    EXPECT_EQ(fills[1].fill_qty, 25u);
}

TEST_F(EngineFixture, PartialFill) {
    submit(make_limit(Side::SELL, 100.0, 200));
    submit(make_limit(Side::BUY, 100.0, 50));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 50u);
    // Remaining 150 should still be in ask book
    auto* book = eng.book_for(SYM);
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 150u);
}

TEST_F(EngineFixture, MarketOrderFullFill) {
    submit(make_limit(Side::SELL, 100.0, 100));
    submit(make_market(Side::BUY, 100));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 100u);
    auto* book = eng.book_for(SYM);
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 0u);
}

TEST_F(EngineFixture, MarketOrderCancelledIfNoLiquidity) {
    submit(make_market(Side::BUY, 100));
    EXPECT_EQ(fills.size(), 0u);
}

TEST_F(EngineFixture, IOCPartialFill) {
    submit(make_limit(Side::SELL, 100.0, 30));
    submit(make_ioc(Side::BUY, 100.0, 100));
    // Should fill 30, cancel remaining 70 — never rests in book
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 30u);
    auto* book = eng.book_for(SYM);
    EXPECT_EQ(book->bid_qty_at(double_to_price(100.0)), 0u); // no resting bid
}

TEST_F(EngineFixture, FOKFullFillSuccess) {
    submit(make_limit(Side::SELL, 100.0, 100));
    submit(make_fok(Side::BUY, 100.0, 100));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 100u);
}

TEST_F(EngineFixture, FOKRejectedWhenInsufficientLiquidity) {
    submit(make_limit(Side::SELL, 100.0, 50)); // only 50 available
    submit(make_fok(Side::BUY, 100.0, 100));   // needs 100
    EXPECT_EQ(fills.size(), 0u);               // rejected — book unchanged
    auto* book = eng.book_for(SYM);
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 50u); // still resting
}

TEST_F(EngineFixture, CancelOrder) {
    auto ev = make_limit(Side::SELL, 100.0, 100);
    OrderId id = ev.order_id;
    submit(ev);

    auto* book = eng.book_for(SYM);
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 100u);

    submit(make_cancel(id));
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 0u);
    EXPECT_EQ(book->order_count(), 0u);
}

TEST_F(EngineFixture, IcebergVisible) {
    // Iceberg: total 1000, peak 100 — only 100 visible in book
    submit(make_iceberg(Side::SELL, 100.0, 1000, 100));
    auto* book = eng.book_for(SYM);
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 100u);

    // Buy 100 — fills visible, reloads 100 from reserve
    submit(make_limit(Side::BUY, 100.0, 100));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 100u);
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 100u); // reloaded
}

TEST_F(EngineFixture, IcebergFullyExhausted) {
    submit(make_iceberg(Side::SELL, 100.0, 200, 100));
    submit(make_limit(Side::BUY, 100.0, 200));
    EXPECT_EQ(fills.size(), 2u);                               // 2 fills of 100
    EXPECT_EQ(fills[0].fill_qty + fills[1].fill_qty, 200u);
    auto* book = eng.book_for(SYM);
    EXPECT_EQ(book->ask_qty_at(double_to_price(100.0)), 0u);
}

TEST_F(EngineFixture, BookConsistencyAfterManyOrders) {
    for (int i = 0; i < 50; ++i) {
        submit(make_limit(Side::SELL, 101.0 + i * 0.01, 10));
        submit(make_limit(Side::BUY,  99.0 - i * 0.01, 10));
    }
    submit(make_market(Side::BUY, 150));
    submit(make_market(Side::SELL, 150));
    auto* book = eng.book_for(SYM);
    EXPECT_TRUE(book->is_consistent());
}
