"""Generate PROJECT_EXPLAINER.pdf — full technical explanation of the matching engine."""

from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import cm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Preformatted,
    Table, TableStyle, HRFlowable, KeepTogether
)
from reportlab.lib.enums import TA_LEFT, TA_CENTER
import os

OUTPUT = os.path.join(os.path.dirname(os.path.dirname(__file__)), "PROJECT_EXPLAINER.pdf")

# ── Styles ────────────────────────────────────────────────────────────────────
styles = getSampleStyleSheet()

title_style = ParagraphStyle("Title", parent=styles["Title"],
    fontSize=22, leading=28, spaceAfter=6, textColor=colors.HexColor("#1a1a2e"))

h1_style = ParagraphStyle("H1", parent=styles["Heading1"],
    fontSize=15, leading=20, spaceBefore=18, spaceAfter=6,
    textColor=colors.HexColor("#16213e"), borderPad=0)

h2_style = ParagraphStyle("H2", parent=styles["Heading2"],
    fontSize=12, leading=16, spaceBefore=12, spaceAfter=4,
    textColor=colors.HexColor("#0f3460"))

body_style = ParagraphStyle("Body", parent=styles["Normal"],
    fontSize=10, leading=15, spaceAfter=6, textColor=colors.HexColor("#222222"))

code_style = ParagraphStyle("Code",
    fontName="Courier", fontSize=8.5, leading=13,
    backColor=colors.HexColor("#f4f4f4"),
    leftIndent=12, rightIndent=12,
    spaceBefore=4, spaceAfter=4,
    textColor=colors.HexColor("#1a1a1a"),
    borderColor=colors.HexColor("#cccccc"),
    borderWidth=0.5, borderPad=6, borderRadius=3)

caption_style = ParagraphStyle("Caption", parent=body_style,
    fontSize=8.5, textColor=colors.HexColor("#555555"), spaceAfter=8)

def h1(text): return Paragraph(text, h1_style)
def h2(text): return Paragraph(text, h2_style)
def p(text):  return Paragraph(text, body_style)
def code(text):
    return Preformatted(text, code_style)
def hr():     return HRFlowable(width="100%", thickness=0.5,
                                color=colors.HexColor("#cccccc"), spaceAfter=6)
def sp(h=6):  return Spacer(1, h)

def table(data, col_widths=None, header=True):
    t = Table(data, colWidths=col_widths)
    style = [
        ("BACKGROUND", (0,0), (-1,0), colors.HexColor("#16213e")),
        ("TEXTCOLOR",  (0,0), (-1,0), colors.white),
        ("FONTNAME",   (0,0), (-1,0), "Helvetica-Bold"),
        ("FONTSIZE",   (0,0), (-1,-1), 9),
        ("ROWBACKGROUNDS", (0,1), (-1,-1),
         [colors.HexColor("#f9f9f9"), colors.white]),
        ("GRID",       (0,0), (-1,-1), 0.4, colors.HexColor("#cccccc")),
        ("VALIGN",     (0,0), (-1,-1), "TOP"),
        ("LEFTPADDING",(0,0), (-1,-1), 6),
        ("RIGHTPADDING",(0,0),(-1,-1), 6),
        ("TOPPADDING", (0,0), (-1,-1), 4),
        ("BOTTOMPADDING",(0,0),(-1,-1), 4),
    ]
    t.setStyle(TableStyle(style))
    return t

# ── Content ───────────────────────────────────────────────────────────────────
story = []

# Title
story += [
    Paragraph("High-Performance Order Book Matching Engine", title_style),
    Paragraph("Technical Explainer — C++20 Portfolio Project", ParagraphStyle(
        "Sub", parent=body_style, fontSize=11, textColor=colors.HexColor("#555555"),
        spaceAfter=4)),
    Paragraph("Targeting: Jane Street · Citadel · Two Sigma · Optiver", ParagraphStyle(
        "Sub2", parent=body_style, fontSize=9, textColor=colors.HexColor("#888888"),
        spaceAfter=2)),
    hr(), sp(4),
]

# ── 1. What is this ──────────────────────────────────────────────────────────
story += [
    h1("1. What Is This Project?"),
    p("An exchange matching engine is the software at the core of every electronic "
      "market — NYSE, CME, Binance. When you place an order, it enters an <b>order "
      "book</b> and is matched against other orders by the engine. This project "
      "implements that from scratch in C++20, emphasising production-level design: "
      "lock-free concurrency, sub-microsecond latency, and correct market "
      "microstructure semantics."),
    sp(),
]

