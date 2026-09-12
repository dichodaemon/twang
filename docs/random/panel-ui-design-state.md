---
title: Panel UI — Design State and Handoff
date: 2026-09-12
status: visual design settled; three screens rendered at 1:1 but not wired;
        interaction model NOT designed (see §10) — do that before building
---

# Panel UI — Design State and Handoff

## 0. How to use this document

This is a handoff, not a specification. §1–§5 are settled decisions with their
reasoning; §6 is what exists in code; §7 is designed-but-unbuilt; §8 is open.

If you are picking this up cold, read §1 (the constraint that shapes
everything), then §4 (the emphasis ladder, which is the part most easily got
wrong), then §10 and §11.

The authoritative constraint document is
`docs/random/engine-recommendations.md`. This document does not repeat its
reasoning; it records what the UI decided as a consequence.

**Curation note.** `engine-recommendations.md` §9 ("Visual system") is an
earlier, shorter copy of the palette, emphasis ladder, header treatments and
split-well encoding. **This document supersedes it.** Where they disagree, this
one is current — §9 there predates the focus-versus-state distinction and the
1:1 renders. Either delete that section or replace it with a pointer here,
before the two drift further.

Also worth fixing while you are in that file: §6 and §7 contain subsections
numbered `5.1-5.3` and `6.1-6.3`, left over from an earlier insertion. The
references in this document use the *section* numbers, which are correct.

**Look at the renders before reading §9**, and read §10 before treating any
of them as an interaction reference. `tools/mockup_screens.cc` draws the
three unbuilt screens through the real primitives and the real atlases, so the
PNGs are what the panel will actually show. Build and run it first:

```
cmake --build build --target mockup_screens
./build/mockup_screens build/mockups 2    # 2x for pixel inspection
```

---

## 1. The constraint that shapes everything

The panel is drawn by an immediate-mode framebuffer renderer on a Cortex-M33
with a 256 KiB flash partition, into a 1024×600 RGB565 buffer in external
SDRAM. There is no 2D acceleration — every pixel is a CPU store.

Two consequences drive the whole visual language:

**Access pattern is a design input, not an implementation detail.** SDRAM
rewards contiguous runs along a scanline and punishes strided access. A
horizontal rule is one burst; a vertical rule is one write per row, roughly an
order of magnitude worse per pixel. A diagonal is worse still.

**Regularity saves flash, minimalism does not.** Sixteen rivets drawn by one
`panel_frame()` called four times cost one function. Eight different ornaments
cost eight. Ornament is cheap when it repeats.

### Relative primitive cost

From `engine-recommendations.md` §3.4. The *ordering* is reliable; the
magnitudes are estimated from general SDRAM behaviour, not measured on target.

| Primitive | Access pattern | Relative cost |
|---|---|---|
| Horizontal rule | one contiguous run | cheapest |
| Inverse-video bar | contiguous runs, no reads | cheap |
| Filled rect | *h* contiguous runs | cheap per unit area |
| Glyph blit (1 bpp) | short runs of width *w* | cheap |
| Vertical rule | one write per row, strided | ~10-20x a horizontal rule of equal length |
| Diagonal polyline | scattered, one write per step | worst case |

### The design rules that follow

These are not style preferences. Each one is the cheap option:

- **Prefer full-width horizontal bands to columnar borders.**
- **Separate columns by monospace advance, not by vertical rules** — a
  monospace grid needs no grid lines. The matrix's 96 cells cost zero rules;
  eight dividers at ~340 px each would be ~2,700 strided writes for decoration.
- **Replace long vertical indicators with short edge ticks.** The filter's
  cutoff marker is two 8 px ticks, not a 286 px line: 16 writes instead of 286
  strided ones.
- **Short vertical legs are fine.** Corner brackets, index ticks and split-well
  limits are all under 10 px. It is the long verticals that hurt.

This is why the design looks the way it does. It is not a retro pastiche that
happens to be cheap; it is the cheap thing, which happens to look like a
terminal.

---

## 2. Type

**One monospace face, two sizes, 1 bit per pixel.**

