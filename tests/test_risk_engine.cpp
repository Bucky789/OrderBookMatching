#include "obm/RiskEngine.hpp"
#include <gtest/gtest.h>

using namespace obm;

static constexpr Symbol SYM = 10;
static std::atomic<OrderId> g_id{900000};

static OrderEvent new_order(Side side, Quantity qty,
                             double price = 100.0,
                             OrderId trader = 1) {
    OrderEvent ev{};
    ev.type            = OrderEvent::Type::NEW_ORDER;
    ev.order_type      = OrderType::LIMIT;
    ev.side            = side;
    ev.symbol          = SYM;
    ev.order_id        = g_id.fetch_add(1);
    ev.client_order_id = trader;
    ev.price           = double_to_price(price);
    ev.qty             = qty;
    ev.tif             = TimeInForce::GTC;
    return ev;
}
static OrderEvent cancel_ev(OrderId id, OrderId trader = 1) {
    OrderEvent ev{};
    ev.type            = OrderEvent::Type::CANCEL_ORDER;
    ev.symbol          = SYM;
    ev.order_id        = id;
    ev.client_order_id = trader;
    return ev;
}

struct RiskFixture : ::testing::Test {
    MatchingEngine    eng;
    RiskEngine::Limits lim;
    std::vector<OrderId> rejected;

    RiskFixture() {
        eng.on_reject([this](OrderId id, std::string_view) {
            rejected.push_back(id);
        });
    }

    RiskEngine make_risk() { return RiskEngine(eng, lim); }
};

// ── Max order qty ─────────────────────────────────────────────────────────────

TEST_F(RiskFixture, MaxQtyPass) {
    lim.max_order_qty = 100;
    auto risk = make_risk();
    auto ev = new_order(Side::BUY, 100);
    EXPECT_TRUE(risk.submit(ev));
}

TEST_F(RiskFixture, MaxQtyReject) {
    lim.max_order_qty = 100;
    auto risk = make_risk();
    risk.set_reject_callback([this](OrderId id, std::string_view) {
        rejected.push_back(id);
    });
    auto ev = new_order(Side::BUY, 101);
    EXPECT_FALSE(risk.submit(ev));
    EXPECT_EQ(rejected.size(), 1u);
}

TEST_F(RiskFixture, MaxQtyExact) {
    lim.max_order_qty = 50;
    auto risk = make_risk();
    EXPECT_TRUE(risk.submit(new_order(Side::BUY, 50)));
    risk.set_reject_callback([this](OrderId id, std::string_view) {
        rejected.push_back(id);
    });
    EXPECT_FALSE(risk.submit(new_order(Side::BUY, 51)));
    EXPECT_EQ(rejected.size(), 1u);
}

// ── Max notional ──────────────────────────────────────────────────────────────

TEST_F(RiskFixture, MaxNotionalPass) {
    // 100 qty @ 100.0 = notional 100 * 10^8 * 100 = 1e12
    lim.max_order_qty      = 1'000'000;
    lim.max_order_notional = static_cast<Price>(100) * PRICE_SCALE * 100 + 1;
    auto risk = make_risk();
    EXPECT_TRUE(risk.submit(new_order(Side::BUY, 100, 100.0)));
}

TEST_F(RiskFixture, MaxNotionalReject) {
    lim.max_order_qty      = 1'000'000;
    lim.max_order_notional = double_to_price(100.0) * 99;  // notional cap = 99 shares worth
    auto risk = make_risk();
    risk.set_reject_callback([this](OrderId id, std::string_view) {
        rejected.push_back(id);
    });
    EXPECT_FALSE(risk.submit(new_order(Side::BUY, 100, 100.0)));  // 100 shares > 99 cap
    EXPECT_EQ(rejected.size(), 1u);
}

TEST_F(RiskFixture, MaxNotionalZeroMeansUnlimited) {
    lim.max_order_notional = 0;
    lim.max_order_qty      = 1'000'000;
    auto risk = make_risk();
    // Should not reject regardless of notional
    EXPECT_TRUE(risk.submit(new_order(Side::BUY, 1000, 999.0)));
}

// ── Net position limit ────────────────────────────────────────────────────────

