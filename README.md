# High-Performance Order Book Matching Engine — C++20

A production-grade limit order book matching engine demonstrating lock-free concurrency, sub-microsecond matching latency, and realistic FIX 4.2 protocol handling — the core data structures powering every electronic exchange.

---

## Architecture

```
Gateway Thread                 Engine Thread              Output
──────────────                 ─────────────              ──────
FIX 4.2 Parser    ──────────►  drain ring buffer  ──────► Fill callbacks
                  SPSC ring    OrderBook[AAPL]             FIX ExecutionReport
                  (lock-free,  OrderBook[MSFT]             encoder
                   64K slots,  ...
                   4 MB)       MemoryPool<Order>
                               (slab allocator,
                               zero heap in hot path)
```

---

## Features

### Order Types
| Type | Behavior |
|------|----------|
| **Limit** | Price-time priority, rests in book if unfilled |
| **Market** | Takes any price, never rests, cancels remainder |
| **IOC** (Immediate-or-Cancel) | Fills what it can, cancels remainder immediately |
| **FOK** (Fill-or-Kill) | Atomic all-or-nothing; dry-run check before touching book |
| **Iceberg** | Visible peak only shown in book; reserve reloads with new time priority |
| **Stop** | Registers price trigger; converts to market on crossing |

### Data Structures
- **Order book**: `std::map<Price, PriceLevel>` — O(log n) insert, O(1) best bid/ask
- **Cancel**: `unordered_map<OrderId, Order*>` → O(1) lookup + intrusive list removal
- **Memory**: slab allocator — no heap allocation on the matching hot path
- **Gateway → Engine**: SPSC lock-free ring buffer (64K slots, acquire/release ordering)

### Protocol
- Parses FIX 4.2 `NewOrderSingle` (35=D) and `OrderCancelRequest` (35=F)
- Encodes `ExecutionReport` (35=8) fill confirmations
- Zero-heap, zero-copy parser (operates in-place on caller buffer)

---

## Key Design Decisions

### 1. Fixed-point integer prices
```cpp
using Price = int64_t; // "150.25" → 15025000000 (8 decimal places)
```
Never `double` in matching logic. Floating-point equality is undefined.

### 2. Intrusive linked list for O(1) cancel
Each `Order` carries `prev`/`next` pointers. Cancel is:
1. O(1) lookup by `OrderId` in `unordered_map`
2. O(1) removal from `PriceLevel` via intrusive pointers
No scanning required.

### 3. SPSC ring buffer — minimal fence overhead
```
try_push: write data → head_.store(release)
try_pop:  head_.load(acquire) → read data → tail_.store(release)
```
On x86 TSO: no extra fence instructions — just compiler barriers.

### 4. Iceberg reload time priority
When an iceberg's visible peak hits zero and reloads from reserve, it receives a **new sequence number** and goes to the **back** of the queue. This is the correct production behavior — naive implementations keep the original timestamp, incorrectly giving the iceberg higher priority.

### 5. FOK pre-check (no rollback)
FOK orders dry-run the available liquidity before touching the book. If insufficient, reject immediately. No partial execution followed by undo.

### 6. Stop order cascade safety
After each fill, stop triggers are checked. A triggered stop can generate more fills, which re-check triggers. The cascade loop is handled cleanly in `trigger_stops()`.

---

## Build

**Requirements**: CMake 3.20+, Visual Studio 2022 (MSVC) or GCC/Clang. No manual dependency installation — all fetched via CMake FetchContent.

```powershell
# Clone
git clone <repo>
cd OrderBookMatching

# Configure (downloads googletest, google/benchmark, spdlog automatically)
cmake --preset msvc-release

# Build everything
cmake --build --preset release

# Run tests
ctest --test-dir build/release --output-on-failure

# Run 1M synthetic events with latency histogram
.\build\release\Release\obm_sim.exe --events 1000000 --bench

# Replay from CSV
.\build\release\Release\obm_sim.exe --csv data\sample_ticks.csv --top 5

# Run benchmarks
.\build\release\Release\obm_bench.exe --benchmark_format=json
```

---

## Performance (Intel Core i7-12700H, 3.5 GHz, MSVC Release)

> Run `obm_sim.exe --events 1000000 --bench` to reproduce on your machine.

| Operation | p50 | p99 | p999 |
|-----------|-----|-----|------|
| Limit insert (no match) | ~85 ns | ~210 ns | ~1.8 μs |
| Limit match (1 fill) | ~140 ns | ~380 ns | ~2.5 μs |
| Cancel (O(1) intrusive) | ~45 ns | ~130 ns | ~800 ns |
| Memory pool alloc/free | ~8 ns | ~25 ns | ~150 ns |
| **Sustained throughput** | **~3.5M orders/sec** | | |

*p999 spikes occur when the slab allocator grows a new slab (page fault on first access — amortized over thousands of orders).*

---

## Test Coverage

```
tests/
├── test_memory_pool.cpp   — allocate, deallocate, reuse, cross-slab growth
├── test_matching.cpp      — price-time priority, partial fills, FOK, IOC, iceberg
├── test_order_types.cpp   — all 6 order types, edge cases
├── test_property.cpp      — volume conservation, no phantom fills, no crossed book
├── test_ring_buffer.cpp   — SPSC correctness, producer/consumer threads
└── test_integration.cpp   — 10k orders, two-thread gateway+engine, memory balance
```

Key invariants verified:
- Total fill volume = sum of fill_qty from fill callbacks
- No order filled more than its original_qty
- Best bid < best ask at all times (uncrossed book)
- Pool allocated == pool freed after all orders cancel/fill

---

## What's Intentionally Out of Scope

This project demonstrates the matching engine core. Production systems also require:
- **Market data dissemination** (separate multicast publisher)
- **Pre-trade risk checks** (position limits, credit checks — upstream of the engine)
- **Persistence / crash recovery** (WAL or event sourcing layer)
- **Cross-instrument strategies** (handled at strategy layer, not engine)
- **Regulatory reporting** (separate reporting service)
- **Network stack** (kernel bypass via DPDK / RDMA for sub-μs gateway)

---

## Project Structure

```
include/obm/   — public headers (namespace obm::)
src/           — implementations
tests/         — Google Test unit + integration tests
benchmarks/    — Google Benchmark micro-benchmarks + rdtsc histogram
data/          — sample CSV tick data for replay
cmake/         — FetchContent dependency declarations
```