| File | Cell | Bytes/glyph | 95 glyphs | Used for |
|---|---|---:|---:|---|
| `ter-u20n.bdf` | 10×20 | 40 | 3,800 | labels, values, note names, list rows, tabs |
| `ter-u14n.bdf` | 8×14 | 14 | 1,330 | mode lines, axis labels, legends, captions |
| | | | **5,130** | |

Terminus, OFL-1.1, in `assets/fonts/`. Converted by `tools/bdf_to_c.py` at
build time — no runtime rasteriser.

### Why these choices

- **One face.** Kerning is dropped, which is free on a monospace and visibly
  wrong on a proportional face. Fixed pitch also collapses text extent to
  `len × advance`, so no atlas access is needed to measure a string.
- **1 bpp.** Deletes the blend path from the blitter and removes a destination
  read per lit pixel. Antialiasing buys most on curves; this interface is
  nearly all axis-aligned strokes.
- **Two sizes, not one.** The hierarchy that a second typeface would provide.
- **Terminus over Departure Mono.** Departure Mono's grid quantises to 7×14 or
  14×28 with nothing between, which would force 6 destination columns and 8
  visible sources on the matrix instead of 8 and 11. A layout argument, not a
  byte-count one — the two are within 1.5 KB.

### Byte-boundary note

Row cost is `⌈w/8⌉` bytes, so **widths 1–8 all cost the same**. A 7-wide cell
is not cheaper than an 8-wide one. The cliff is at 9.

| Cell | 95 glyphs |
|---|---:|
| 8×14 | 1,330 |
| 8×16 | 1,520 |
| 10×20 | 3,800 |
| 12×24 | 6,840 |

If flash gets tight, `ter-u16n` + `ter-u14n` is 2,850 B — 44% less, at the cost
of smaller type. At 168 PPI a 16 px cell is ≈2.4 mm: fine at desk distance,
marginal at a metre.

### Verified from the file, not from memory

Terminus's default `0` **is** slashed, and `0`/`O` are cleanly distinct at both
sizes. An earlier version of this claim was wrong.

### Implementation trap

For a 10-wide cell at `bpr = 2`, the ten bits must be **left-aligned** in the
16-bit row word — `1 << (bpr*8 - 1 - x)`, not `1 << (w - 1 - x)`. The two are
identical when `w = 8`, so an 8-wide atlas passes a test a 10-wide atlas fails
silently. `bdf_to_c.py` avoids this by copying BDF row bytes verbatim (BDF is
already left-aligned), and `--check` round-trips against a reference.

---

## 3. Palette

Four entries. Green phosphor.

| Role | Value |
|---|---|
| Background | `#050A06` |
| Dim | `#1B6238` |
| Mid | `#3FBF78` |
| Bright | `#7CFFB0` |

Green rather than amber for two reasons: it sits near the eye's photopic peak
(~555 nm), and RGB565 gives green 6 bits where red and blue get 5 — so the
green ramp has twice the quantisation steps if a fifth intensity is ever
needed.

Colour is *not* a constraint. The framebuffer is not the scarce resource; a
28-entry palette would be 56 B of `.rodata`. Four entries is a legibility
choice, not a budget one.

A broader 8-hue × 3-level palette exists in `palette.yaml` (design exploration,
not adopted) if a future screen needs semantic colour.

---

## 4. The emphasis ladder

**This is the part most easily got wrong.** With endless encoders the cursor
constantly sits on items that are not the current selection, so focus and state
must be visually distinct.

| Level | Treatment | Reserved for |
|---|---|---|
| 1 (loudest) | Bright fill | **Alerts only — at most one per screen** |
| 2 | Bracket cursor + dim band + bright text | Focus (moves with the encoder) |
| 3 | Bright text, no fill | Active state, current values |
| 4 | Dim fill | Chrome, headers |
| 5 | Mid text | Labels |
| 6 (quietest) | Faint | Empty / unassigned |

### Focus versus state

- **Focus** — where the encoder is now. Bracket cursor, dim band, text lifted
  to bright. Transient.
- **Loaded / active** — the current selection. A leading marker glyph in a
  dedicated column. Persistent, changes only on push.

