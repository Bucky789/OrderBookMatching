#pragma once

#include "Types.hpp"
#include <vector>

namespace obm {

// Batch auction cross: collect orders, find the price that maximizes traded
// volume, execute all crosses at that single price (the "uncrossing price").
//
// Algorithm — O(n log n):
// 1. Sort bids descending, asks ascending.
// 2. Build cumulative supply (asks) and demand (bids) curves.
// 3. Walk the curves to find the price where min(cum_bid, cum_ask) is largest.
// 4. Execute fills at that price; return unfilled remainders.
//
// Used for opening/closing auctions where continuous matching is suspended.
class AuctionCross {
public:
    struct AuctionOrder {
        OrderId  id;
        Side     side;
        Price    limit_price;  // INVALID_PRICE = market (always participates)
        Quantity qty;
    };

    struct AuctionFill {
        OrderId  aggressive_id;
        OrderId  passive_id;
        Price    price;
        Quantity qty;
    };

    // Submit an order into the auction book.
    void add(const AuctionOrder& o);

    // Clear all orders (e.g., after auction ends).
    void clear() noexcept;

    // Run the uncrossing algorithm. Returns fills and sets uncrossing_price_.
    // Orders not matched are left in bids_/asks_ with reduced qty.
    std::vector<AuctionFill> cross();

    [[nodiscard]] Price    uncrossing_price() const noexcept { return uncrossing_price_; }
    [[nodiscard]] Quantity matched_volume()   const noexcept { return matched_volume_;   }
    [[nodiscard]] std::size_t bid_count()     const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_count()     const noexcept { return asks_.size(); }

private:
    std::vector<AuctionOrder> bids_;
    std::vector<AuctionOrder> asks_;
    Price    uncrossing_price_ = INVALID_PRICE;
    Quantity matched_volume_   = 0;
};

} // namespace obm
