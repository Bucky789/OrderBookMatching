#include "obm/MatchingEngine.hpp"
#include "obm/MarketDataSimulator.hpp"
#include "obm/TickDataReplayer.hpp"
#include "obm/Benchmark.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace obm;

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  --events N      Number of synthetic events (default: 100000)\n"
              << "  --symbol N      Symbol ID (default: 1)\n"
              << "  --seed N        RNG seed (default: 42)\n"
              << "  --csv PATH      Replay from CSV file instead of synthetic sim\n"
              << "  --top N         Print top N book levels after each 10k events\n"
              << "  --bench         Print latency histogram at end\n"
              << "  --cpu-ghz F     CPU frequency for latency conversion (default: 3.5)\n";
}

int main(int argc, char** argv) {
    uint64_t n_events = 100'000;
    Symbol   symbol   = 1;
    uint32_t seed     = 42;
    std::string csv_path;
    int    top_n   = 5;
    bool   bench   = false;
    double cpu_ghz = 3.5;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--events") == 0 && i+1 < argc) n_events = std::stoull(argv[++i]);
        else if (std::strcmp(argv[i], "--symbol") == 0 && i+1 < argc) symbol = static_cast<Symbol>(std::stoul(argv[++i]));
        else if (std::strcmp(argv[i], "--seed") == 0 && i+1 < argc) seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (std::strcmp(argv[i], "--csv") == 0 && i+1 < argc) csv_path = argv[++i];
        else if (std::strcmp(argv[i], "--top") == 0 && i+1 < argc) top_n = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--bench") == 0) bench = true;
        else if (std::strcmp(argv[i], "--cpu-ghz") == 0 && i+1 < argc) cpu_ghz = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "--help") == 0) { print_usage(argv[0]); return 0; }
    }

    MatchingEngine engine;
    LatencyHistogram hist;
    uint64_t fill_count  = 0;
    uint64_t fill_volume = 0;

    engine.on_fill([&](const Fill& f) {
        ++fill_count;
        fill_volume += f.fill_qty;
    });

    auto t_start = std::chrono::steady_clock::now();

    if (!csv_path.empty()) {
        // CSV replay mode
        TickDataReplayer replayer(csv_path, symbol);
        uint64_t count = 0;
        while (!replayer.done()) {
            auto ev = replayer.next();
            if (!ev) break;
            uint64_t t0 = rdtsc();
            while (!engine.submit(*ev)) {}
            engine.run_once();
            uint64_t t1 = rdtsc();
            if (bench) hist.record(t1 - t0);
            ++count;
            if (top_n > 0 && count % 10000 == 0) {
                auto* book = engine.book_for(symbol);
                if (book) book->print_top(top_n, std::cout);
            }
        }
        std::cout << "Replayed " << count << " events from " << csv_path << "\n";
    } else {
        // Synthetic simulation mode
        SimConfig sim_cfg;
        sim_cfg.symbol = symbol;
        sim_cfg.seed   = seed;
        MarketDataSimulator sim(sim_cfg);

        for (uint64_t i = 0; i < n_events; ++i) {
            auto ev = sim.next_event();
            uint64_t t0 = rdtsc();
            while (!engine.submit(ev)) {}
            engine.run_once();
            uint64_t t1 = rdtsc();
            if (bench) hist.record(t1 - t0);

            if (top_n > 0 && i > 0 && i % 10000 == 0) {
                auto* book = engine.book_for(symbol);
                if (book) {
                    std::cout << "\n--- After " << i << " events ---\n";
                    book->print_top(top_n, std::cout);
                    std::cout << "  mid = " << price_to_double(
                        (book->best_bid() != INVALID_PRICE && book->best_ask() != INVALID_PRICE)
                        ? (book->best_bid() + book->best_ask()) / 2
                        : 0) << "\n";
                }
            }
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();

    auto stats = engine.stats();
    std::cout << "\n=== Stats ===\n"
              << "  Orders received:  " << stats.orders_received  << "\n"
              << "  Fills generated:  " << stats.fills_generated  << "\n"
              << "  Volume traded:    " << stats.volume_traded     << "\n"
              << "  Orders cancelled: " << stats.orders_cancelled  << "\n"
              << "  Elapsed:          " << elapsed_s << " s\n"
              << "  Throughput:       "
              << static_cast<uint64_t>(stats.orders_received / elapsed_s)
              << " orders/sec\n";

    if (bench) {
        std::cout << "\n";
        hist.print_report(std::cout, cpu_ghz);
    }

    return 0;
}