Conflating them makes the screen unable to express a state the hardware
produces continuously. The patch browser learned this the hard way: an early
version used full inverse for the selected row, which was correct only for the
instant before the encoder moved.

### Consequences

The title bar is **dim fill**, not bright — persistent chrome should not be the
loudest thing on screen. The active nav tab is a **bright underline**, not a
fill (3 px of horizontal run, cheaper than the fill it replaced). Filter chips
use bright *text*, not a fill.

That leaves exactly one bright fill per screen, which is what makes an alert
unmissable.

---

## 5. What animates, and when

From `engine-recommendations.md` §5.7. **Redraws are driven by an invalidation
flag set by the parameter layer, never by a timer that repaints everything.**
In steady state exactly one region is animating.

| Region | Update trigger |
|---|---|
| Scope / output trace | continuous, ~30 fps |
| Filter response | on encoder change only |
| Envelope | on encoder change only |
| Waveform | on oscillator change only |
| Readouts | on value change only |

A synth is idle far more often than it is being tweaked, so this is the largest
runtime saving available and it costs nothing. It also means **static ornament
is genuinely free** — drawn once per buffer at startup and never touched again.
That is what makes the corner brackets, index ticks and segmented headers
affordable.

Two consequences for anyone adding a screen:

- Anything you draw inside a plot rect during the chrome pass must also appear
  in that module's `grat[]`, or the erase path will eat it (see §7).
- With double buffering each buffer is a frame behind, so a region dirtied in
  frame *n* must repaint in *n+1*. Repaint the union of this frame's damage and
  last frame's.

---

## 6. Component vocabulary

### Header bar treatments — chosen by what is in the row

| Treatment | Used when |
|---|---|
| Plain full fill | App chrome, selection, alert, default action |
| Segmented (broken at column boundaries) | The header sits above columns |
| Block + tail rule | A single label above a field or panel |

Segment breaks aligned to column pitch let the header rule the grid
horizontally while index ticks rule it vertically — neither requires a vertical
line. The block-and-tail variable width also gives a row of identical panels a
rhythm that full-width bars do not.

**Rejected: chamfered corners.** A 45° cut reads as Turbo Vision, not as an
instrument. The corner brackets, bracket cursor, well limits and index ticks
already carry the angular language at four scales.

### Bipolar values — the split well

Two dim wells separated by a gap, filled outward from the gap by sign, with the
gap itself filled when the value is exactly zero.

The gap guarantees a minimum separation, so the smallest positive and smallest
negative can never be confused — which a centre-anchored bar cannot do at 2 px.
It also gives *zero* a representation distinct from *unassigned*.

Three cell states must stay distinct:

1. Unassigned — faint `·`, no wells drawn
2. Assigned, amount zero — wells drawn, gap filled bright
3. Assigned, non-zero — wells drawn, one side filled

### Other elements

- **Corner brackets** sit 6 px outside the content, so they read as
  registration marks rather than a frame. All blocks on a screen share top and
  bottom edges so the marks align across it.
- **Column index ticks**, 5 px, one per boundary — graduate the grid without
  ruling it.
- **Horizontal position bar** instead of a scrollbar. A vertical scrollbar is
  the only strided element that would survive on any screen; the horizontal bar
  carries the same information contiguously. Pair it with `001-013 OF 128`,
  since a bar alone cannot say where you are numerically.

---

## 7. What exists in code

| File | State |
|---|---|
| `spike/panel.{h,cc}` | Signal-flow screen, ~1,100 lines. Live, stateful, input-driven. |
| `tools/mockup_screens.cc` | Matrix, patch, save. Fixed content, renders to PNG. |
| `tools/panel_shot.cc` | Renders a live `Panel` frame to PNG. |
| `assets/fonts/ter-u{20,14}n.bdf` | The two atlases, OFL-1.1. |
| `tools/bdf_to_c.py` | BDF → C atlas, with `--check` round-trip. |

`spike/panel.{h,cc}` — the signal-flow screen, ~1,100 lines.

### Layout constants

