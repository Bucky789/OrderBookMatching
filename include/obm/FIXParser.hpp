#pragma once

#include "Types.hpp"
#include <cstddef>
#include <cstring>

namespace obm {

// Parsed FIX 4.2 message fields (zero-heap, operates on caller buffer).
// Supports: 35=D (NewOrderSingle), 35=F (OrderCancelRequest).
struct FIXMessage {
    char msg_type;           // tag 35: 'D' | 'F' | '8'
    char sender[24];         // tag 49
    char target[24];         // tag 56
    int  seq_num;            // tag 34

    // NewOrderSingle (35=D)
    char     cl_ord_id[24];  // tag 11
    char     symbol[12];     // tag 55
    char     side;           // tag 54: '1'=buy '2'=sell
    char     ord_type;       // tag 40: '1'=market '2'=limit '3'=stop
    char     tif;            // tag 59: '0'=day '1'=GTC '3'=IOC '4'=FOK
    int64_t  price;          // tag 44 fixed-point
    int64_t  stop_price;     // tag 99 fixed-point
    int64_t  order_qty;      // tag 38
    int64_t  max_floor;      // tag 111 (iceberg peak)

    // CancelRequest (35=F)
    char     orig_cl_ord_id[24]; // tag 41
    int64_t  order_id;           // tag 37

    bool        valid;
    const char* error_msg;
};

class FIXParser {
public:
    // Parse FIX message in buf[0..len). Returns false on malformed input.
    // No heap allocation; result stored in out.
    [[nodiscard]] static bool parse(const char* buf, std::size_t len,
                                    FIXMessage& out) noexcept;

    // Convert parsed FIXMessage to OrderEvent for the matching engine.
    [[nodiscard]] static OrderEvent to_order_event(const FIXMessage& msg,
                                                   Symbol symbol_id) noexcept;

private:
    // Find value for tag in SOH-delimited buffer. Returns pointer to value,
    // sets val_len. Returns nullptr if tag not found.
    static const char* find_tag(const char* buf, std::size_t len,
                                int tag, std::size_t& val_len) noexcept;

    // Parse decimal string to integer (no floating point).
    static int fast_atoi(const char* s, std::size_t len) noexcept;

    // Parse price string like "150.25" → 15025000000 (8 decimal places).
    static int64_t parse_price(const char* s, std::size_t len) noexcept;
};

} // namespace obm
