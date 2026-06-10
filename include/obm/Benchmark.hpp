#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <tuple>

namespace obm {

// rdtsc timestamp — falls back to std::chrono on non-x86.
inline uint64_t rdtsc() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    return __rdtsc();
#else
    #include <chrono>
    return static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
#endif
}

// Latency histogram with log2 bucketing (64 buckets cover 1 cycle to ~10^19).
// Bucket i holds samples where 2^i <= cycles < 2^(i+1).
class LatencyHistogram {
    std::array<uint64_t, 64> buckets_{};
    uint64_t count_ = 0;
    uint64_t sum_   = 0;
    uint64_t min_   = UINT64_MAX;
    uint64_t max_   = 0;

    static int log2_floor(uint64_t v) noexcept {
        if (v == 0) return 0;
        int b = 0;
        while (v >>= 1) ++b;
        return b;
    }

public:
    void record(uint64_t cycles) noexcept {
        int bucket = log2_floor(cycles);
        if (bucket > 63) bucket = 63;
        ++buckets_[static_cast<std::size_t>(bucket)];
        ++count_;
        sum_ += cycles;
        if (cycles < min_) min_ = cycles;
        if (cycles > max_) max_ = cycles;
    }

    // Returns {p50, p95, p99, p999} in cycles.
    [[nodiscard]] std::tuple<uint64_t,uint64_t,uint64_t,uint64_t>
    percentiles() const noexcept {
        if (count_ == 0) return {0,0,0,0};
        uint64_t targets[4] = {
            count_ / 2,
            count_ * 95 / 100,
            count_ * 99 / 100,
            count_ * 999 / 1000
        };
        uint64_t results[4] = {};
        uint64_t cum = 0;
        int ri = 0;
        for (int b = 0; b < 64 && ri < 4; ++b) {
            cum += buckets_[static_cast<std::size_t>(b)];
            while (ri < 4 && cum >= targets[ri]) {
                results[ri] = (1ull << b); // midpoint of bucket
                ++ri;
            }
        }
        return {results[0], results[1], results[2], results[3]};
    }

    void print_report(std::ostream& os, double cpu_ghz = 3.5) const {
        auto [p50, p95, p99, p999] = percentiles();
        double ghz = cpu_ghz;
        auto to_ns = [&](uint64_t c) { return static_cast<double>(c) / ghz; };

        os << "Latency Report (" << count_ << " samples, CPU " << ghz << " GHz)\n"
           << "  min:  " << to_ns(min_)  << " ns\n"
           << "  p50:  " << to_ns(p50)   << " ns\n"
           << "  p95:  " << to_ns(p95)   << " ns\n"
           << "  p99:  " << to_ns(p99)   << " ns\n"
           << "  p999: " << to_ns(p999)  << " ns\n"
           << "  max:  " << to_ns(max_)  << " ns\n"
           << "  mean: " << (count_ > 0 ? to_ns(sum_ / count_) : 0.0) << " ns\n";
    }

    void reset() noexcept {
        buckets_ = {};
        count_ = sum_ = 0;
        min_ = UINT64_MAX; max_ = 0;
    }
};

// RAII timing guard: records elapsed cycles to dest on destruction.
struct ScopedTimer {
    uint64_t& dest;
    uint64_t  start;
    explicit ScopedTimer(uint64_t& d) : dest(d), start(rdtsc()) {}
    ~ScopedTimer() noexcept { dest = rdtsc() - start; }
};

} // namespace obm