```
frame        1024 × 600
title bar    x16 y16, 992 × 26
modules      y84, h340, w242, at x = {16, 266, 516, 766}
plot area    +6,+58 within a module, 230 × 232
readout      y = kModY + 306, two lines at 22 px pitch
keyboard     y440
nav bar      y512
```

### Implemented

- Four modules: oscillator, filter, envelope, output
- Output has three modes: `kScope`, `kCycle`, `kSpectrum`
- Bracket cursors on the filter cutoff and the three envelope handles,
  draggable
- One-octave keyboard, encoder legend, nav bar
- Chrome drawn once per buffer; plots invalidated per region, never on a timer
- Column-update plot redraw with per-buffer `TraceState`
- Graticule-aware erase, no background mask

### Tools

| Tool | Purpose |
|---|---|
| `tools/panel_shot.cc` | Renders a live `Panel` frame offscreen to PNG. No display, no zlib. Golden-image companion to `test_panel`'s hash. |
| `tools/mockup_screens.cc` | Renders the three **unbuilt** screens (§9) at 1:1 through the real primitives and atlases. Fixed content, no `Panel` state — a design surface, not panel code. |
| `tools/bdf_to_c.py` | BDF → C atlas, with `--check` round-trip |
| `tests/test_panel.cc` | Golden hash + regression tests |
| `tests/test_mockup_chrome.cc` | Asserts the signal-flow mockup's chrome is pixel-identical to the live panel. That identity is what makes the mockup file a usable reference for the other three screens, and for validating a descriptor encoding. |

### Recently fixed, worth not regressing

`DrawPlotAxes` drew *both* the filter and envelope baselines on every call, so
each panel carried a stray line 2 px from its own. The stray was absent from
that module's `grat[]`, and `grat[]` is the only record `ColumnUpdate` has of
what sits under the curve — so any column crossing it erased it permanently.

**The general hazard: `DrawChrome` and `grat[]` are two descriptions of the
same static pixels, and nothing enforces that they agree.** Anything drawn
inside a plot rect by the chrome pass but absent from that module's `grat[]`
will be eaten. Worth deriving both from one table if more static detail is
added inside plots.

---

## 8. What a screen costs, measured

Compiled `-Os -fno-exceptions -fno-rtti -ffunction-sections`, x86-64. Thumb-2
is typically 10-20% smaller for straight-line call sequences like these.

| | `.text` |
|---|---:|
| `DrawMatrix` | 1,885 B |
| `DrawPatch` | 1,720 B |
| `DrawSave` | 1,430 B |
| **Three screens** | **5,035 B** |
| Shared vocabulary helpers | 1,479 B |
| String literals | ~1,529 B |
| Content tables | ~1,100 B |

So roughly **1,700 B of code per screen**, plus its own strings and tables.

Counting the matrix's static chrome against the opcode widths in
`engine-recommendations.md` §5.2 (`RECT` 10 B, `HLINE`/`VLINE` 8 B, `TEXT`
8+len, `CALL` 7 B, `DYN` 10 B) gives **≈ 900-1,100 B** of descriptor — roughly
2x on chrome, in the range §5.3 predicted. The 88 cells and their split wells
are `DYN` and stay as C either way, so the saving applies only to the chrome
portion.

The same economy appears at three scales, which is worth seeing as one thing:

| Scale | Duplicated thing | Descriptor answer |
|---|---|---|
| Within a screen | `Brackets` is 292 B of code called from 6 sites | `CALL` a ~80 B template, 7 B per call |
| Across screens | ~1,700 B of emitted code each | ~900 B of data each, one interpreter |
| Mockup vs panel | The whole layout, written twice | One blob, two consumers |

---

## 9. Rendered at 1:1, not yet wired

Three screens exist in `tools/mockup_screens.cc` as fixed-content renderers.
They draw through `FillRect` / `DrawHLine` / `DrawVLine` / `DrawGlyphRun` and
the shipped 10x20 and 8x14 atlases, so geometry, density and legibility are
exactly what the panel will produce.

**They are also provisional.** The interaction model is not designed (§10), and
a coherent one will move things here — legends, readout placement, possibly the
column geometry. Treat these as the current best statement of the visual
system, not as frozen layouts.

