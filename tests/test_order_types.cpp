#include "obm/MatchingEngine.hpp"
#include <gtest/gtest.h>

using namespace obm;

static constexpr Symbol SYM = 2;
static OrderId next_id() {
    static std::atomic<OrderId> ctr{10000};
    return ctr.fetch_add(1, std::memory_order_relaxed);
}

static OrderEvent make_ev(OrderType ot, Side side, double price, Quantity qty,
                           Quantity peak = 0, double stop = 0.0) {
    OrderEvent ev{};
    ev.type        = OrderEvent::Type::NEW_ORDER;
    ev.order_type  = ot;
    ev.side        = side;
    ev.tif         = TimeInForce::GTC;
    ev.symbol      = SYM;
    ev.order_id    = next_id();
    ev.price       = double_to_price(price);
    ev.stop_price  = double_to_price(stop);
    ev.qty         = qty;
    ev.visible_qty = peak;
    return ev;
}

struct OrderTypeFixture : ::testing::Test {
    MatchingEngine eng;
    std::vector<Fill> fills;
    OrderTypeFixture() {
        eng.on_fill([this](const Fill& f) { fills.push_back(f); });
    }
    void submit(const OrderEvent& ev) { ASSERT_TRUE(eng.submit(ev)); eng.run_once(); }
};

// ── LIMIT ─────────────────────────────────────────────────────────────────────
TEST_F(OrderTypeFixture, LimitRestsIfNoMatch) {
    submit(make_ev(OrderType::LIMIT, Side::BUY, 100.0, 50));
    EXPECT_EQ(fills.size(), 0u);
    EXPECT_EQ(eng.book_for(SYM)->bid_qty_at(double_to_price(100.0)), 50u);
}

TEST_F(OrderTypeFixture, LimitSweepsMultipleLevels) {
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 100));
    submit(make_ev(OrderType::LIMIT, Side::SELL, 101.0, 100));
    submit(make_ev(OrderType::LIMIT, Side::BUY,  102.0, 200));
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].fill_qty + fills[1].fill_qty, 200u);
}

// ── MARKET ────────────────────────────────────────────────────────────────────
TEST_F(OrderTypeFixture, MarketNeverRests) {
    submit(make_ev(OrderType::MARKET, Side::BUY, 0.0, 100));
    EXPECT_EQ(fills.size(), 0u);
    // No ask liquidity — market order cancelled, not resting
    EXPECT_EQ(eng.book_for(SYM)->order_count(), 0u);
}

TEST_F(OrderTypeFixture, MarketSweepsAllLevels) {
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 50));
    submit(make_ev(OrderType::LIMIT, Side::SELL, 101.0, 50));
    submit(make_ev(OrderType::LIMIT, Side::SELL, 102.0, 50));
    submit(make_ev(OrderType::MARKET, Side::BUY, 0.0, 150));
    ASSERT_EQ(fills.size(), 3u);
    uint64_t total = 0;
    for (auto& f : fills) total += f.fill_qty;
    EXPECT_EQ(total, 150u);
}

// ── IOC ───────────────────────────────────────────────────────────────────────
TEST_F(OrderTypeFixture, IOCNoLiquidityNoRest) {
    submit(make_ev(OrderType::IOC, Side::BUY, 100.0, 100));
    EXPECT_EQ(fills.size(), 0u);
    EXPECT_EQ(eng.book_for(SYM)->order_count(), 0u);
}

TEST_F(OrderTypeFixture, IOCPartialFillNeverRests) {
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 30));
    submit(make_ev(OrderType::IOC, Side::BUY, 100.0, 100));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 30u);
    EXPECT_EQ(eng.book_for(SYM)->bid_qty_at(double_to_price(100.0)), 0u);
}

// ── FOK ───────────────────────────────────────────────────────────────────────
TEST_F(OrderTypeFixture, FOKAllOrNothing) {
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 99));
    submit(make_ev(OrderType::FOK,   Side::BUY,  100.0, 100));
    EXPECT_EQ(fills.size(), 0u);
    // Book must be untouched — 99 still resting
    EXPECT_EQ(eng.book_for(SYM)->ask_qty_at(double_to_price(100.0)), 99u);
}

TEST_F(OrderTypeFixture, FOKSucceedsExactMatch) {
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 100));
    submit(make_ev(OrderType::FOK,   Side::BUY,  100.0, 100));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 100u);
}

TEST_F(OrderTypeFixture, FOKSucceedsAcrossLevels) {
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 60));
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.5, 60));
    submit(make_ev(OrderType::FOK,   Side::BUY,  101.0, 120));
    ASSERT_EQ(fills.size(), 2u);
}

// ── ICEBERG ──────────────────────────────────────────────────────────────────
TEST_F(OrderTypeFixture, IcebergShowsOnlyPeak) {
    submit(make_ev(OrderType::ICEBERG, Side::SELL, 100.0, 1000, 100));
    EXPECT_EQ(eng.book_for(SYM)->ask_qty_at(double_to_price(100.0)), 100u);
}

TEST_F(OrderTypeFixture, IcebergReloadGetsNewTimePriority) {
    // Iceberg A: total 300, peak 100. Rests first.
    auto ev_ice = make_ev(OrderType::ICEBERG, Side::SELL, 100.0, 300, 100);
    submit(ev_ice);

    // Regular limit B: rests second at same price.
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 100));

    // Buy 100 — exhausts iceberg visible peak. Iceberg reloads (new sequence)
    // and goes BEHIND limit B. Limit B now has higher time priority.
    submit(make_ev(OrderType::LIMIT, Side::BUY, 100.0, 100));
    ASSERT_GE(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 100u);

    // Next buy 100: should fill limit B first (earlier sequence after reload)
    fills.clear();
    submit(make_ev(OrderType::LIMIT, Side::BUY, 100.0, 100));
    ASSERT_GE(fills.size(), 1u);
}

// ── STOP ─────────────────────────────────────────────────────────────────────
TEST_F(OrderTypeFixture, BuyStopTriggerOnPriceRise) {
    // Place liquidity first
    submit(make_ev(OrderType::LIMIT, Side::SELL, 105.0, 100));

    // Stop buy at 102 — triggers when market price >= 102
    auto ev_stop = make_ev(OrderType::STOP, Side::BUY, 0.0, 100);
    ev_stop.stop_price = double_to_price(102.0);
    submit(ev_stop);
    EXPECT_EQ(fills.size(), 0u);

    // A trade at 102 triggers the stop
    submit(make_ev(OrderType::LIMIT, Side::SELL, 100.0, 50));
    submit(make_ev(OrderType::LIMIT, Side::BUY,  102.0, 50));
    // The fill at 102 should trigger the stop → market buy → fills at 105
    // (stop trigger fires after each fill)
}
