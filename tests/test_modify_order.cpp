#include "obm/MatchingEngine.hpp"
#include <gtest/gtest.h>

using namespace obm;

static constexpr Symbol SYM = 20;
static std::atomic<OrderId> g_mod_id{200000};

static OrderEvent limit_ev(Side side, double price, Quantity qty) {
    OrderEvent ev{};
    ev.type       = OrderEvent::Type::NEW_ORDER;
    ev.order_type = OrderType::LIMIT;
    ev.side       = side;
    ev.symbol     = SYM;
    ev.order_id   = g_mod_id.fetch_add(1);
    ev.price      = double_to_price(price);
    ev.qty        = qty;
    ev.tif        = TimeInForce::GTC;
    return ev;
}
static OrderEvent modify_ev(OrderId id, Side side, double price, Quantity qty) {
    OrderEvent ev = limit_ev(side, price, qty);
    ev.type     = OrderEvent::Type::MODIFY_ORDER;
    ev.order_id = id;
    return ev;
}

struct ModifyFixture : ::testing::Test {
    MatchingEngine    eng;
    std::vector<Fill> fills;

    ModifyFixture() {
        eng.on_fill([this](const Fill& f) { fills.push_back(f); });
    }
    void submit(const OrderEvent& ev) { ASSERT_TRUE(eng.submit(ev)); eng.run_once(); }
    OrderBook* book() { return eng.book_for(SYM); }
};

// ── Price change ──────────────────────────────────────────────────────────────

TEST_F(ModifyFixture, ModifyBidPrice) {
    auto ev = limit_ev(Side::BUY, 99.0, 100);
    OrderId id = ev.order_id;
    submit(ev);

    auto* bk = book();
    ASSERT_NE(bk, nullptr);
    EXPECT_EQ(bk->bid_qty_at(double_to_price(99.0)), 100u);

    submit(modify_ev(id, Side::BUY, 98.0, 100));

    EXPECT_EQ(bk->bid_qty_at(double_to_price(99.0)), 0u);  // old price gone
    EXPECT_EQ(bk->bid_qty_at(double_to_price(98.0)), 100u);  // new price present
}

TEST_F(ModifyFixture, ModifyAskPrice) {
    auto ev = limit_ev(Side::SELL, 101.0, 50);
    OrderId id = ev.order_id;
    submit(ev);

    auto* bk = book();
    EXPECT_EQ(bk->ask_qty_at(double_to_price(101.0)), 50u);

    submit(modify_ev(id, Side::SELL, 102.0, 50));

    EXPECT_EQ(bk->ask_qty_at(double_to_price(101.0)), 0u);
    EXPECT_EQ(bk->ask_qty_at(double_to_price(102.0)), 50u);
}

// ── Qty change (same price) ───────────────────────────────────────────────────

TEST_F(ModifyFixture, ModifyQty) {
    auto ev = limit_ev(Side::BUY, 100.0, 200);
    OrderId id = ev.order_id;
    submit(ev);

    auto* bk = book();
    EXPECT_EQ(bk->bid_qty_at(double_to_price(100.0)), 200u);

    submit(modify_ev(id, Side::BUY, 100.0, 50));

    EXPECT_EQ(bk->bid_qty_at(double_to_price(100.0)), 50u);
}

// ── Modified order gets new time priority ─────────────────────────────────────

TEST_F(ModifyFixture, ModifyLosesTimePriority) {
    // Two bids at 100.  Second one cancels first via MODIFY.
    // After modify, original order ID should now be lowest priority.
    auto ev1 = limit_ev(Side::BUY, 100.0, 10);
    auto ev2 = limit_ev(Side::BUY, 100.0, 10);
    OrderId id1 = ev1.order_id;
    submit(ev1);
    submit(ev2);

    // Modify order 1 → it gets re-inserted behind order 2
    submit(modify_ev(id1, Side::BUY, 100.0, 10));

    // Aggressive sell for 10 → should fill against the first remaining bid (ev2)
    auto sell = limit_ev(Side::SELL, 100.0, 10);
    submit(sell);

    ASSERT_EQ(fills.size(), 1u);
    // ev2's order is now first in queue; fill should be against ev2
    EXPECT_EQ(fills[0].passive_order_id, ev2.order_id);
}

// ── Modify triggers match ─────────────────────────────────────────────────────

TEST_F(ModifyFixture, ModifyIntoAggress) {
    // Resting ask at 101. Modify bid from 99 → 101 should cross.
    submit(limit_ev(Side::SELL, 101.0, 30));

    auto bid = limit_ev(Side::BUY, 99.0, 30);
    OrderId bid_id = bid.order_id;
    submit(bid);
    EXPECT_EQ(fills.size(), 0u);  // no cross yet

    submit(modify_ev(bid_id, Side::BUY, 101.0, 30));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fill_qty, 30u);
}

// ── Modify non-existent order ─────────────────────────────────────────────────

TEST_F(ModifyFixture, ModifyNonExistentAddsOrder) {
    // Cancel of unknown id is no-op; then add_order inserts new order.
    OrderEvent ev = modify_ev(999999, Side::BUY, 100.0, 50);
    submit(ev);
    auto* bk = book();
    ASSERT_NE(bk, nullptr);
    EXPECT_EQ(bk->bid_qty_at(double_to_price(100.0)), 50u);
}

// ── Multiple modifies ─────────────────────────────────────────────────────────

TEST_F(ModifyFixture, ChainedModifies) {
    auto ev = limit_ev(Side::BUY, 100.0, 100);
    OrderId id = ev.order_id;
    submit(ev);

    submit(modify_ev(id, Side::BUY, 101.0, 80));
    submit(modify_ev(id, Side::BUY, 102.0, 60));

    auto* bk = book();
    EXPECT_EQ(bk->bid_qty_at(double_to_price(100.0)), 0u);
    EXPECT_EQ(bk->bid_qty_at(double_to_price(101.0)), 0u);
    EXPECT_EQ(bk->bid_qty_at(double_to_price(102.0)), 60u);
}

// ── Book consistency preserved ────────────────────────────────────────────────

TEST_F(ModifyFixture, BookConsistentAfterModify) {
    auto b = limit_ev(Side::BUY,  99.0, 100);
    auto a = limit_ev(Side::SELL, 101.0, 100);
    submit(b);
    submit(a);
    submit(modify_ev(b.order_id, Side::BUY, 98.0, 100));
    submit(modify_ev(a.order_id, Side::SELL, 102.0, 100));

    EXPECT_TRUE(book()->is_consistent());
    EXPECT_EQ(fills.size(), 0u);
}