**What they are not:** they take no `Panel` state, handle no input, and do no
damage tracking. Converting one into a real screen means keeping the drawing
and replacing the literal content with state — the layout arithmetic carries
over unchanged.

The file also carries the component vocabulary of §6 as reusable helpers
(`Brackets`, `Cursor`, `BlockTail`, `SegmentedHeader`, `IndexTicks`,
`SplitWell`, `TitleBar`, `NavBar`, `EncoderLegend`), duplicated from `panel.cc`.

**Do not resolve that duplication by deleting the mockup.** Resolve it by
moving both onto a shared representation. Under the descriptor design
(`engine-recommendations.md` §5) a screen is *data*, and the mockup tool
becomes an interpreter plus a PNG writer — perhaps 30 lines beyond what
`panel_shot` already is, with canned state for the `DYN` slots. One description
of each screen, two consumers, no drift.

Left as parallel implementations, the two will diverge the moment a screen is
built, and the mockup stops predicting the panel — which is the only reason it
is worth having.

### What the real font changed

The earlier browser mockups used VT323 with faked letter-spacing. Rendering
through Terminus exposed three problems they had hidden, all from the real
font being heavier:

| Was | Now | Why |
|---|---|---|
| Occupancy strip at `y + 20` | `y + 26` | The target bracket collided with the `BANK A OCCUPANCY` label |
| Occupancy ticks 6 px on a 7.75 px pitch | 5 px | Read as a solid dashed band; individual slots were not countable |
| Name cells 24 px + 4 px gap | 20 px + 3 px | 28 px pitch for a 10 px glyph looked broken, not airy |

Two things the renders **confirmed** rather than changed: the split well is
readable at 1:1 (sign is legible without reading the digits), and focus versus
state is unmistakable on the patch browser at real density.

### Modulation matrix — `DrawMatrix()`

12 sources × 8 destinations on the left (598 px), slot detail panel on the
right (356 px).

**The central idea: a monospace grid needs no grid lines.** Columns align
because every glyph advances 10 px, so 96 cells cost zero rules. A conventional
matrix would want 8 vertical dividers at ~340 px each — around 2,700 strided
writes for decoration.

- Row height 32 px (20 px value + 8 px split-well bar)
- Values signed three-character, right-aligned, sign always present so digits
  never shift
- Destination names truncated to five characters (`CUTOF`, `FMAMT`, `LFO2R`)
- Detail panel rather than a column highlight — highlighting the column would
  mean a vertical fill down the whole matrix

**Open:** 88 visible intersections against 32 slots. Needs a defined behaviour
when the cursor sits on an unassigned cell and the amount encoder turns with no
slots free.

### Patch browser — `DrawPatch()`

13-row list on the left, detail panel on the right.

- Columns: marker (16), number (54), name (310), category (120), mod count
  (60), favourite (42)
- Cursor is a bracket + dim band; the *loaded* patch is a filled block in the
  marker column
- Detail panel previews the patch **under the cursor** and reports the loaded
  patch separately

The `MOD` column earns its width — it tells you which patches are complex
before you load them, from a number you already have.

**Open:** names capped at 24 characters by the 310 px column. Imported banks
run longer. Either drop the category column and let names run to 44, or
truncate with a marker.

### Save dialogue — `DrawSave()`

The hard part is text entry with encoders.

**The ribbon is the whole idea.** Forty characters laid out at once with the
current one inverse, so turning ENC2 moves a highlight along a row you can see.
Without it you are scrolling a hidden list. Costs 40 short text runs and one
inverse cell.

- 16-cell name field, per-cell underline, inverse cursor
- Bank occupancy strip: 128 ticks, tall for used, short for free, bracket over
  the target. A horizontal run pattern, so nearly free, and it answers "where
  are the gaps" without a second screen.

**Open:** uppercase only (66 characters would need two ribbon rows). And the
destructive action is currently the highlighted default — either default to
*Save As New*, or require a hold for overwrite.

---

---

## 10. Interaction model — not designed

**Read this before using anything in §9 as an interaction reference.**

