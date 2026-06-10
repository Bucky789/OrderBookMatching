#include "obm/OrderBook.hpp"
#include "obm/MemoryPool.hpp"
#include "obm/Benchmark.hpp"
#include <benchmark/benchmark.h>
#include <random>

using namespace obm;

static constexpr Symbol SYM = 20;

static void BM_CancelBestOrder(benchmark::State& state) {
    MemoryPool<Order> pool(8);
    OrderBook book(SYM, pool);

    std::vector<OrderId> ids;
    OrderId next = 3'000'000;

    // Pre-populate
    for (int i = 0; i < 500; ++i) {
        OrderEvent ev{};
        ev.type = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side = Side::BUY;
        ev.tif  = TimeInForce::GTC;
        ev.symbol = SYM;
        ev.order_id = next++;
        ev.price = double_to_price(90.0 + i * 0.01);
        ev.qty   = 50;
        book.add_order(ev);
        ids.push_back(ev.order_id);
    }

    std::mt19937 rng(42);
    for (auto _ : state) {
        state.PauseTiming();
        // Add a fresh order
        OrderEvent ev{};
        ev.type = OrderEvent::Type::NEW_ORDER;
        ev.order_type = OrderType::LIMIT;
        ev.side = Side::BUY;
        ev.tif  = TimeInForce::GTC;
        ev.symbol = SYM;
        ev.order_id = next++;
        ev.price = double_to_price(95.0);
        ev.qty   = 50;
        book.add_order(ev);
        ids.push_back(ev.order_id);
        state.ResumeTiming();

        uint64_t t0 = rdtsc();
        book.cancel_order(ev.order_id); // O(1) via intrusive list
        uint64_t t1 = rdtsc();
        ids.pop_back();
        benchmark::DoNotOptimize(t1 - t0);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CancelBestOrder)->Iterations(500'000);

static void BM_MemoryPoolAllocFree(benchmark::State& state) {
    MemoryPool<Order> pool(4);
    for (auto _ : state) {
        Order* o = pool.allocate();
        benchmark::DoNotOptimize(o);
        pool.deallocate(o);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MemoryPoolAllocFree)->Iterations(5'000'000);
