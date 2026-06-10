#include "obm/FIXEncoder.hpp"
#include <cstring>
#include <cstdio>

namespace obm {

static char* write_str(char* pos, char* end, const char* s) noexcept {
    while (pos < end && *s) *pos++ = *s++;
    return pos;
}
static char* write_char(char* pos, char* end, char c) noexcept {
    if (pos < end) *pos++ = c;
    return pos;
}
static char* write_int64(char* pos, char* end, int64_t v) noexcept {
    char tmp[24];
    int n = std::snprintf(tmp, sizeof(tmp), "%lld", static_cast<long long>(v));
    if (n > 0 && pos + n < end) {
        std::memcpy(pos, tmp, static_cast<std::size_t>(n));
        pos += n;
    }
    return pos;
}

char* FIXEncoder::append_tag(char* pos, char* end,
                             int tag, const char* val) noexcept {
    pos = write_int64(pos, end, tag);
    pos = write_char(pos, end, '=');
    pos = write_str(pos, end, val);
    pos = write_char(pos, end, '\x01');
    return pos;
}
char* FIXEncoder::append_tag_int(char* pos, char* end,
                                 int tag, int64_t val) noexcept {
    pos = write_int64(pos, end, tag);
    pos = write_char(pos, end, '=');
    pos = write_int64(pos, end, val);
    pos = write_char(pos, end, '\x01');
    return pos;
}
char* FIXEncoder::append_price(char* pos, char* end,
                               int tag, Price price) noexcept {
    int64_t integer_part = price / PRICE_SCALE;
    int64_t frac_part    = price % PRICE_SCALE;
    pos = write_int64(pos, end, tag);
    pos = write_char(pos, end, '=');
    pos = write_int64(pos, end, integer_part);
    pos = write_char(pos, end, '.');
    // Write exactly 8 fractional digits with leading zeros
    char frac[9];
    std::snprintf(frac, sizeof(frac), "%08lld", static_cast<long long>(frac_part));
    pos = write_str(pos, end, frac);
    pos = write_char(pos, end, '\x01');
    return pos;
}

std::size_t FIXEncoder::encode_fill(const Fill& fill, char execType,
                                    char* out, std::size_t out_size) noexcept {
    char* pos = out;
    char* end = out + out_size - 1; // reserve for null

    pos = append_tag(pos, end, 8,  "FIX.4.2");      // BeginString
    pos = append_tag(pos, end, 35, "8");             // MsgType=ExecutionReport
    pos = append_tag_int(pos, end, 37, static_cast<int64_t>(fill.aggressive_order_id));
    pos = append_tag_int(pos, end, 17, static_cast<int64_t>(fill.fill_qty)); // ExecID reuse qty
    char et[2] = {execType, '\0'};
    pos = append_tag(pos, end, 150, et);             // ExecType
    pos = append_tag(pos, end, 39, "2");             // OrdStatus=Filled
    pos = append_price(pos, end, 31, fill.fill_price);
    pos = append_tag_int(pos, end, 32, static_cast<int64_t>(fill.fill_qty));
    pos = append_tag_int(pos, end, 60, static_cast<int64_t>(fill.timestamp_ns));

    if (pos >= end) return 0; // overflow
    *pos = '\0';
    return static_cast<std::size_t>(pos - out);
}

std::size_t FIXEncoder::encode_reject(OrderId order_id, const char* reason,
                                      char* out, std::size_t out_size) noexcept {
    char* pos = out;
    char* end = out + out_size - 1;

    pos = append_tag(pos, end, 8,  "FIX.4.2");
    pos = append_tag(pos, end, 35, "8");
    pos = append_tag_int(pos, end, 37, static_cast<int64_t>(order_id));
    pos = append_tag(pos, end, 39, "8");              // OrdStatus=Rejected
    pos = append_tag(pos, end, 103, reason);          // OrdRejReason

    if (pos >= end) return 0;
    *pos = '\0';
    return static_cast<std::size_t>(pos - out);
}

} // namespace obm
