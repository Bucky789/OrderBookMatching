#pragma once

#include "Types.hpp"
#include <cstddef>

namespace obm {

// Encodes a FIX 4.2 ExecutionReport (35=8) into a caller-provided buffer.
// No heap allocation.
class FIXEncoder {
public:
    // Encode fill as ExecutionReport. Returns bytes written, 0 on overflow.
    static std::size_t encode_fill(const Fill& fill, char execType,
                                   char* out, std::size_t out_size) noexcept;

    // Encode rejection notice.
    static std::size_t encode_reject(OrderId order_id, const char* reason,
                                     char* out, std::size_t out_size) noexcept;

private:
    static char* append_tag(char* pos, char* end, int tag, const char* val) noexcept;
    static char* append_tag_int(char* pos, char* end, int tag, int64_t val) noexcept;
    static char* append_price(char* pos, char* end, int tag, Price price) noexcept;
};

} // namespace obm
