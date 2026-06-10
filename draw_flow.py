import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

fig, ax = plt.subplots(figsize=(14, 18))
fig.patch.set_facecolor('#0d1117')
ax.set_facecolor('#0d1117')
ax.set_xlim(0, 10)
ax.set_ylim(0, 18)
ax.axis('off')

# Colors
BG       = '#0d1117'
BOX_DARK = '#161b22'
BOX_MID  = '#1f2937'
GREEN    = '#3fb950'
BLUE     = '#58a6ff'
ORANGE   = '#f0883e'
PURPLE   = '#bc8cff'
RED      = '#ff7b72'
GRAY     = '#8b949e'
WHITE    = '#e6edf3'
YELLOW   = '#e3b341'

def box(ax, x, y, w, h, color, label, sublabel=None, text_color=WHITE, fontsize=13):
    rect = FancyBboxPatch((x, y), w, h,
                          boxstyle="round,pad=0.08",
                          linewidth=1.5, edgecolor=color,
                          facecolor=BOX_MID)
    ax.add_patch(rect)
    ty = y + h/2 + (0.15 if sublabel else 0)
    ax.text(x + w/2, ty, label, ha='center', va='center',
            color=text_color, fontsize=fontsize, fontweight='bold',
            fontfamily='monospace')
    if sublabel:
        ax.text(x + w/2, y + h/2 - 0.22, sublabel, ha='center', va='center',
                color=GRAY, fontsize=9, fontfamily='monospace')

def arrow(ax, x1, y1, x2, y2, color=GREEN, label=None):
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle='->', color=color, lw=2.0))
    if label:
        mx, my = (x1+x2)/2, (y1+y2)/2
        ax.text(mx + 0.15, my, label, color=GRAY, fontsize=8.5,
                fontfamily='monospace', va='center')

def side_label(ax, x, y, text, color=GRAY):
    ax.text(x, y, text, color=color, fontsize=8.5, fontfamily='monospace', va='center')

# ── Title ──────────────────────────────────────────────────────────────────────
ax.text(5, 17.4, 'Order Book Matching Engine', ha='center', va='center',
        color=WHITE, fontsize=17, fontweight='bold', fontfamily='monospace')
ax.text(5, 17.0, 'C++20  ·  Lock-Free  ·  Sub-microsecond Latency', ha='center',
        color=GRAY, fontsize=10, fontfamily='monospace')

# ── [1] Market Data Simulator ──────────────────────────────────────────────────
box(ax, 2.5, 15.4, 5, 0.9, GREEN, 'MarketDataSimulator',
    'GBM price drift · 6 order types · configurable seed')

# ── arrow ──────────────────────────────────────────────────────────────────────
arrow(ax, 5, 15.4, 5, 14.65, GREEN, 'OrderEvent  64B = 1 cache line')

# ── [2] SPSC Ring Buffer ───────────────────────────────────────────────────────
box(ax, 2.5, 13.7, 5, 0.9, BLUE, 'SPSC Ring Buffer',
    'lock-free  ·  65536 slots  ·  4 MB  ·  no mutex')

arrow(ax, 5, 13.7, 5, 12.95, BLUE, 'zero-copy pop')

# ── [3] Matching Engine ────────────────────────────────────────────────────────
box(ax, 2.5, 12.0, 5, 0.9, PURPLE, 'MatchingEngine',
    'routes events · owns pool · manages books')

arrow(ax, 5, 12.0, 5, 11.25, PURPLE, 'add_order / cancel_order')

# ── [4] Order Book ─────────────────────────────────────────────────────────────
box(ax, 2.5, 10.3, 5, 0.9, ORANGE, 'OrderBook',
    'price-time priority · BidMap + AskMap · order_map_')

# ── Split into BID / ASK ───────────────────────────────────────────────────────
arrow(ax, 3.5, 10.3, 2.2, 9.45, ORANGE)
arrow(ax, 6.5, 10.3, 7.8, 9.45, ORANGE)

box(ax, 0.4, 8.5, 3.5, 0.9, GREEN,  'BID Side',
    'std::map<Price, PriceLevel, greater>')
box(ax, 6.1, 8.5, 3.5, 0.9, RED,    'ASK Side',
    'std::map<Price, PriceLevel, less>')

