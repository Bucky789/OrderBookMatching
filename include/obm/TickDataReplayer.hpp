#pragma once

#include "Types.hpp"
#include <fstream>
#include <optional>
#include <string>

namespace obm {

// Replays orders from a CSV file.
// Format: type,side,price,qty,visible_qty,order_id
//   type:  L=limit M=market I=IOC F=FOK B=iceberg C=cancel
//   side:  B=buy S=sell (unused for cancel)
//   price: decimal like "100.25"
//   qty:   integer
//   visible_qty: integer (iceberg only, else 0)
//   order_id: integer
class TickDataReplayer {
public:
    explicit TickDataReplayer(const std::string& path, Symbol symbol = 1);

    // Returns next OrderEvent from file, or nullopt at EOF.
    std::optional<OrderEvent> next();

    // True if no more events.
    [[nodiscard]] bool done() const noexcept { return !file_.is_open() || file_.eof(); }
    [[nodiscard]] uint64_t replayed() const noexcept { return count_; }

private:
    std::ifstream file_;
    Symbol        symbol_;
    uint64_t      count_ = 0;
};

} // namespace obm