# ── 2. Order Book ────────────────────────────────────────────────────────────
story += [
    h1("2. The Order Book"),
    p("Two sides: <b>bids</b> (buyers, sorted descending by price) and <b>asks</b> "
      "(sellers, sorted ascending by price)."),
    code(
        "ASKS:\n"
        "  101.00  qty=150\n"
        "  100.50  qty=100\n"
        "--- spread ---\n"
        "BIDS:\n"
        "   99.75  qty= 50\n"
        "   99.50  qty=100"
    ),
    p("<b>Best bid</b> = highest buy price. <b>Best ask</b> = lowest sell price. "
      "The gap between them is the <b>spread</b>. An order <i>crosses</i> when its "
      "price reaches or beats the opposite best — triggering a <b>fill</b> (trade)."),
    sp(),
]

# ── 3. Price-Time Priority ───────────────────────────────────────────────────
story += [
    h1("3. Price-Time Priority"),
    p("When two resting orders are at the same price, the one that arrived "
      "<b>earlier</b> fills first. Universal rule for continuous trading venues."),
    p("Implementation: each order gets a monotonic <font name='Courier'>sequence_num"
      "</font>. Lower number = higher priority at same price. Clock resolution is "
      "insufficient (orders arrive microseconds apart); sequence number is the "
      "tiebreaker."),
    sp(),
]

# ── 4. Fixed-Point Price ─────────────────────────────────────────────────────
story += [
    h1("4. Fixed-Point Price Representation"),
    code('using Price = int64_t;  // "150.25" → 15025000000  (8 decimal places)'),
    p("<b>Why not double?</b> Floating-point cannot exactly represent many decimals. "
      "<font name='Courier'>0.1 + 0.2 != 0.3</font> in IEEE 754. In a matching "
      "engine, price equality is critical — two orders at 100.00 must be at the "
      "exact same price level. Fixed-point integers are exact and comparable with "
      "<font name='Courier'>==</font>."),
    sp(),
]

# ── 5. Order Types ───────────────────────────────────────────────────────────
story += [
    h1("5. Order Types"),
    table([
        ["Type", "Behaviour", "Rests in Book?"],
        ["Limit",   "Specify price + qty. Matches if price crosses, else rests.", "Yes"],
        ["Market",  "No price — take whatever is available at any price.", "Never"],
        ["IOC",     "Limit price, cancel unfilled remainder immediately.", "Never"],
        ["FOK",     "All-or-nothing. Entire qty must fill or order rejected.", "Never"],
        ["Iceberg", "Show only a peak qty; reload from hidden reserve on exhaustion.", "Yes (peak only)"],
        ["Stop",    "Dormant until market crosses trigger price → converts to market.", "As stop trigger"],
    ], col_widths=[2.8*cm, 9.5*cm, 2.7*cm]),
    sp(8),
    h2("Iceberg Correctness Detail"),
    p("When the visible peak exhausts and reloads from reserve, the order receives "
      "a <b>new sequence number</b> and goes to the <b>back of the queue</b> at "
      "that price level. This is correct production behaviour. Naive implementations "
      "keep the original timestamp — wrong, giving the iceberg unfair priority over "
      "later-arriving orders."),
    h2("FOK Correctness Detail"),
    p("Perform a dry-run traversal of the passive side to count available liquidity "
      "<i>before</i> touching the book. If insufficient, reject immediately. Never "
      "partially execute then roll back."),
    sp(),
]

# ── 6. Matching Logic ────────────────────────────────────────────────────────
story += [
    h1("6. Matching Loop (The Hot Path)"),
    code(
        "while aggressive has remaining qty:\n"
        "    best_passive = opposite_side.begin()      // O(1)\n"
        "    if prices don't cross: break\n"
        "    trade_qty = min(aggressor.remaining,\n"
        "                   passive.executable_qty)\n"
        "    emit Fill(aggressor, passive, trade_qty, fill_price)\n"
        "    update quantities on both sides\n"
        "    if passive fully filled:\n"
        "        remove from book, free to pool\n"
        "    if price level now empty:\n"
        "        erase from map"
    ),
    p("<b>Fill price</b> = the passive order's limit price. The passive order "
      "set the price; the aggressor takes it."),
    sp(),
]

