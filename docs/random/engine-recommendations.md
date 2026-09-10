# Framebuffer GUI Engine — Recommendations

**Target:** EK-RA8D2 (R7KA8D2KFLCAC), Cortex-M33 core
**Panel:** 7" 1024×600, GLCDC input RGB565
**Status:** design recommendation, pre-implementation
**Date:** 2026-09-09

---

## 0. Summary

Build **dirty-rectangle tracking and nothing else**. Draw directly into
SDRAM. Defer the DMAC, scratch tiles, and every other optimisation until
measurement justifies them.

Estimated core engine footprint: **12–16 KiB** including two font atlases,
against LVGL's ≈150 KB of code plus ≈20 KB of fonts.

**The font and engine decisions are not where the flash budget comes from.**
See §1.

---

## 1. Budget context

Current cm33 image against a 256 KiB MRAM partition:

$$
\text{free} = 262{,}144 - 205{,}916 = 56{,}228\ \text{B} = 54.91\ \text{KiB}
$$

Target of ≤50% occupancy implies:

$$
\Delta = 205{,}916 - 131{,}072 = 74{,}844\ \text{B}
$$

Contribution by action:

| Action | Saves (B) | Share of $\Delta$ |
|---|---:|---:|
| Drop the second typeface | ~3,800 | 5.1% |
| 4 bpp → 1 bpp glyph bitmaps | ~6,000 | 8.0% |
| Both font actions together | ~10,000 | 13.4% |
| **Deleting LVGL blenders, gradients, shadows, arcs, theme** | **~65,000** | **86.6%** |

**Sequencing consequence:** do the LVGL deletions first and re-measure.
The engine work below is what you build *after* the cut lands, so that
what replaces LVGL stays small. It is not a substitute for the cut.

> **Units note.** Source material mixes KB and KiB (e.g. 56,228 B labelled
> "~56 KiB"). Normalise to bytes before sizing any decision.

---

## 2. Memory model

| Zone | Type | cm33 view | Holds |
|---|---|---|---|
| MRAM | Non-volatile, XIP | 0x020C0000, 256 KiB | `.text` + `.rodata` (fonts, tables) |
| SRAM | Internal fast RAM | 0x22100000, 640 KiB | `.data`, `.bss`, heap, stack |
| SDRAM | External, memory-mapped | 0x68000000, 64 MiB | framebuffer, IPC, large buffers |

**The scarce resource is MRAM, not SDRAM.** This inverts several
conclusions that would hold on a RAM-constrained target:

- Colour depth costs framebuffer bytes, which are not scarce. Drawing in
  12 colours costs the same *code* as drawing in 2.
- Glyph bitmaps are `const` and land in `.rodata`, so they compete
  directly with engine code.
- Regularity, not minimalism, saves flash. Sixteen ornaments drawn by one
  `panel_frame()` called four times cost one function; eight different
  ornaments cost eight.

Framebuffer arithmetic, for reference:

$$
1024 \times 600 \times 2\ \text{B} = 1{,}228{,}800\ \text{B} = 1.17\ \text{MiB}
$$

Double-buffered ≈ 2.4 MiB. This exceeds cm33 SRAM (640 KiB), so a
full-frame scratch buffer is impossible. Only a tiled or banded scratch
could fit — see §5.

---

## 3. Build now

### 3.1 Damage list

This is the entire optimisation. Everything else is contingent.

A full frame is 1.17 MiB of CPU stores. At 30 fps:

$$
1{,}228{,}800 \times 30 \approx 36.9\ \text{MB/s}
$$

That will dominate the cm33. But only the plots and readouts change per
frame; the chrome is static. Dirty tracking costs perhaps 1–2 KiB of code
and reduces per-frame cost by one to two orders of magnitude. No other
item on this list approaches that ratio.

Keep the structure trivial: a fixed-capacity array of rectangles, merged
when they overlap, with a "whole screen" fallback when the array
overflows. Region arithmetic (subtraction, XOR, proper region trees) is
not worth its code size here.

