#include "obm/FIXParser.hpp"
#include <cstring>
#include <cstdlib>
#include <functional>
#include <string_view>

namespace obm {

// Copy at most dst_max-1 chars and null-terminate.
static void safe_copy(char* dst, std::size_t dst_max,
                      const char* src, std::size_t src_len) noexcept {
    std::size_t n = (src_len < dst_max - 1) ? src_len : (dst_max - 1);
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

const char* FIXParser::find_tag(const char* buf, std::size_t len,
                                int tag, std::size_t& val_len) noexcept {
    const char* pos = buf;
    const char* end = buf + len;

    while (pos < end) {
        // Find '='
        const char* eq = static_cast<const char*>(
            std::memchr(pos, '=', static_cast<std::size_t>(end - pos)));
        if (!eq) break;

        // Parse tag number
        int t = 0;
        for (const char* p = pos; p < eq; ++p) {
            if (*p < '0' || *p > '9') { t = -1; break; }
            t = t * 10 + (*p - '0');
        }

        const char* val = eq + 1;
        const char* soh = static_cast<const char*>(
            std::memchr(val, '\x01', static_cast<std::size_t>(end - val)));
        if (!soh) soh = end;

        if (t == tag) {
            val_len = static_cast<std::size_t>(soh - val);
            return val;
        }
        pos = soh + 1;
    }
    val_len = 0;
    return nullptr;
}

int FIXParser::fast_atoi(const char* s, std::size_t len) noexcept {
    int result = 0;
    bool neg = (len > 0 && s[0] == '-');
    for (std::size_t i = neg ? 1 : 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9') break;
        result = result * 10 + (s[i] - '0');
    }
    return neg ? -result : result;
}

int64_t FIXParser::parse_price(const char* s, std::size_t len) noexcept {
    // "150.25" → 15025000000 (8 decimal places, no floating-point)
    int64_t integer_part = 0;
    int64_t frac_part    = 0;
    int     frac_digits  = 0;
    bool    in_frac      = false;

    for (std::size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c == '.') { in_frac = true; continue; }
        if (c < '0' || c > '9') break;
        if (in_frac) {
            if (frac_digits < 8) {
                frac_part = frac_part * 10 + (c - '0');
                ++frac_digits;
            }
        } else {
            integer_part = integer_part * 10 + (c - '0');
        }
    }
    // Pad fractional part to 8 decimal places
    while (frac_digits < 8) { frac_part *= 10; ++frac_digits; }

    return integer_part * PRICE_SCALE + frac_part;
}

bool FIXParser::parse(const char* buf, std::size_t len, FIXMessage& out) noexcept {
    std::memset(&out, 0, sizeof(out));
    out.valid = false;

    std::size_t vlen = 0;
    const char* v;

    // Tag 35: MsgType (required)
    v = find_tag(buf, len, 35, vlen);
    if (!v || vlen == 0) { out.error_msg = "missing tag 35"; return false; }
    out.msg_type = v[0];

    // Tag 34: MsgSeqNum
    v = find_tag(buf, len, 34, vlen);
    if (v) out.seq_num = fast_atoi(v, vlen);

    // Tag 49: SenderCompID
    v = find_tag(buf, len, 49, vlen);
    if (v) safe_copy(out.sender, sizeof(out.sender), v, vlen);

    // Tag 56: TargetCompID
    v = find_tag(buf, len, 56, vlen);
    if (v) safe_copy(out.target, sizeof(out.target), v, vlen);

    if (out.msg_type == 'D') {
        // NewOrderSingle
        v = find_tag(buf, len, 11, vlen);
        if (v) safe_copy(out.cl_ord_id, sizeof(out.cl_ord_id), v, vlen);

        v = find_tag(buf, len, 55, vlen);
        if (v) safe_copy(out.symbol, sizeof(out.symbol), v, vlen);

        v = find_tag(buf, len, 54, vlen);
        if (v) out.side = v[0];

        v = find_tag(buf, len, 40, vlen);
        if (v) out.ord_type = v[0];

        v = find_tag(buf, len, 59, vlen);
        if (v) out.tif = v[0];

        v = find_tag(buf, len, 38, vlen);
        if (v) out.order_qty = fast_atoi(v, vlen);

        v = find_tag(buf, len, 44, vlen);
        if (v) out.price = parse_price(v, vlen);

        v = find_tag(buf, len, 99, vlen);
        if (v) out.stop_price = parse_price(v, vlen);

        v = find_tag(buf, len, 111, vlen);
        if (v) out.max_floor = fast_atoi(v, vlen);

    } else if (out.msg_type == 'F') {
        // OrderCancelRequest
        v = find_tag(buf, len, 11, vlen);
        if (v) safe_copy(out.cl_ord_id, sizeof(out.cl_ord_id), v, vlen);

        v = find_tag(buf, len, 41, vlen);
        if (v) safe_copy(out.orig_cl_ord_id, sizeof(out.orig_cl_ord_id), v, vlen);

        v = find_tag(buf, len, 37, vlen);
        if (v) out.order_id = fast_atoi(v, vlen);
    }

    // Tag 112: TestReqID (used in TestRequest/Heartbeat)
    v = find_tag(buf, len, 112, vlen);
    if (v) safe_copy(out.test_req_id, sizeof(out.test_req_id), v, vlen);

    out.valid = true;
    return true;
}

OrderEvent FIXParser::to_order_event(const FIXMessage& msg,
                                     Symbol symbol_id) noexcept {
    OrderEvent ev{};
    ev.symbol = symbol_id;

    if (msg.msg_type == 'F') {
        ev.type     = OrderEvent::Type::CANCEL_ORDER;
        ev.order_id = static_cast<OrderId>(msg.order_id);
        return ev;
    }

    // NewOrderSingle
    ev.type     = OrderEvent::Type::NEW_ORDER;
    ev.order_id = static_cast<OrderId>(std::abs(
        static_cast<long long>(std::hash<std::string_view>{}(
            std::string_view(msg.cl_ord_id)))));
    ev.price      = msg.price;
    ev.stop_price = msg.stop_price;
    ev.qty        = static_cast<Quantity>(msg.order_qty);
    ev.visible_qty = static_cast<Quantity>(msg.max_floor);

    ev.side = (msg.side == '2') ? Side::SELL : Side::BUY;

    switch (msg.ord_type) {
        case '1': ev.order_type = OrderType::MARKET; break;
        case '3': ev.order_type = OrderType::STOP;   break;
        default:  ev.order_type = OrderType::LIMIT;  break;
    }
    if (msg.max_floor > 0 && ev.order_type == OrderType::LIMIT) {
        ev.order_type = OrderType::ICEBERG;
    }

    switch (msg.tif) {
        case '3': ev.tif = TimeInForce::IOC;
                  ev.order_type = OrderType::IOC; break;
        case '4': ev.tif = TimeInForce::FOK;
                  ev.order_type = OrderType::FOK; break;
        case '1': ev.tif = TimeInForce::GTC; break;
        default:  ev.tif = TimeInForce::DAY; break;
    }

    return ev;
}

} // namespace obm