# ── Labels beside BID/ASK ─────────────────────────────────────────────────────
ax.text(0.45, 8.1, '↑ highest bid first', color=GREEN, fontsize=8, fontfamily='monospace')
ax.text(6.15, 8.1, '↓ lowest ask first',  color=RED,   fontsize=8, fontfamily='monospace')

# ── Price cross check ──────────────────────────────────────────────────────────
arrow(ax, 2.15, 8.5,  4.1, 7.65, YELLOW)
arrow(ax, 7.85, 8.5,  5.9, 7.65, YELLOW)

box(ax, 3.5, 6.85, 3, 0.75, YELLOW, 'Price Cross?',
    'bid ≥ ask → MATCH')

# ── YES / NO branches ─────────────────────────────────────────────────────────
arrow(ax, 4.0, 6.85, 2.8, 5.95, GREEN)
arrow(ax, 6.0, 6.85, 7.2, 5.95, GRAY)

ax.text(2.55, 6.38, 'YES', color=GREEN, fontsize=9, fontweight='bold', fontfamily='monospace')
ax.text(7.05, 6.38, 'NO',  color=GRAY,  fontsize=9, fontweight='bold', fontfamily='monospace')

box(ax, 1.0, 5.1, 3.5, 0.8, GREEN, 'execute_fill()',
    'deduct qty · fire callback · trigger stops')
box(ax, 5.5, 5.1, 3.5, 0.8, GRAY,  'place_in_book()',
    'push to PriceLevel FIFO')

# ── Fill callback ──────────────────────────────────────────────────────────────
arrow(ax, 2.75, 5.1, 2.75, 4.2, GREEN, 'Fill { price, qty, ids }')

box(ax, 0.8, 3.3, 3.9, 0.85, GREEN, 'Fill Callback',
    'fills_generated · volume_traded · stats')

# ── trigger_stops ──────────────────────────────────────────────────────────────
arrow(ax, 4.5, 5.5, 5.5, 5.5, ORANGE)
ax.text(4.55, 5.62, 'trigger_stops()', color=ORANGE, fontsize=8, fontfamily='monospace')

# ── Memory Pool ────────────────────────────────────────────────────────────────
box(ax, 6.5, 3.3, 3.1, 0.85, PURPLE, 'MemoryPool<Order>',
    '~1.3 ns alloc · slab · no malloc')

# ── Pool arrow from OrderBook ──────────────────────────────────────────────────
ax.annotate('', xy=(8.05, 4.15), xytext=(8.05, 10.3),
            arrowprops=dict(arrowstyle='->', color=PURPLE, lw=1.5,
                            connectionstyle='arc3,rad=0.0'))
ax.text(8.15, 7.3, 'allocate /\ndeallocate', color=PURPLE, fontsize=8,
        fontfamily='monospace', va='center')

# ── Latency stats bar at bottom ────────────────────────────────────────────────
ax.axhline(y=2.9, xmin=0.03, xmax=0.97, color=BOX_DARK, lw=1)

stats = [
    ('p50',  '73 ns',  GREEN),
    ('p99',  '293 ns', YELLOW),
    ('p999', '585 ns', ORANGE),
    ('throughput', '4M+ orders/sec', BLUE),
    ('alloc', '1.3 ns', PURPLE),
]
positions = [1.0, 2.9, 4.8, 6.5, 8.8]
for (label, val, color), xpos in zip(stats, positions):
    ax.text(xpos, 2.5, label, color=GRAY,  fontsize=8,  fontfamily='monospace', ha='center')
    ax.text(xpos, 2.1, val,   color=color, fontsize=10, fontfamily='monospace', ha='center', fontweight='bold')

ax.text(5, 1.65, '39 / 39 tests passing  ·  C++20  ·  MSVC Release', ha='center',
        color=GRAY, fontsize=9, fontfamily='monospace')

out_path = r'C:\Users\manth\OneDrive\Desktop\OrderBookMatching\flow_diagram.jpg'
plt.savefig(out_path, dpi=180, bbox_inches='tight',
            facecolor=BG, edgecolor='none')
plt.close()
print(f'Saved: {out_path}')
