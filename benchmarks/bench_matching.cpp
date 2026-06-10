#include "obm/MatchingEngine.hpp"
#include "obm/MarketDataSimulator.hpp"
#include "obm/Benchmark.hpp"
#include <benchmark/benchmark.h>

using namespace obm;

static constexpr Symbol SYM = 10;

// Benchmark: limit order insertion with no match (pure book insertion path)
static void BM_LimitInsertNoMatch(benchmark::State& state) {
    MemoryPool<Order> pool(8);
    OrderBook book(SYM, pool);

    // Pre-populate with 200 resting bids so we're inserting into a non-empty book
    for (int i = 0; i < 100; ++i) {
        OrderEvent ev{};
        ev.type = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side = Side::BUY;
        ev.tif  = TimeInForce::GTC;
        ev.symbol = SYM;
        ev.order_id = static_cast<OrderId>(i + 900000);
        ev.price = double_to_price(90.0 + i * 0.01);
        ev.qty   = 100;
        book.add_order(ev);
    }

    OrderId next_id = 1'000'000;
    for (auto _ : state) {
        OrderEvent ev{};
        ev.type = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side = Side::SELL;
        ev.tif  = TimeInForce::GTC;
        ev.symbol = SYM;
        ev.order_id = next_id++;
        ev.price = double_to_price(110.0); // above bids — no match
        ev.qty   = 100;
        uint64_t t0 = rdtsc();
        book.add_order(ev);
        uint64_t t1 = rdtsc();
        benchmark::DoNotOptimize(t1 - t0);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("orders/sec");
}
BENCHMARK(BM_LimitInsertNoMatch)->Iterations(1'000'000);

// Benchmark: aggressive limit order that matches one resting order
static void BM_LimitMatchOneLevel(benchmark::State& state) {
    MemoryPool<Order> pool(8);

    OrderId next_id = 2'000'000;
    auto make_sell = [&]() -> OrderEvent {
        OrderEvent ev{};
        ev.type = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side = Side::SELL;
        ev.tif  = TimeInForce::GTC;
        ev.symbol = SYM;
        ev.order_id = next_id++;
        ev.price = double_to_price(100.0);
        ev.qty   = 10;
        return ev;
    };
    auto make_buy = [&]() -> OrderEvent {
        OrderEvent ev{};
        ev.type = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side = Side::BUY;
        ev.tif  = TimeInForce::GTC;
        ev.symbol = SYM + 1;
        ev.order_id = next_id++;
        ev.price = double_to_price(100.0);
        ev.qty   = 10;
        return ev;
    };

    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book(SYM + 1, pool);
        book.add_order(make_sell()); // pre-rest one sell
        state.ResumeTiming();

        uint64_t t0 = rdtsc();
        book.add_order(make_buy());  // aggressive buy — one fill
        uint64_t t1 = rdtsc();
        benchmark::DoNotOptimize(t1 - t0);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LimitMatchOneLevel)->Iterations(500'000);

// Benchmark: throughput — how many orders/sec can the engine sustain?
static void BM_EngineThroughput(benchmark::State& state) {
    MatchingEngine eng;

    MarketDataSimulator sim;
    constexpr int BATCH = 10'000;
    std::vector<OrderEvent> events;
    sim.generate_batch(events, BATCH);

    std::size_t idx = 0;
    uint64_t count = 0;
    for (auto _ : state) {
        if (idx >= events.size()) idx = 0;
        while (!eng.submit(events[idx++])) {}
        eng.run_once();
        ++count;
    }
    state.SetItemsProcessed(static_cast<int64_t>(count));
}
BENCHMARK(BM_EngineThroughput)->Iterations(2'000'000)->Unit(benchmark::kNanosecond);