# ── 7. Data Structures ───────────────────────────────────────────────────────
story += [
    h1("7. Data Structures — Design Rationale"),
    h2("Order Book: std::map<Price, PriceLevel>"),
    p("Bids use <font name='Courier'>std::greater&lt;Price&gt;</font> comparator "
      "→ highest bid is <font name='Courier'>.begin()</font> → O(1) best bid. "
      "Asks use <font name='Courier'>std::less&lt;Price&gt;</font> → lowest ask "
      "is <font name='Courier'>.begin()</font> → O(1) best ask. "
      "Insert/delete a price level: O(log n), where n = distinct price levels "
      "(typically 10–50 in practice, not millions)."),
    h2("PriceLevel: Intrusive Doubly-Linked List"),
    p("Each <font name='Courier'>Order</font> carries <font name='Courier'>prev"
      "</font> and <font name='Courier'>next</font> pointers. Operations:"),
    table([
        ["Operation", "Complexity", "How"],
        ["push_back (new order)", "O(1)", "Append to tail"],
        ["pop_front (oldest fills)", "O(1)", "Advance head pointer"],
        ["remove(Order*) — cancel", "O(1)", "Use intrusive prev/next — no scan"],
    ], col_widths=[5*cm, 2.5*cm, 7.5*cm]),
    sp(8),
    p("Without intrusive pointers, cancellation would require O(n) scan of the "
      "level. With them: O(1)."),
    h2("Order Map: unordered_map<OrderId, Order*>"),
    p("O(1) average cancel lookup by ID. Combined with intrusive list: cancel "
      "is O(1) end-to-end."),
    sp(),
]

# ── 8. Memory Pool ───────────────────────────────────────────────────────────
story += [
    h1("8. Slab Allocator (Memory Pool)"),
    code(
        "T* allocate() {\n"
        "    T* obj    = free_head_;\n"
        "    free_head_ = *reinterpret_cast<T**>(obj);  // pop freelist\n"
        "    return obj;\n"
        "}\n"
        "void deallocate(T* obj) {\n"
        "    *reinterpret_cast<T**>(obj) = free_head_;  // push freelist\n"
        "    free_head_ = obj;\n"
        "}"
    ),
    p("<b>Why:</b> <font name='Courier'>new Order()</font> calls the OS allocator "
      "(mutex, bookkeeping overhead, cache-unfriendly metadata). At 4M+ orders/sec "
      "this is measurable latency. Instead: pre-allocate slabs of 4096 orders on "
      "startup, maintain a freelist of available slots. <b>Zero OS calls on the "
      "hot path after warmup.</b>"),
    p("The freelist pointer is stored in the first 8 bytes of each free object "
      "(same bytes that would hold <font name='Courier'>OrderId id</font> when live). "
      "Safe because the allocator owns those bytes when the object is free."),
    p("Benchmark: ~1 ns per alloc+free (vs ~50–200 ns for system malloc)."),
    sp(),
]

# ── 9. SPSC Ring Buffer ──────────────────────────────────────────────────────
story += [
    h1("9. SPSC Lock-Free Ring Buffer"),
    p("Single-Producer Single-Consumer queue between the gateway thread "
      "(FIX parsing) and the engine thread (matching). 65,536 slots × 64 bytes "
      "= 4 MB, heap-allocated."),
    code(
        "// Push — producer thread\n"
        "buffer_[head] = item;\n"
        "head_.store(next, memory_order_release);   // publish\n"
        "\n"
        "// Pop — consumer thread\n"
        "head_.load(memory_order_acquire);           // observe\n"
        "item = buffer_[tail];\n"
        "tail_.store(next, memory_order_release);   // consume"
    ),
    p("<b>acquire/release</b> form a happens-before guarantee. On x86 (TSO): "
      "no extra CPU fence instructions — just compiler reordering prevention. "
      "On ARM: emits <font name='Courier'>ldar</font>/<font name='Courier'>stlr"
      "</font>."),
    p("<b>False sharing prevention:</b> <font name='Courier'>head_</font> (written "
      "by producer) and <font name='Courier'>tail_</font> (written by consumer) "
      "are padded to separate 64-byte cache lines. Without padding, both threads "
      "would thrash the same cache line — ~10× latency penalty."),
    p("<b>Power-of-2 capacity:</b> index wrapping uses bitmask "
      "(<font name='Courier'>& MASK</font>) instead of modulo — one instruction "
      "vs divide."),
    p("<b>Heap allocation:</b> the ring buffer is 4 MB. Stored as a direct struct "
      "member, instantiating <font name='Courier'>MatchingEngine</font> on the "
      "stack would overflow. Heap-allocated via <font name='Courier'>unique_ptr"
      "</font>."),
    sp(),
]