This document covers what the panel *looks like*. It does not cover how it
*behaves*. That is not an oversight to be filled in later — it is a missing
foundation, and closing it will change the screens in §9.

### There is no cohesive story yet

The instrument is a physical control surface and a screen. Nothing so far
states what the relationship between them *is*. Every interaction detail in the
mockups was decided locally, to make one screen look complete, with no
principle connecting it to the next.

**The encoder legend is the clearest symptom.** Every screen carries a row of
text saying what the four encoders currently do. Ask why that row exists and
the answer is: because nothing else tells you. There is no correspondence
between where an encoder sits on the panel and where its parameter appears on
the display, so the screen has to spell it out.

Give the geometry meaning — encoder *n* under column *n*, say, or a readout
placed directly above the control that changes it — and the legend becomes
redundant. The screen labels itself, a text row comes back on every screen, and
the mapping stops being something to memorise and becomes something you can
see. That is not a tweak to the legend; it is a different design, and it
follows from a principle nobody has stated.

Expect the same to be true elsewhere. **A coherent interaction model will
change the layouts in §9**, and that is the intended outcome, not a regression.

### A prerequisite nobody has settled

The mockups assume four encoders and call them ENC1–ENC4. Where they physically
sit — under the screen, beside it, how many there really are, whether they
push, whether there are dedicated buttons — is not recorded anywhere in this
repo.

Spatial correspondence cannot be designed without that, and the panel layout
probably should not be frozen before it. **The control surface and the screen
want designing together**, which is an argument for doing this study early
rather than after the three screens are built.

### Pointers, not a specification

What follows is a collection of loose observations gathered while doing the
visual work. Some matter, several are low-level, and **none of them is a story
about how the instrument should be operated.** The list is neither comprehensive
nor at the right level of abstraction. Use it as evidence of where the seams
are, not as an agenda.

- **The mockups' encoder legends were written to fill a legend line.** They are
  plausible, not derived. Nothing was traded off to reach them.
- **`spike/panel.cc`'s input handling grew from whatever the SDL host
  delivered.** `PollTouch` emits only press and release, so dragging works by
  re-running the hit test on repeated presses.
- **Push is overloaded with no rule behind it** — `PUSH LOAD`, `PUSH CONFIRM`,
  `PUSH CLEAR` mean different things on different screens.
- **Hold is proposed but undefined.** §9 suggests "hold to overwrite" without
  anyone establishing that hold exists as an input.
- **Nothing changes the nav bar.** It is drawn with an active tab on every
  screen and no mechanism selects it.
- **The matrix needs 2D traversal** across 88 cells, and the current mockup has
  no column indicator — the bracket cursor marks a cell, but nothing shows
  which axis an encoder will move along.
- **Modal entry and exit are unspecified**: what dismisses the save dialogue,
  what happens to a half-typed name.
- **Encoder acceleration is undecided**, and it interacts with the matrix's
  32-slot addressing.

### What the study can treat as settled

Constraints, not open questions:

- **The emphasis ladder in §4**, in particular that focus and state are
  distinct visuals. That distinction is already an interaction fact — it exists
  because an endless encoder moves a cursor over items that are not the current
  selection.
- **The primitive cost ordering in §1.** Any affordance needing long vertical
  rules or per-frame full redraws is expensive.
- **The invalidation model in §5.** Input changes a parameter; the parameter
  layer sets a dirty flag; the region redraws. Input never draws directly.
- Two atlases, four colours, 1024×600.

Everything else in §9 — layouts, legends, where a readout sits — is
provisional and should be expected to move.

### Suggested shape

The output-stage study in `docs/workflow/design-studies/` is a reasonable
template: principles first, then options with their trade-offs made explicit,
then a decision with the rejected alternatives recorded. Interaction cannot be
measured the way aliasing can, but the options can still be enumerated rather
than defaulted into.

Prior art is likely to be more useful here than argument. Instruments with a
small number of encoders and a screen have solved spatial correspondence in
several different ways, and the choices are visible in the hardware.

## 11. Open questions

Ordered by how much they block other work.