### 3.2 Two-frame rule for double buffering

With two buffers, each is one frame behind. A region dirtied in frame $n$
must also be repainted in frame $n+1$, because the buffer being drawn into
still holds frame $n-1$'s content there.

$$
\text{repaint}[n] = \text{damage}[n] \cup \text{damage}[n-1]
$$

Keep one previous damage list and take the union. Roughly twenty lines.

This is preferable to the alternatives — per-buffer bookkeeping, or
maintaining a clean "plate" and restoring chrome before each frame —
both of which cost more code and more bandwidth.

**Consequence for design:** static ornament is genuinely free. It is drawn
once per buffer at startup and never touched again. This is what makes the
corner brackets, index ticks and segmented headers affordable.

### 3.3 Primitive set

Six calls and one clip rectangle:

```
hline(x, y, w, colour)
vline(x, y, h, colour)
fill_rect(x, y, w, h, colour)
glyph_run(font, str, n, x, y, colour, tracking)
polyline(pts, n, colour)
clip_rect(x, y, w, h)
```

Notes:

- Fill runs with 32-bit stores (two RGB565 pixels per store). Halves the
  store count for a few lines of code.
- No blend path. See §4 — 1 bpp glyphs and flat fills never read the
  destination.
- `polyline` is the only primitive with a scattered access pattern. It is
  used exclusively by the four plots.

### 3.4 Access-pattern discipline

SDRAM rewards sequential bursts within a row and punishes strided access.
Relative costs:

| Primitive | Access pattern | Relative cost |
|---|---|---|
| Horizontal rule | one contiguous run | cheapest |
| Inverse-video bar | contiguous runs, no reads | cheap |
| Filled rect | $h$ contiguous runs | cheap per unit area |
| Glyph blit (1 bpp) | short runs of width $w$ | cheap |
| Vertical rule | one write per row, strided | ≈10–20× a horizontal rule of equal length |
| Diagonal polyline | scattered, one write per step | worst case |

**Design rules that follow:**

- Prefer full-width horizontal bands to columnar borders.
- Separate columns by monospace advance, not by vertical rules — a
  monospace grid needs no grid lines.
- Replace long vertical indicators with short edge ticks (e.g. a cutoff
  marker as two 8 px ticks rather than a 286 px line: 16 writes instead
  of 286 strided ones).
- Short vertical legs (corner brackets, index ticks, well limits) are
  fine; it is the long ones that hurt.

> **Caveat.** This ordering is derived from general SDRAM and Cortex-M
> behaviour, not from the RA8D2 datasheet or measurements on your board.
> The *ranking* should hold. The *magnitudes* are estimates. See §6.

---

## 4. Fonts

**Two Terminus bitmap atlases, 1 bpp, ASCII only.**

| File | Cell | Bytes/glyph | 95 glyphs | Used for |
|---|---|---:|---:|---|
| `ter-u20n` | 10×20 | 40 | 3,800 | labels, values, note names, list rows, tabs |
| `ter-u14n` | 8×14 | 14 | 1,330 | mode lines, axis labels, legends, captions |
| | | | **5,130** | |

Terminus is OFL-1.1 and ships as BDF — a plain-text format, a few lines of
hex per glyph. Write a build-time script that emits a C array plus a small
header. **No runtime rasteriser**: a TrueType path costs ≈40 KB of code to
produce glyphs you could have baked offline.

### 4.1 Why 1 bpp

Row cost at $d$ bits per pixel:

$$
\text{bytes/glyph} = \left\lceil \frac{w \cdot d}{8} \right\rceil \cdot h
$$

At 1 bpp this collapses to $\lceil w/8 \rceil \cdot h$.

Beyond the ~6 KB saved, 1 bpp **deletes the blend path from the blitter**
and removes one SDRAM read per lit pixel. Antialiasing buys most on curves
and diagonals; this interface is overwhelmingly axis-aligned strokes,
hairline rules and rectangular fills.