# ── 10. FIX Protocol ─────────────────────────────────────────────────────────
story += [
    h1("10. FIX 4.2 Protocol"),
    p("Industry standard for order entry. SOH-delimited (ASCII 0x01):"),
    code("8=FIX.4.2|35=D|49=CLIENT|56=EXCHANGE|11=ORD001|55=AAPL|54=1|38=100|40=2|44=150.25|"),
    table([
        ["Tag", "Field", "Values"],
        ["35", "MsgType", "D=NewOrder, F=Cancel, 8=ExecutionReport"],
        ["54", "Side", "1=Buy, 2=Sell"],
        ["40", "OrdType", "1=Market, 2=Limit, 3=Stop"],
        ["44", "Price", "Decimal string e.g. '150.25'"],
        ["38", "OrderQty", "Integer quantity"],
        ["59", "TimeInForce", "0=Day, 1=GTC, 3=IOC, 4=FOK"],
        ["111", "MaxFloor", "Iceberg visible peak size"],
    ], col_widths=[1.5*cm, 4*cm, 9.5*cm]),
    sp(8),
    p("Parser: single O(n) pass over buffer, no heap allocation. Prices parsed "
      "digit-by-digit into fixed-point integers — never via "
      "<font name='Courier'>strtod</font>."),
    sp(),
]

# ── 11. Architecture ─────────────────────────────────────────────────────────
story += [
    h1("11. System Architecture"),
    code(
        "Gateway Thread                    Engine Thread\n"
        "──────────────                    ─────────────\n"
        "Parse FIX message\n"
        "    ↓\n"
        "Build OrderEvent (64 bytes)\n"
        "    ↓\n"
        "try_push ──→ SPSC Ring ─────────→ try_pop\n"
        "             (lock-free,          ↓\n"
        "              65536 slots,        process_event()\n"
        "              4 MB heap)          ↓\n"
        "                                 OrderBook::add_order()\n"
        "                                 ↓\n"
        "                                 match_limit / match_market\n"
        "                                 ↓\n"
        "                                 execute_fill() → Fill callback\n"
        "                                 ↓\n"
        "                                 trigger_stops()"
    ),
    p("<b>OrderEvent</b> is exactly 64 bytes (one cache line) — ring buffer "
      "stores events contiguously with no padding gaps between them."),
    p("<b>Single-threaded engine</b> is intentional: matching is inherently "
      "sequential. Production HFT engines (LMAX, many exchanges) use this model. "
      "Speed comes from eliminating latency per operation, not parallelism."),
    sp(),
]

# ── 12. Performance ──────────────────────────────────────────────────────────
story += [
    h1("12. Benchmark Results"),
    p("Measured on Intel Core i7, 16 cores @ 2918 MHz, Windows 11, MSVC Release /O2 /arch:AVX2:"),
    table([
        ["Operation", "Wall Time", "CPU Time", "Throughput"],
        ["Limit insert (no match)", "247 ns", "234 ns", "4.27M orders/sec"],
        ["Limit match (1 fill)", "287 ns", "281 ns", "3.56M orders/sec"],
        ["Cancel (O(1) intrusive)", "213 ns", "156 ns", "6.40M orders/sec"],
        ["Memory pool alloc+free", "1.19 ns", "< 1 ns", "~1B ops/sec"],
    ], col_widths=[6*cm, 2.5*cm, 2.5*cm, 4*cm]),
    sp(8),
    p("Cancel is faster than matching because it is O(1) hash lookup + pointer "
      "unlink, vs O(log n) book traversal for matching. Pool at ~1 ns confirms "
      "zero OS overhead — the compiler nearly elides it entirely in tight loops."),
    sp(),
]

