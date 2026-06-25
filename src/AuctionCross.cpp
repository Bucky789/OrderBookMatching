#include "obm/AuctionCross.hpp"
#include <algorithm>
#include <cassert>
#include <map>

namespace obm {

void AuctionCross::add(const AuctionOrder& o) {
    if (o.side == Side::BUY)  bids_.push_back(o);
    else                       asks_.push_back(o);
}

void AuctionCross::clear() noexcept {
    bids_.clear();
    asks_.clear();
    uncrossing_price_ = INVALID_PRICE;
    matched_volume_   = 0;
}

// ── Uncrossing algorithm ──────────────────────────────────────────────────────
//
// Build cumulative bid/ask curves over distinct price levels, then scan for
// the price P* where min(cum_bid(P*), cum_ask(P*)) is maximized.
//
// Tie-break: prefer the price closest to the midpoint of the range.
//
std::vector<AuctionCross::AuctionFill> AuctionCross::cross() {
    std::vector<AuctionFill> fills;
    uncrossing_price_ = INVALID_PRICE;
    matched_volume_   = 0;

    if (bids_.empty() || asks_.empty()) return fills;

    // ── 1. Aggregate qty at each price level ──────────────────────────────────
    // Market orders get INVALID_PRICE; treat as best possible.
    // Bids: placed at highest prices first → market bids at +∞ (use max finite bid).
    // Asks: placed at lowest prices first → market asks at -∞ (use min finite ask).

    // Map price → (total_bid_qty, total_ask_qty)
    std::map<Price, std::pair<Quantity, Quantity>> levels;

    Price max_bid_price = INVALID_PRICE;
    Price min_ask_price = std::numeric_limits<Price>::max();

    for (const auto& b : bids_) {
        if (b.limit_price != INVALID_PRICE)
            max_bid_price = (max_bid_price == INVALID_PRICE)
                ? b.limit_price
                : std::max(max_bid_price, b.limit_price);
    }
    for (const auto& a : asks_) {
        if (a.limit_price != INVALID_PRICE)
            min_ask_price = std::min(min_ask_price, a.limit_price);
    }

    // Market bids match at any ask price; market asks match at any bid price.
    // Derive effective reference price from the opposite side when only market
    // orders exist on one side.
    if (max_bid_price == INVALID_PRICE &&
        min_ask_price != std::numeric_limits<Price>::max()) {
        max_bid_price = min_ask_price;  // market bids "willing to pay" best ask
    }
    if (min_ask_price == std::numeric_limits<Price>::max() &&
        max_bid_price != INVALID_PRICE) {
        min_ask_price = max_bid_price;  // market asks "willing to sell" best bid
    }

    if (max_bid_price == INVALID_PRICE ||
        min_ask_price == std::numeric_limits<Price>::max()) {
        return fills;  // both sides all-market: no reference price
    }

    for (const auto& b : bids_) {
        Price p = (b.limit_price == INVALID_PRICE) ? max_bid_price : b.limit_price;
        levels[p].first += b.qty;
    }
    for (const auto& a : asks_) {
        Price p = (a.limit_price == INVALID_PRICE) ? min_ask_price : a.limit_price;
        levels[p].second += a.qty;
    }

    // ── 2. Build cumulative curves ────────────────────────────────────────────
    // cum_bid(P) = total buy qty willing to trade at price ≥ P (descending walk)
    // cum_ask(P) = total sell qty willing to trade at price ≤ P (ascending walk)
    //
    // Collect all distinct prices in ascending order.
    std::vector<Price> prices;
    prices.reserve(levels.size());
    for (const auto& [p, _] : levels) prices.push_back(p);

    const std::size_t N = prices.size();
    std::vector<Quantity> cum_bid(N, 0);
    std::vector<Quantity> cum_ask(N, 0);

    // cum_ask[i] = total ask qty at prices[0..i] (ascending sum)
    Quantity running = 0;
    for (std::size_t i = 0; i < N; ++i) {
        running += levels[prices[i]].second;
        cum_ask[i] = running;
    }

    // cum_bid[i] = total bid qty at prices[i..N-1] (descending sum, stored at index i)
    running = 0;
    for (std::size_t i = N; i-- > 0;) {
        running += levels[prices[i]].first;
        cum_bid[i] = running;
    }

    // ── 3. Find uncrossing price ──────────────────────────────────────────────
    // P* = price level that maximizes min(cum_bid[i], cum_ask[i]).
    // Tie-break: pick the price closest to the bid-ask midpoint.

    Quantity best_vol = 0;
    std::size_t best_idx = N;  // invalid sentinel

    for (std::size_t i = 0; i < N; ++i) {
        Quantity vol = std::min(cum_bid[i], cum_ask[i]);
        if (vol > best_vol ||
            (vol == best_vol && best_idx != N &&
             std::abs(prices[i] - (max_bid_price + min_ask_price) / 2) <
             std::abs(prices[best_idx] - (max_bid_price + min_ask_price) / 2))) {
            best_vol = vol;
            best_idx = i;
        }
    }

    if (best_idx == N || best_vol == 0) return fills;

    uncrossing_price_ = prices[best_idx];
    matched_volume_   = best_vol;

    // ── 4. Execute fills ──────────────────────────────────────────────────────
    // All bids with limit_price >= uncrossing_price_ and all asks with
    // limit_price <= uncrossing_price_ participate, in FIFO order.

    std::vector<AuctionOrder*> eligible_bids, eligible_asks;
    for (auto& b : bids_) {
        Price p = (b.limit_price == INVALID_PRICE) ? max_bid_price : b.limit_price;
        if (p >= uncrossing_price_) eligible_bids.push_back(&b);
    }
    for (auto& a : asks_) {
        Price p = (a.limit_price == INVALID_PRICE) ? min_ask_price : a.limit_price;
        if (p <= uncrossing_price_) eligible_asks.push_back(&a);
    }

    // Match bid-by-bid against asks, all at uncrossing_price_.
    std::size_t ai = 0;
    for (auto* bp : eligible_bids) {
        while (bp->qty > 0 && ai < eligible_asks.size()) {
            AuctionOrder* ap = eligible_asks[ai];
            if (ap->qty == 0) { ++ai; continue; }
            Quantity qty = std::min(bp->qty, ap->qty);
            fills.push_back({bp->id, ap->id, uncrossing_price_, qty});
            bp->qty -= qty;
            ap->qty -= qty;
            if (ap->qty == 0) ++ai;
        }
        if (ai >= eligible_asks.size()) break;
    }

    return fills;
}

} // namespace obm
