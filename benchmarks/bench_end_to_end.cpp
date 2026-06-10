#include "obm/MatchingEngine.hpp"
#include "obm/MarketDataSimulator.hpp"
#include "obm/Benchmark.hpp"
#include <benchmark/benchmark.h>
#include <thread>

using namespace obm;

// End-to-end: gateway thread pushes via SPSC → engine thread pops and matches.
// Measures full pipeline round-trip.
static void BM_EndToEnd(benchmark::State& state) {
    constexpr int BATCH = 50'000;

    MatchingEngine eng;
    MarketDataSimulator sim;
    std::vector<OrderEvent> events;
    sim.generate_batch(events, BATCH);

    uint64_t submitted = 0;
    for (auto _ : state) {
        state.PauseTiming();
        LatencyHistogram hist;
        state.ResumeTiming();

        // Producer + consumer in same thread for deterministic latency measurement
        for (int i = 0; i < 1000; ++i) {
            const auto& ev = events[submitted % BATCH];
            uint64_t t0 = rdtsc();
            while (!eng.submit(ev)) {}
            eng.run_once();
            uint64_t t1 = rdtsc();
            hist.record(t1 - t0);
            ++submitted;
        }

        state.PauseTiming();
        auto [p50, p95, p99, p999] = hist.percentiles();
        state.counters["p50_ns"]  = benchmark::Counter(static_cast<double>(p50)  / 3.5);
        state.counters["p99_ns"]  = benchmark::Counter(static_cast<double>(p99)  / 3.5);
        state.counters["p999_ns"] = benchmark::Counter(static_cast<double>(p999) / 3.5);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<int64_t>(submitted));
}
BENCHMARK(BM_EndToEnd)->Iterations(100)->Unit(benchmark::kMicrosecond);