# ── 13. Interview Q&A ────────────────────────────────────────────────────────
story += [
    h1("13. Likely Interview Questions"),
    h2("Why not a heap/priority queue for the order book?"),
    p("<font name='Courier'>std::priority_queue</font> doesn't support O(1) removal "
      "by arbitrary ID. Cancelling an order would require O(n) scan. The "
      "<font name='Courier'>std::map</font> + intrusive list gives O(log n) insert "
      "and O(1) cancel."),
    h2("Why single-threaded engine?"),
    p("Correctness. Matching is inherently sequential — you cannot parallelize it "
      "without complex locking that would cost more latency than it saves. Production "
      "systems like LMAX Disruptor and most exchange engines are single-threaded "
      "event loops. Speed comes from minimising work per operation."),
    h2("Why SPSC not MPSC?"),
    p("One connection per gateway thread. SPSC has zero CAS overhead — just two "
      "atomic loads/stores with acquire/release. MPSC requires a CAS on the push "
      "side. If multiple gateways are needed, each gets its own SPSC queue; the "
      "engine drains them round-robin."),
    h2("How does iceberg reload work?"),
    p("Visible peak fills → reload from reserve → assign new "
      "<font name='Courier'>sequence_num</font> → "
      "<font name='Courier'>push_back</font> to end of PriceLevel. Iceberg goes "
      "to back of queue — correct time priority. The original peak size is stored "
      "in the <font name='Courier'>stop_price</font> field (ICEBERG and STOP are "
      "mutually exclusive types, so the field is free)."),
    h2("What is the space complexity?"),
    p("O(N) where N = live orders. Each order = 128 bytes (2 cache lines). "
      "Price levels = O(P), P = distinct price points. Order map = O(N)."),
    h2("Why fixed-point and not double?"),
    p("IEEE 754 doubles cannot exactly represent most decimal fractions. "
      "<font name='Courier'>150.25</font> in binary floating-point is approximately "
      "150.24999999... or 150.25000000001. Two orders submitted at the same price "
      "could land in different price levels. Fixed-point integers are exact."),
    h2("What would you add for production?"),
    p("Pre-trade risk checks (position limits, credit), market data dissemination "
      "(multicast publisher), persistence/crash recovery (WAL), regulatory reporting, "
      "network stack (kernel bypass via DPDK/RDMA for sub-μs gateway latency), "
      "and a price-ladder (array-based) book for instruments with bounded price range."),
    sp(),
]

# ── 14. File Structure ───────────────────────────────────────────────────────
story += [
    h1("14. Repository Structure"),
    code(
        "include/obm/\n"
        "  Types.hpp          — Price (int64 fixed-pt), enums, Fill, OrderEvent\n"
        "  Order.hpp          — 128-byte struct, intrusive list ptrs, pool_next\n"
        "  PriceLevel.hpp     — FIFO queue, O(1) push/pop/remove\n"
        "  OrderBook.hpp      — Two-sided book, all 6 order types\n"
        "  MatchingEngine.hpp — Engine + SPSC gateway interface\n"
        "  RingBuffer.hpp     — Lock-free SPSC (heap, 4 MB)\n"
        "  MemoryPool.hpp     — Slab allocator, zero heap on hot path\n"
        "  FIXParser.hpp      — Zero-heap FIX 4.2 parser\n"
        "  FIXEncoder.hpp     — ExecutionReport encoder\n"
        "  MarketDataSimulator.hpp — Poisson arrivals, GBM mid-price\n"
        "  Benchmark.hpp      — rdtsc, LatencyHistogram, ScopedTimer\n"
        "\n"
        "src/                 — Implementations\n"
        "tests/               — 39 Google Tests (all passing)\n"
        "benchmarks/          — Google Benchmark microbenchmarks\n"
        "data/sample_ticks.csv — CSV tick data for replay mode"
    ),
    sp(),
    p("Build: <font name='Courier'>cmake --preset msvc-release &amp;&amp; "
      "cmake --build --preset release</font>"),
    p("Test:  <font name='Courier'>ctest --test-dir build/release "
      "--build-config Release</font>"),
    p("Run:   <font name='Courier'>obm_sim.exe --events 1000000 --bench</font>"),
]

# ── Build PDF ─────────────────────────────────────────────────────────────────
doc = SimpleDocTemplate(
    OUTPUT,
    pagesize=A4,
    leftMargin=2*cm, rightMargin=2*cm,
    topMargin=2*cm, bottomMargin=2*cm,
    title="Order Book Matching Engine — Technical Explainer",
    author="Claude",
)
doc.build(story)
print(f"PDF written to: {OUTPUT}")
