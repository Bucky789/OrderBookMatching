#include "obm/AuctionCross.hpp"
#include <gtest/gtest.h>

using namespace obm;

static AuctionCross::AuctionOrder bid(OrderId id, double price, Quantity qty) {
    return {id, Side::BUY,
            price > 0 ? double_to_price(price) : INVALID_PRICE,
            qty};
}
static AuctionCross::AuctionOrder ask(OrderId id, double price, Quantity qty) {
    return {id, Side::SELL,
            price > 0 ? double_to_price(price) : INVALID_PRICE,
            qty};
}

// ── Basic cases ───────────────────────────────────────────────────────────────

TEST(AuctionCross, NoCrossWhenBidBelowAsk) {
    AuctionCross ac;
    ac.add(bid(1, 99.0, 100));
    ac.add(ask(2, 101.0, 100));
    auto fills = ac.cross();
    EXPECT_TRUE(fills.empty());
    EXPECT_EQ(ac.uncrossing_price(), INVALID_PRICE);
    EXPECT_EQ(ac.matched_volume(), 0u);
}

TEST(AuctionCross, ExactPriceMatch) {
    AuctionCross ac;
    ac.add(bid(1, 100.0, 50));
    ac.add(ask(2, 100.0, 50));
    auto fills = ac.cross();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].qty, 50u);
    EXPECT_EQ(fills[0].price, double_to_price(100.0));
    EXPECT_EQ(ac.matched_volume(), 50u);
}

TEST(AuctionCross, PartialFill) {
    AuctionCross ac;
    ac.add(bid(1, 100.0, 80));
    ac.add(ask(2, 100.0, 50));
    auto fills = ac.cross();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].qty, 50u);
    EXPECT_EQ(ac.matched_volume(), 50u);
}

TEST(AuctionCross, EmptySidesReturnNoFills) {
    AuctionCross ac;
    ac.add(bid(1, 100.0, 100));
    EXPECT_TRUE(ac.cross().empty());

    ac.clear();
    ac.add(ask(2, 100.0, 100));
    EXPECT_TRUE(ac.cross().empty());
}

// ── Volume maximization ───────────────────────────────────────────────────────

TEST(AuctionCross, SelectsPriceMaximizingVolume) {
    // Bids: 200 @ 102, 100 @ 100
    // Asks: 50 @ 98, 100 @ 100, 200 @ 103
    // Cum bid at 100: 200+100=300; cum ask at 100: 50+100=150 → matched=150
    // Cum bid at 102: 200;         cum ask at 102: 50+100=150 → matched=150 (tie)
    // Should match 150 either way.
    AuctionCross ac;
    ac.add(bid(1, 102.0, 200));
    ac.add(bid(2, 100.0, 100));
    ac.add(ask(3, 98.0,   50));
    ac.add(ask(4, 100.0, 100));
    ac.add(ask(5, 103.0, 200));
    auto fills = ac.cross();
    EXPECT_GE(ac.matched_volume(), 150u);
    EXPECT_NE(ac.uncrossing_price(), INVALID_PRICE);
}

TEST(AuctionCross, AllBidsAboveAllAsks_MaxVolume) {
    // Bids: 100 @ 105; Asks: 100 @ 95
    // Any price in [95, 105] would cross; volume = 100
    AuctionCross ac;
    ac.add(bid(1, 105.0, 100));
    ac.add(ask(2,  95.0, 100));
    auto fills = ac.cross();
    EXPECT_EQ(ac.matched_volume(), 100u);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].qty, 100u);
}

// ── Multiple orders same side ─────────────────────────────────────────────────

TEST(AuctionCross, MultipleBidsMatchOneAsk) {
    AuctionCross ac;
    ac.add(bid(1, 101.0, 30));
    ac.add(bid(2, 101.0, 30));
    ac.add(bid(3, 101.0, 30));
    ac.add(ask(4, 100.0, 90));
    auto fills = ac.cross();
    Quantity total = 0;
    for (auto& f : fills) total += f.qty;
    EXPECT_EQ(total, 90u);
    EXPECT_EQ(ac.matched_volume(), 90u);
}

TEST(AuctionCross, OneBidMatchesMultipleAsks) {
    AuctionCross ac;
    ac.add(bid(1, 105.0, 90));
    ac.add(ask(2, 100.0, 30));
    ac.add(ask(3, 100.0, 30));
    ac.add(ask(4, 100.0, 30));
    auto fills = ac.cross();
    Quantity total = 0;
    for (auto& f : fills) total += f.qty;
    EXPECT_EQ(total, 90u);
    EXPECT_EQ(ac.matched_volume(), 90u);
}

// ── Market orders ─────────────────────────────────────────────────────────────

TEST(AuctionCross, MarketBidAlwaysParticipates) {
    AuctionCross ac;
    ac.add({1, Side::BUY, INVALID_PRICE, 100});  // market bid
    ac.add(ask(2, 99.0, 100));
    auto fills = ac.cross();
    ASSERT_FALSE(fills.empty());
    Quantity total = 0;
    for (auto& f : fills) total += f.qty;
    EXPECT_EQ(total, 100u);
}

TEST(AuctionCross, MarketAskAlwaysParticipates) {
    AuctionCross ac;
    ac.add(bid(1, 101.0, 100));
    ac.add({2, Side::SELL, INVALID_PRICE, 100});  // market ask
    auto fills = ac.cross();
    ASSERT_FALSE(fills.empty());
    Quantity total = 0;
    for (auto& f : fills) total += f.qty;
    EXPECT_EQ(total, 100u);
}

// ── clear() ───────────────────────────────────────────────────────────────────

TEST(AuctionCross, ClearResetsState) {
    AuctionCross ac;
    ac.add(bid(1, 100.0, 50));
    ac.add(ask(2, 100.0, 50));
    ac.cross();
    EXPECT_EQ(ac.matched_volume(), 50u);

    ac.clear();
    EXPECT_EQ(ac.bid_count(), 0u);
    EXPECT_EQ(ac.ask_count(), 0u);
    EXPECT_EQ(ac.uncrossing_price(), INVALID_PRICE);
    EXPECT_EQ(ac.matched_volume(), 0u);
    EXPECT_TRUE(ac.cross().empty());
}

// ── Fill IDs correct ──────────────────────────────────────────────────────────

TEST(AuctionCross, FillIdsMatchOrderIds) {
    AuctionCross ac;
    ac.add(bid(42, 100.0, 10));
    ac.add(ask(99, 100.0, 10));
    auto fills = ac.cross();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].aggressive_id, 42u);
    EXPECT_EQ(fills[0].passive_id,    99u);
}