### 4.2 The byte boundary

$\lceil w/8 \rceil$ means **widths 1 through 8 all cost the same**. A 7-wide
cell is not cheaper than an 8-wide one. The cliff is at $w = 9$:

| Cell | Bytes/glyph | 95 glyphs |
|---|---:|---:|
| 8×14 | 14 | 1,330 |
| 8×16 | 16 | 1,520 |
| 8×20 | 20 | 1,900 |
| 10×20 | 40 | 3,800 |
| 12×24 | 72 | 6,840 |

If flash gets tight late, `ter-u16n` + `ter-u14n` is **2,850 B** — 44% less
— at the cost of smaller type. At 168 PPI a 16 px cell is ≈2.4 mm:
adequate at desk distance, marginal at a metre.

### 4.3 Why one typeface

- Kerning is already dropped. Free on a monospace; visibly wrong on a
  proportional face (`AV`, `To`, `Yo` gap at 20 px).
- A proportional face reintroduces per-glyph width lookup and variable
  advance accumulation in the layout code, paid once regardless of how
  little it is used.
- Fixed pitch collapses text extent to $\text{len} \times \text{advance}$
  — no atlas access needed to measure a string.

Hierarchy comes from five techniques that cost **zero** `.rodata`:
inverse video, intensity, tracking (blank columns added to the advance),
case, and alignment.

### 4.4 Open font items

- ~~**Zero shape.** Terminus's default `0` is unslashed...~~ **Corrected
  2026-09-09:** verified against `ter-u20n.bdf` — the default `0` **is
  slashed**, and `0` / `O` are cleanly distinct at both atlas sizes. No
  build flag needed. The earlier claim was from memory and was wrong.
- **Bold.** `ter-u20b` has identical metrics to `ter-u20n`, so it costs
  3,800 B of data and zero layout code. Do not add it now. The door stays
  open if the five techniques prove insufficient on the panel.
- **Plot axis labels.** Earlier drafts used a 9 px tier here. If 14 px is
  cramped in a 230 px-wide plot, the honest options are shorter labels or
  a third atlas — not silently shrinking the type.

---

## 5. Descriptor representation and animation

### 5.1 The governing split

**Descriptors describe static chrome. Animation is a C function pointer.**

Do not add keyframes, tweening, easing, or data-binding opcodes to the
descriptor language. That path is how LVGL reached ≈150 KB. The
descriptor's job is to avoid writing forty near-identical drawing
functions; it is not an engine.

```
static chrome   → byte-stream descriptor, interpreted once per buffer
dynamic region  → DYN opcode reserves a rect; a C function draws into it
```

The descriptor says *where*. C says *what*.

### 5.2 Encoding

A tagged **variable-length byte stream**, not an array of structs — a
struct array pads every entry to the size of its largest member.

| Op | Name | Operands | Size |
|---|---|---|---:|
| `0x01` | `RECT` | `x:u16 y:u16 w:u16 h:u16 c:u8` | 10 B |
| `0x02` | `HLINE` | `x:u16 y:u16 w:u16 c:u8` | 8 B |
| `0x03` | `VLINE` | `x:u16 y:u16 h:u16 c:u8` | 8 B |
| `0x04` | `TEXT` | `x:u16 y:u16 font:u8 c:u8 len:u8 bytes…` | 8+len |
| `0x05` | `CALL` | `tmpl:u16 dx:u16 dy:u16` | 7 B |
| `0x06` | `DYN` | `slot:u8 x:u16 y:u16 w:u16 h:u16` | 10 B |
| `0x00` | `END` | — | 1 B |

`c` is an **index into the four-entry palette**, not a colour value. One
byte instead of two, and retheming becomes free.

### 5.3 CALL is where the saving is

The panel frame — eight bracket rects, header block, tail rule, two
separators, three graticule lines — is one template invoked four times at
different origins:

$$
\underbrace{4 \times 120}_{\text{inlined}} = 480\ \text{B}
\qquad\text{vs}\qquad
\underbrace{120 + 4 \times 7}_{\text{template}} = 148\ \text{B}
$$

This is the same principle as §2's "regularity, not minimalism": identical
treatment across panels costs one template; eight different ornaments cost
eight.

Rough estimate for the whole signal-flow screen: **900–1,200 B** of
descriptor against perhaps 4 KB of emitted Thumb-2 for the equivalent
calls — roughly 3–4× on chrome.

> **Estimate, not measurement.** Derived from the opcode widths above.
> §7.3 remains the number that decides this.

**If it comes out large:** switch to `u8` coordinates *local to a
template*, with `CALL` supplying the `u16` origin. Panels are 242 px wide,
so x fits in a byte; this halves most operands. Do this second, after
measuring, not first.

### 5.4 The DYN mechanism

```c
typedef struct {
    uint16_t x, y, w, h;              /* from the descriptor */
    void   (*draw)(const rect_t *r, void *state);
    void    *state;
    uint8_t  dirty;
} dyn_slot_t;
```

On encountering `DYN`, the interpreter records the rect and does nothing
else. Per frame, walk the slot table; only slots with `dirty` set are
called. The static pass never runs again after the initial paint of each
buffer.

Keep `DYN` **opaque**. The moment a descriptor can express *"bind this
rect to parameter 12 scaled by 0.7"*, you have begun writing an
interpreter for a language, and it will not stop growing.

### 5.5 Animating the plots: update columns, not rectangles

Every plot on these screens is a single-valued function of $x$ — waveform,
filter response, envelope, scope trace. Exploit that.

Store, per plot, the previous vertical span of each column:

```c
typedef struct { uint8_t y0[230], y1[230]; } trace_state_t;   /* 460 B */
```

Per frame, for each column: compute the new span, compare against the
stored one, skip if unchanged, otherwise erase the old span and draw the
new.

$$
\text{full rect redraw} = 230 \times 232 = 53{,}360\ \text{px}
$$
$$
\text{column update} \approx 1{,}000\text{–}2{,}000\ \text{px}
$$

Roughly **30×**, and it eliminates the clear-then-redraw flash entirely.
SRAM cost is 460 B per plot — 1.8 KiB for four, against 640 KiB available.

Worst case is the sawtooth reset: three columns with ~180 px spans, which
are strided writes. Bounded and unavoidable.

### 5.6 Erasing without a background mask

The obvious approach — a 1 bpp mask of what lies underneath — costs 6.7 KiB
per plot. Don't. The graticule is three horizontal lines at known $y$, so
erasing a span is:

```c
for (int py = old_y0; py <= old_y1; py++)
    put(x, py, is_graticule(py) ? grat_colour(py) : BG);
```

`is_graticule` is three integer compares. No mask, no extra state, and it
stays correct if the graticule changes.

### 5.7 Invalidate per slot; do not run a global frame loop

The largest runtime saving available, and it costs nothing:

| Region | Update trigger |
|---|---|
| Scope / output trace | continuous, ~30 fps |
| Filter response | on encoder change only |
| Envelope | on encoder change only |
| Waveform | on oscillator change only |
| Readouts | on value change only |

In steady state **one** region animates, not four. Drive redraws from an
invalidation flag set by the parameter layer, never from a timer that
repaints everything. A synth is idle far more often than it is being
tweaked.

### 5.8 Interaction with double buffering

**The two-frame rule of §3.2 applies to dynamic slots as well.** A slot
that animated in frame $n$ must repaint in $n+1$, because the buffer now
being drawn into still holds older content there.

Concretely: `trace_state_t` must be **per buffer** — two copies, 920 B per
plot — or the erase step will target the wrong previous span. This is the
easiest bug to introduce in the whole design, and it produces intermittent
smearing that is hard to attribute after the fact.

---

## 6. Do not build yet

### 5.1 DMAC (~2 KiB)