TEST_F(RiskFixture, NetPositionBuyAccumulates) {
    lim.max_net_position = 200;
    lim.max_order_qty    = 1000;
    auto risk = make_risk();

    EXPECT_TRUE(risk.submit(new_order(Side::BUY, 100)));  // pos = +100
    EXPECT_TRUE(risk.submit(new_order(Side::BUY, 100)));  // pos = +200 (at limit)
}

TEST_F(RiskFixture, NetPositionBuyBreaches) {
    lim.max_net_position = 200;
    lim.max_order_qty    = 1000;
    auto risk = make_risk();
    risk.set_reject_callback([this](OrderId id, std::string_view) {
        rejected.push_back(id);
    });

    EXPECT_TRUE(risk.submit(new_order(Side::BUY, 200)));  // pos = +200
    EXPECT_FALSE(risk.submit(new_order(Side::BUY, 1)));   // pos = +201 > limit
    EXPECT_EQ(rejected.size(), 1u);
}

TEST_F(RiskFixture, NetPositionSellBreaches) {
    lim.max_net_position = 100;
    lim.max_order_qty    = 1000;
    auto risk = make_risk();
    risk.set_reject_callback([this](OrderId id, std::string_view) {
        rejected.push_back(id);
    });

    EXPECT_TRUE(risk.submit(new_order(Side::SELL, 100)));  // pos = -100
    EXPECT_FALSE(risk.submit(new_order(Side::SELL, 1)));   // pos = -101 > limit
    EXPECT_EQ(rejected.size(), 1u);
}

TEST_F(RiskFixture, NetPositionBuySellNets) {
    lim.max_net_position = 100;
    lim.max_order_qty    = 1000;
    auto risk = make_risk();

    EXPECT_TRUE(risk.submit(new_order(Side::BUY,  100)));  // +100
    EXPECT_TRUE(risk.submit(new_order(Side::SELL,  50)));  // +50
    EXPECT_TRUE(risk.submit(new_order(Side::BUY,   50)));  // +100, at limit
}

// ── Cancel-to-trade ratio ─────────────────────────────────────────────────────

TEST_F(RiskFixture, CancelRatioBelowLimit) {
    lim.max_cancel_ratio = 2.0;   // 2 cancels per fill
    lim.max_order_qty    = 1000;
    auto risk = make_risk();
    risk.set_reject_callback([this](OrderId id, std::string_view) {
        rejected.push_back(id);
    });

    // Simulate 1 fill tracked externally
    Fill f{};
    f.aggressive_order_id = 1;
    f.passive_order_id    = 2;
    risk.on_fill(f);   // fills = 1 for both trader IDs

    // 2 cancels for trader 1: ratio = 2/1 = 2.0 (at limit, should pass)
    EXPECT_TRUE(risk.submit(cancel_ev(100, 1)));
    EXPECT_TRUE(risk.submit(cancel_ev(101, 1)));

    // 3rd cancel: ratio = 3/1 > 2.0 → new order should be rejected
    EXPECT_TRUE(risk.submit(cancel_ev(102, 1)));

    // Now try new order for trader 1 — ratio exceeded
    EXPECT_FALSE(risk.submit(new_order(Side::BUY, 10, 100.0, 1)));
    EXPECT_EQ(rejected.size(), 1u);
}

TEST_F(RiskFixture, CancelRatioZeroMeansUnlimited) {
    lim.max_cancel_ratio = 0.0;
    lim.max_order_qty    = 1000;
    auto risk = make_risk();
    // Many cancels should not block new orders
    for (int i = 0; i < 100; ++i) (void)risk.submit(cancel_ev(static_cast<OrderId>(i)));
    EXPECT_TRUE(risk.submit(new_order(Side::BUY, 1)));
}

// ── Cancel events always pass ─────────────────────────────────────────────────

TEST_F(RiskFixture, CancelAlwaysPassesRisk) {
    lim.max_order_qty = 1;
    auto risk = make_risk();
    // Cancels are not gated by qty/notional/position checks
    auto cev = cancel_ev(12345);
    EXPECT_TRUE(risk.submit(cev));
}

// ── Non-order events pass through ────────────────────────────────────────────

TEST_F(RiskFixture, ShutdownPassesThrough) {
    lim.max_order_qty = 1;
    auto risk = make_risk();
    OrderEvent ev{};
    ev.type = OrderEvent::Type::SHUTDOWN;
    EXPECT_TRUE(risk.submit(ev));
}