| # | Question | Notes |
|---|---|---|
| 1 | **Double-buffer dirty tracking** | With two buffers each is a frame behind, so a region dirtied in frame *n* must repaint in *n+1*. Determines whether static ornament is free or costs every frame. The two-frame rule is implemented; the question is whether it generalises when more screens exist. |
| 2 | **GUI definition cost — half measured** | Emitted-code side now measured (see §8): ~1,700 B of `.text` per screen. Descriptor side and the interpreter's fixed cost are still estimates. Break-even is around three screens; five nav tabs plus modals puts twang past it. |
| 3 | **Plot axis label size** | Earlier drafts used a 9 px tier; the two-atlas decision moved them to 14 px. If that is cramped in a 230 px plot, the honest options are shorter labels or a third atlas — not silently shrinking the type. |
| 4 | **Dim-well weight at 4 px** | Across eleven matrix rows the wells add up to standing ink for something meant to be a background track. May want dropping to the faint step. Needs the real panel. |
| 5 | **Terminus zero form** | Default is slashed and `0`/`O` are distinct. Confirm this is what you want before it is baked into an atlas. |
| 6 | **Touch input model** — folded into §10 | `PollTouch` emits only press/release, no move. Dragging works via repeated presses, which means `HitTestEnv` re-runs each event and a drag can jump between handles. Also `g_touch` is non-atomic across threads and `INPUT_EV_SYN` is unhandled. |
| 7 | **Spectrum FFT cost** | 8192-point FFT every dirty output frame. Decimate to 2048, run on a slower cadence, or move to the M85. |
| 8 | **Split-well limit ticks may be too loud** | `kMid` at full 8 px height, so on a populated matrix row they compete with the bright fill they frame. Try `kFaint`, or 6 px. Visible in `mockup_matrix_2x.png`. |
| 9 | **Save dialogue has three bright elements** | Name cursor, ribbon highlight, overwrite warning — against the one-bright-fill-per-screen rule of §4. Inherent to a modal, but the ribbon highlight could drop to a bracket cursor like the matrix uses. |
| 10 | **Matrix row budget** | Eleven sources at 32 px fills the block almost exactly. A twelfth needs the bars to lose 2 px. |

---

## 12. What is measured and what is not

**Measured on host:**

- Font atlas sizes, from the compiled tables
- Layout geometry, from `panel_shot` renders at 1:1
- The `DrawPlotAxes` defect and its fix, by pixel-counting rendered frames
- Golden hash stability across the two buffers
- The three unbuilt screens' geometry, from `mockup_screens` renders at 1:1
  and 2x — this is what caught the three font-weight problems above
- Per-screen `.text` cost (§8), from `nm --size-sort -S` on the `-Os` object

**Estimated, not measured:**

- Every SDRAM access-cost ratio in §1. The *ordering* — contiguous beats
  strided, fills beat diagonals — is from general SDRAM and Cortex-M behaviour
  and should hold. The magnitudes are guesses.
- All flash footprint figures beyond the font atlases.
- Frame bandwidth implications.

**Not measured at all:** anything requiring hardware. Actual GLCDC behaviour,
cache configuration, touch driver semantics, real frame timing. No figure in
this document comes from an M33.

---

## 13. Provenance

Verified in-session from files rather than recalled:

- Terminus BDF header `FONTBOUNDINGBOX 10 20 0 -4` and the OFL-1.1 notice, read
  from `ter-u20n.bdf`
- Departure Mono metrics (advance 7 @ 11 px, 14 @ 22 px), measured from
  `DepartureMono-1.500.otf`
- Font byte arithmetic from `⌈w·d/8⌉ · h · n`
- The doubled-axis defect, by diffing rendered frames: 377 differing pixels on
  exactly two rows, y=354 in module 1 and y=356 in module 2

Corrected after being wrong:

- Terminus's default zero is slashed, not plain
- The 1024×600 mockups rendered with VT323 as a browser stand-in are
  *approximations* — VT323 at `font-size: 20px` does not produce a 10×20 cell,
  and letter-spacing was used to fake the pitch. Only the `panel_shot` renders
  are faithful.