Only useful once a scratch buffer exists to move. No scratch, no reason.

Worth keeping in mind: the DRW 2D peripheral is silicon you have already
paid for; what costs flash is the FSP driver plus LVGL GPU glue, which is
tens of KB. Declining that is correct. But the DMAC is a separate, much
cheaper door for bulk memory-to-memory transfer if §6.2 says you need one.

### 5.2 Tile / band scratch

The original case for this was scattered diagonal writes in the plots.
Sizing the actual workload undercuts it: a plot trace is a few hundred lit
pixels, so four plots at 30 fps is on the order of $10^4$ writes/second.
That is negligible.

**If measurement contradicts this**, build a **full-width band** sized to
the plot row, not a per-plot tile:

- A 150 px-wide tile into a 1024-wide frame is 180 separate 300 B
  transfers, each strided by 2,048 B. Gain ≈3–5×.
- A 1024×180 band is contiguous: one linear transfer, no striding.
  At RGB565 that is 360 KiB, which fits in the ~410 KiB of free SRAM.
- Half-height (1024×90, 180 KiB) gives two transfers with more headroom.

### 5.3 Framebuffer restructuring

It lives in SDRAM, GLCDC scans it directly. That is correct and it is not
your budget.

---

## 7. Measure before deciding

Three measurements, each of which independently closes a question. Run
them one at a time.

### 6.1 MPU attributes on `0x68000000`

Determine whether the SDRAM region is normal-cacheable-write-back or
device-nGnRE. If writes are buffered and the controller coalesces them,
scattered-write cost drops by roughly an order of magnitude and §6.2 is
closed permanently.

Also establish whether the cm33 has a D-cache enabled on that region. If
it does, a DMA- and GLCDC-visible framebuffer needs cache maintenance
before scan-out, which is its own complication and belongs in the design
from the start rather than bolted on.

### 6.2 Cycles to draw one plot direct into SDRAM

Instrument a single plot redraw — graticule, polyline, cursor brackets —
with the cycle counter. Compare against the frame budget.

**Decision rule:** if it fits comfortably, never build §6.2. "Don't write
it" is the best outcome available given the flash pressure.

### 6.3 GUI definition size, isolated from LVGL

This is the component that scales with screen count, and nothing measured
so far separates it. Table-driven descriptors in `.rodata` usually beat
emitted drawing calls, with the interpreter cost amortised across every
screen — but that is a general expectation, not a measurement of your
code. Get the number before committing to a representation.

---

## 8. Footprint estimate

| Component | Estimate |
|---|---:|
| Primitives + clipping | 3–4 KiB |
| 1 bpp glyph blit | ~1 KiB |
| Damage list + two-frame rule | 1–2 KiB |
| Descriptor interpreter + CALL | 2–4 KiB |
| Dynamic slot table + column-update trace | 1–2 KiB |
| Two Terminus atlases (`.rodata`) | 5.1 KiB |
| Screen descriptors, ~1 KiB each (`.rodata`) | 4–6 KiB |
| **Total** | **17–23 KiB** |

Against ≈150 KB LVGL code + ≈20 KB LVGL fonts.

These are estimates from the primitive set and font arithmetic above, not
from compiled output. Treat §8 as a target to validate, not a result.

---

## 9. Visual system (for reference)

The theme is a consequence of the constraints rather than a decoration on
top of them. Recorded here so the engine and the design stay consistent.

**Palette — four entries, green phosphor:**

| Role | Value |
|---|---|
| Background | `#050A06` |
| Dim | `#1B6238` |
| Mid | `#3FBF78` |
| Bright | `#7CFFB0` |

Green rather than amber: near the eye's photopic peak (~555 nm), and in
RGB565 the green channel has 6 bits where red and blue have 5 — so the
green ramp has twice the quantisation steps available if a fifth intensity
is ever needed.

**Emphasis ladder**, loudest first:

| Level | Treatment | Reserved for |
|---|---|---|
| 1 | Bright fill | Alerts only — at most one per screen |
| 2 | Bracket cursor + dim band + bright text | Focus (moves with the encoder) |
| 3 | Bright text, no fill | Active state, current values |
| 4 | Dim fill | Chrome, headers |
| 5 | Mid text | Labels |
| 6 | Faint | Empty / unassigned |

**Focus vs. state must be distinct.** With endless encoders the cursor
constantly sits on items that are not the current selection. Cursor takes
the bracket treatment; the loaded/active item takes a leading marker glyph
in a dedicated column. Conflating them makes the screen unable to express
a state the hardware produces continuously.

**Header bar treatments**, chosen by what is in the row:

| Treatment | Used when |
|---|---|
| Plain full fill | App chrome, selection, alert, default action |
| Segmented (broken at column boundaries) | The header sits above columns |
| Block + tail rule | A single label above a field or panel |

Segment breaks aligned to column pitch let the header rule the grid
horizontally while index ticks rule it vertically — neither requires a
vertical line.

**Bipolar value display — split well.** Two dim wells separated by a gap,
filled outward from the gap by sign, with the gap itself filled when the
value is exactly zero. The gap guarantees a minimum separation so the
smallest positive and smallest negative can never be confused, and it
gives *zero* a distinct representation separate from *unassigned*.

Three cell states must remain visually distinct:

1. Unassigned — faint `·`, no wells drawn
2. Assigned, amount zero — wells drawn, gap filled bright
3. Assigned, non-zero — wells drawn, one side filled

---

## 10. Open questions

| Item | Status |
|---|---|
| Plot axis label size (14 px vs. a third atlas) | Verify on panel |
| Dim-well weight at 4 px on the real display | Verify; may need dropping to faint |
| Mod matrix: 88 visible intersections vs. 32 slots | Needs a defined behaviour when the cursor is on an unassigned cell with no slots free |
| Save dialogue: destructive action is the highlighted default | Either default to *Save As New* or require a hold for overwrite |
| Per-buffer `trace_state_t` (§5.8) | Must be two copies; verify no smearing on hardware |
| Uppercase-only patch names | Consequence of the 40-cell character ribbon; confirm acceptable |

---

## 11. Provenance

Measured or verified in-session:

- Terminus BDF header: `FONTBOUNDINGBOX 10 20 0 -4`, OFL-1.1 notice —
  read from `ter-u20n.bdf`
- Departure Mono metrics: advance 7 @ 11 px, 14 @ 22 px — measured from
  `DepartureMono-1.500.otf`. Grid quantises to 7×14 or 14×28 with nothing
  between, which forces 6 destination columns and 8 visible sources on the
  mod matrix versus 8 and 11 for Terminus. This is why Terminus wins, and
  it is a layout argument rather than a byte-count one (6,650 B vs 5,130 B
  is only 2% of $\Delta$).
- Font byte arithmetic throughout: derived from
  $\lceil w \cdot d / 8 \rceil \cdot h \cdot n$

Corrections made after verification:

- §4.4 previously stated that Terminus's default `0` is unslashed. Checked
  against `ter-u20n.bdf`: it **is** slashed, and `0` / `O` are cleanly
  distinct at both sizes. No build flag required.

Implementation note found while building the HTML mockup:

- For a 10-wide cell at `bpr = 2`, the ten bits must be **left-aligned**
  in the 16-bit row word — `1 << (bpr*8 - 1 - x)`, not `1 << (w - 1 - x)`.
  The two expressions are identical when `w = 8`, so an 8-wide atlas will
  pass a test that a 10-wide atlas fails silently. Unit-test `ter-u20n`
  specifically.

Estimated, not measured:

- All SDRAM access-cost ratios (§3.4)
- All flash footprint figures in §8
- Descriptor size estimates in §5.3
- Column-update saving ratio in §5.5
- Frame bandwidth implications in §3.1

Everything in the second list should be treated as a hypothesis to test,
not a result to design against.
