---
title: Nostromo Interaction Layer
status: draft
date: 2026-09-13
author: Dizan Vasquez
design-study: ../design-studies/2026-09-12_panel-interaction-model_design-study.md
---

# Nostromo Interaction Layer

## 1. Objective

**Two names, one instrument.** *Noisetromo* is the hardware synth being prototyped;
*nostromo* is its visual vocabulary and UI code, deliberately coupled to it. The interaction
model described here is Noisetromo's; `nostromo/` is where it lives. The general/specific
boundary in this project runs between `spike/` — framebuffer, descriptor interpreter, fonts,
damage tracking — and `nostromo/`. Everything this document adds sits on the `nostromo/` side.
No further boundary is drawn inside it: with one instrument built, any line between mechanism
and instance would be a guess, and a guessed seam is harder to correct than none.

`nostromo` renders the panel but does not yet decide what the panel *does* with input. Today
`panel.cc` handles input as it arrived from the SDL host: `PollTouch` emits press and release
only, drag works by re-running the hit test on repeated presses, and `PUSH` means a different
thing on each screen. There is no representation of a control surface, no notion of which
parameter a given encoder drives, and no page structure.

This arch-design defines the **interaction layer**: the component that sits between the input
driver and the engine's parameter API, resolves what each physical control currently drives,
applies gestures to that binding, and marks the affected DYN slots dirty. It is the settled
result of the companion design study; the study carries the reasoning and the rejected
alternatives, and this document does not repeat them.

The component is a pure state machine over input events. It performs no drawing — the
descriptor interpreter draws static chrome and DYN hooks draw live content. It does not touch
invalidation state either: the panel owns the damage list, and this layer reports a state
change through the panel's `MarkDirty` entry point (§4).

**The control surface is expected to iterate, starting early.** Handling and feel are judged
by hand, on real prototypes, over a sequence of them — the X-Touch first, then built panels of
varying control counts, then a refined one. Four consequences shape the architecture, and each
exists to keep a prototype change out of the source:

- **Geometry is derived from parameters.** The column count $E$, the framebuffer size and the
  panel's physical dimensions are compile-time constants; pitch, column origins and aspect
  correction derive from them (§7.1), with a `static_assert` on the ergonomic floor.
- **Pages declare ordered parameter lists, not packed groups.** Grouping into columns is
  computed from $E$ (§7.4), so changing $E$ re-groups every page without re-authoring one.
- **The physical surface is data.** A `SurfaceProfile` maps physical inputs to logical
  controls (§7.9), so a new prototype is a table rather than a code change.
- **Feel is runtime-tunable.** Detent scaling, acceleration threshold, long-press duration and
  the fine divisor live in a mutable `FeelProfile` edited from the CONF page (§7.10). A feel
  experiment must not cost a rebuild and a reflash.

This is the one place where this document deliberately departs from the study's framing: §4.4
there argued that $E$ could not be revised after the panel was cut. The conclusion ($E = 5$)
stands on its independent grounds — five-parameter modules and 28 mm pitch — but the
irreversibility argument does not apply to a project that iterates on hardware throughout.

## 2. Non-Goals

- **The engine's parameter surface.** This layer addresses parameters through `ParamId` and
  walks `g_params`; it does not define what parameters exist. Pages whose parameters are not
  yet in the enum are declared with their columns marked pending (§7.4).
- **Drawing.** No primitive is called from this component. DYN hooks draw; this layer sets
  `DynSlot::dirty`.
- **Touch.** `PollTouch` remains, but no gesture in this design depends on it. Touch as a
  secondary modality is additive.
- **Performance mode content.** The mode, its latch and its LED are reserved; which
  parameters it binds is deferred (study §4.12).
- **Patch storage.** The browser addresses patches through an opaque `PatchRef`; what backs
  it is out of scope.
- **MIDI-CC mapping of panel controls.** The addressing leaves room for it; the mapping is
  not designed here.

## 3. Terminology

| Term | Meaning |
|---|---|
| **Subject** | An entry in the navigation pane — an oscillator, the filter, the mod page, a global. What NAV1 selects. |
| **Page** | The screen for one subject: a pane label, one or more column groups, an optional item axis, and DYN slot bindings. |
| **Column** | One of $E$ parameter positions across the screen. Column $n$ is driven by encoder $n$ and labelled by its own header. |
| **Column group** | A set of at most $E$ columns shown together. A page with more parameters than $E$ has several, cycled by the group button. |
| **Item axis** | What NAV2 walks on a page: modulation slots, patches, a route list, or nothing. |
| **Binding** | The resolution of a physical control to what it currently drives, given the navigation state. |
| **Gesture** | A recognised input pattern: turn, short press, long press, hold-and-turn. |
| **Latched mode** | A mode that persists without a held control. Every latched mode carries a physical indicator (§9). |
| **Route** | A modulation connection, `ModRoute` in the engine: source, destination `ParamId`, signed amount. |

## 4. System Context

The interaction layer is a portable library in `nostromo/`, linked by both the simulator and
the target firmware. It has no transport dependency.

```
   encoder / button driver          (host: SDL; target: GPIO + timer scan)
              │  physical event
              ▼
   ┌──────────────────────────┐
   │   interaction layer      │   NavState · PageTable · gesture recognition
   │   (this document)        │   binding resolution · dispatch
   └───────┬──────────┬───────┘
           │          │
   engine API      MarkDirty(panel, slot)
   EngineSetParam  EngineSetRoute        →  panel records damage, sets the
   EngineGetParam  EngineGetRoute            slot flag, and schedules the
                                             repaint into both buffers
```

- **Driver to interaction**: `void InteractionOnInput(const InputEvent &ev)`.
- **Interaction to engine**: the existing `EngineSetParam` / `EngineSetParamDisp` /
  `EngineSetRoute` surface. No new engine entry points, with one exception in §7.8.
- **Interaction to panel**: `MarkDirty(Panel *, SlotIdx)`. Never draws, never writes
  invalidation state directly.
- **Panel to interaction**: DYN hooks read `NavState` and the resolved bindings as their
  opaque `state` pointer, to draw cursors, headers and values.

**Invalidation is the panel's, not this layer's.** `Panel` owns the `spike::Damage` instance,
and `MarkDirty` does three things that must happen together: it sets `DynSlot::dirty`, sets
`pending[slot] = 2` so the region is repainted into *both* buffers, and adds the slot's damage
rects. Setting `dirty` alone does nothing at all — `PanelDraw`'s walk gates on
`pending[k] > 0` and then on damage intersection, and never reads the flag. The only path that
read `dirty` directly was `DrawDyn`, which ignored `pending` and `damage` and would have
repainted a single buffer; it had no callers and has been removed. There is one owner and one
entry point, and this layer is a caller of it. `spike_arch-design.md` is authoritative here.

Input never draws (`engine-recommendations.md` §5.7). `MarkDirty` is the only channel.

## 5. Architecture

Five components, in the order an event traverses them.

- **`SurfaceProfile`** — maps physical inputs (MIDI CC on the host, scan index on target) to
  logical `Control` values, and declares what the current prototype physically has. Selected
  at startup; swapping prototypes swaps a table.
- **`GestureRecognizer`** — converts a stream of `InputEvent` into `Gesture`. Stateful per
  control: it holds press timestamps and whether a detent has arrived during a press. This
  is where the hold-versus-press disambiguation lives. Its timings come from `FeelProfile`,
  not from constants.
- **`NavState`** — the complete navigation state: part, subject, item cursor, column group,
  view mode, armed modulation source, and per-page item-cursor memory. Plain value type,
  serialisable, no pointers.
- **`PageTable`** — a static table of `PageDesc`, one per subject, in pane order. Read-only,
  `constexpr`, lives in `.rodata`.
- **`BindingResolver`** — a pure function `(NavState, Control) → Binding`. Given the state
  and a control, says what that control drives right now. No side effects; this is what makes
  the layer testable without a framebuffer.
- **`Dispatcher`** — applies a `Gesture` to a `Binding`: calls the engine, updates `NavState`,
  and calls `MarkDirty` for each affected slot. The only component with side effects, and the
  only one that knows which slots a given state change touches.

Per-event flow: driver emits a physical event → `SurfaceProfile` maps it to a logical
`InputEvent` → `GestureRecognizer` emits a `Gesture` or nothing → `BindingResolver` resolves
the control against `NavState` → `Dispatcher` applies it.

### Design Decisions

**Binding resolution is a pure function, not stored state.** The alternative — caching what
each encoder drives and invalidating on navigation — would be a second description of a fact
already held in `NavState` and `PageTable`, with nothing enforcing agreement. That is exactly
the `DrawChrome`/`grat[]` failure mode recorded in `panel-ui-design-state.md` §7. Resolution
is a table lookup and a bounds check; it is cheap enough to run per event.

**Geometry is derived from parameters, not written as literals.** See §7.1. A layout constant
appearing as a number anywhere outside `geom` is a defect.

**Acceleration is a per-parameter field, not a global constant.** Three distinct policies are
already required — none on cursor traversal, capped with a zero notch on modulation amounts,
and a default for ordinary parameters — so a single input-layer constant cannot express the
design. It belongs in `ParamDesc` beside range and formatter (§7.8).

**Column assignments are data, not code.** `PageTable` is a table so that pages fill in as the
engine's parameter surface grows, without touching this component.

**Column grouping is computed, not authored.** A page declares an ordered `ParamId` list;
groups are $\lceil n/E \rceil$ slices of it. Order carries meaning — the head of the list is
the page's hot set and stays the first group at any $E$ — which expresses the study's
"declared hot set" in the type rather than in a comment.

**The pane renders hierarchically and navigates flat.** `OSC`, `ENV`, `LFO` and `OUT` appear
as labels owning a horizontal strip of cells, so the pane is 11 rows rather than 20. But the
labels are *presentation*: they are not selectable, and NAV1 walks every subject in reading
order — rows and cells alike, one detent per subject. §4.1's Option 3 therefore still holds;
what was rejected there was a second navigation *axis* requiring a second control, not a
grouped rendering. Using NAV2 for instances was tried and rejected: it spends two encoders on
what is a one-dimensional walk.

What the grouped rendering buys is the growth case the flat list failed. A flat 18-row pane
needed 513 px of 526 — zero headroom, and a second filter failed the build. Rows plus strips
need 415, and an added instance costs a cell rather than a row.

**`kOut` is three subjects, not one with a view setting.** §7.6 originally gave OUT a `VIEW`
column toggling scope, cycle and spectrum. Rendering it showed that only `SOURCE` is shared
across the three: `SCALE` means full-scale amplitude in scope and a dB floor in spectrum, and
the remaining three columns have no counterpart at all. One page with a value-dependent column
set is a shape `PageDesc` cannot express; three subjects sharing a class label is a shape it
already has. A selector below the columns it governs also inverted the visual hierarchy —
every other selector is in the pane, to the left of what it controls.

**Feel is state, not constants.** Anything judged by hand is tunable at runtime and editable
from the CONF page. Anything that changes the shape of the design is compile-time. That line
is the line between §7.10 and §7.1.

**One owner for invalidation.** This layer could set `DynSlot::dirty` itself and save a call,
but the flag is one of three things `MarkDirty` updates together, and a caller that knows only
about the flag cannot know a slot's full damage extent. Two writers of one invariant is the
`DrawChrome`/`grat[]` failure recorded in `panel-ui-design-state.md` §7. The panel keeps
ownership; this layer reports.

## 6. Component Lifecycle

The layer is a singleton initialised once and never destroyed.

1. **Init** — `InteractionInit(DynSlot *slots, int n_slots, const SurfaceProfile &surface)`.
   Binds the slot array and the surface, loads `g_feel` from persisted settings or its
   defaults, zeroes `NavState` to part 0, subject `kOut`, `prev` `{kFilt, 0, -1}`, group 0,
   mode `kEdit`, and marks all slots dirty. `kOut` is the power-on page for the same reason it
   has a button: it is what the instrument shows when nobody is editing. Reports once if `surface.n_encoders < geom::kColumns`.
2. **Steady state** — events arrive, gestures are recognised, bindings resolve, the engine is
   written, slots are marked. No allocation, no blocking.
3. **Mode transitions** — entering `kModArm` (MOD pressed) or `kModView` (MOD tapped) calls
   `MarkDirty` for the pane and every column slot, because both change what those regions
   display. Leaving does the same.
4. **Part change** — writes `NavState::part` and calls `MarkDirty` for the value slots only.
   The layout is untouched: header, columns, cursor and group are identical across parts
   (study §4.11), so chrome is never re-interpreted on a part change.

There is no teardown path.

## 7. Types

### 7.1. Geometry

```cpp
namespace nostromo::geom {

// --- Parameters. Change these; everything below derives. ---
inline constexpr int   kFbWidth      = 1024;
inline constexpr int   kFbHeight     = 600;
inline constexpr float kPanelWmm     = 154.2144f;  // ER-TFT070-6 active area
inline constexpr float kPanelHmm     = 85.92f;
inline constexpr int   kColumns      = 5;          // E
inline constexpr int   kMargin       = 16;
inline constexpr int   kPaneW        = 92;
inline constexpr float kKnobDiaMm    = 20.0f;
inline constexpr float kFingerGapMm  = 5.0f;

// --- Vertical band parameters. Plot-dominant: the plot takes the residue. ---
inline constexpr int kTitleH   = 26;  // title bar
inline constexpr int kBandGap  = 16;  // title -> content
inline constexpr int kHeaderH  = 26;  // column header (segmented)
inline constexpr int kHeaderGap = 6;
inline constexpr int kValueH   = 20;  // one primary-atlas line
inline constexpr int kValueGap = 4;
inline constexpr int kWellH    = 8;   // split well
inline constexpr int kSumGap   = 6;
inline constexpr int kSumLines = 2;   // inbound-route summary, edit mode
inline constexpr int kSumPitch = 18;  // secondary atlas + leading
inline constexpr int kPlotGap  = 16;
inline constexpr int kRowPitch = 26;  // list rows: 20px cell + leading
inline constexpr int kPanePitch = 26; // pane entries
inline constexpr int kStripH   = 22;  // instance strip
inline constexpr int kGlyphW   = 10;  // primary atlas cell width
inline constexpr int kPaneRule = 3;   // globals separator
inline constexpr int kPrimaryH = 20;  // ter-u20n cell height
inline constexpr int kPlotMinH = 120; // below this a plot is decoration

// --- Derived. Never write these as literals elsewhere. ---
inline constexpr float kPxMm    = kPanelWmm / kFbWidth;    // 0.150600
inline constexpr float kPyMm    = kPanelHmm / kFbHeight;   // 0.143200
inline constexpr float kAspect  = kPxMm / kPyMm;           // 1.0517 — pixels are wider
inline constexpr int   kColW    = (kFbWidth - 2 * kMargin - kPaneW) / kColumns;
inline constexpr int   kColX(int n) { return kMargin + kPaneW + n * kColW; }
inline constexpr float kPitchMm = kColW * kPxMm;

inline constexpr int kTitleY   = kMargin;                              //  16
inline constexpr int kContentY = kTitleY + kTitleH + kBandGap;         //  58
inline constexpr int kHeaderY  = kContentY;                            //  58
inline constexpr int kValueY   = kHeaderY + kHeaderH + kHeaderGap;     //  90
inline constexpr int kWellY    = kValueY + kValueH + kValueGap;        // 114
inline constexpr int kSumY     = kWellY + kWellH + kSumGap;            // 128
inline constexpr int kSumH     = kSumLines * kSumPitch;                //  36
inline constexpr int kPlotY    = kSumY + kSumH + kPlotGap;             // 180
inline constexpr int kBottom   = kFbHeight - kMargin;                  // 584

inline constexpr int kPlotH = kBottom - kPlotY;                        // 404
inline constexpr int kPlotW = kColumns * kColW;                        // 900
inline constexpr int kPlotX = kMargin + kPaneW;                        // 108

// List pages (MOD, PATCH) have no value row: rows start under the headers.
inline constexpr int kListY = kValueY;                                 //  90
inline constexpr int kListH = kBottom - kListY;                        // 494
inline constexpr int kListRows = kListH / kRowPitch;                   //  19

// Mod view keeps the header and one value line, and replaces the rest.
inline constexpr int kRouteY = kWellY;                                 // 114
inline constexpr int kRouteH = kBottom - kRouteY;                      // 470
inline constexpr int kRouteLines = kRouteH / kRowPitch;                //  18

inline constexpr int kPaneY = kContentY;                               //  58
inline constexpr int kPaneH = kBottom - kPaneY;                        // 526
// The pane renders hierarchically: class labels own instance strips, so its
// height requirement is rows plus strips, not a row count.
inline constexpr int kPaneRowsN   = 11;  // labels and singletons
inline constexpr int kPaneStripsN = 4;   // OSC ENV LFO OUT
inline constexpr int kPaneNeedH   = kPaneRowsN * kPanePitch +
                                    kPaneStripsN * (kStripH + 8) +
                                    kPaneRule + 6;                     // 415

// Strip cells have a fixed pitch so digits align between classes; padding
// shrinks as the token grows.
inline constexpr int kStripAvail = kPaneW - 8 - 6;                     //  78
inline constexpr int StripCellW(int chars) {
  return chars * kGlyphW + (chars == 1 ? 8 : 4);
}
inline constexpr int StripW(int cells, int chars) {
  return cells * StripCellW(chars);
}

// A configuration that cannot be built must not compile.
static_assert(kColumns * kColW + kPaneW + 2 * kMargin == kFbWidth,
              "columns must tile the content width exactly");
static_assert(kPitchMm >= kKnobDiaMm + kFingerGapMm,
              "encoder pitch below the ergonomic floor: reduce kColumns "
              "or widen the panel");
static_assert(kPlotH >= kPlotMinH,
              "plot band collapsed: the vertical bands above it have grown "
              "past what a plot-dominant layout allows");
static_assert(kValueH >= kPrimaryH && kHeaderH >= kPrimaryH,
              "a text band is shorter than the primary atlas cell");
static_assert(kRouteLines >= 6,
              "mod view cannot show a useful number of routes per column");
static_assert(kPaneH >= kPaneNeedH,
              "the subject pane cannot show every class and its instance "
              "strips without scrolling");
static_assert(StripW(4, 1) <= kStripAvail, "digit strip overflows the pane");
static_assert(StripW(3, 2) <= kStripAvail, "view strip overflows the pane");
}  // namespace nostromo::geom
```

`kAspect` is the correction factor for any drawing that must read as geometrically square:
a shape intended to be $h$ px tall and visually square is $\lceil h / \text{kAspect} \rceil$
px wide.

**Plot-dominant, and what it costs.** The bands above the plot are fixed at their minimum
legible size and the plot takes the residue — 404 px against 900 px wide, roughly 1.9:1 after
aspect correction, and about 1.7× the 232 px the current four-module layout gives it. The
price is paid in edit mode: one value line and one well per column, with no room for a
secondary readout under a column. Mod view is unaffected, because it replaces the plot rather
than sharing with it — 18 route lines per column, which is past any plausible inbound count
and well past the point where §4.7's reflow takes over.

**The summary band is not dead space.** `kSumY`/`kSumH` hold two lines of inbound-route
summary per column in *edit* mode (§4.8), so "what modulates this" needs no mode change.
Pages whose columns are not modulatable — MOD, PATCH, CONF, the OUT views — have no summary,
and the band would read as a gap; the OUT views fill it with their own content rather than
letting the plot claim it, because band registration across pages is what makes switching
cheap to read.

**The pane has headroom now.** 415 px of 526, against the 513 px a flat 18-row list needed.
The instance strips are what bought it: a fifth oscillator is a cell, not a row, so the pane
scales with instance count for free. That headroom is why a second filter — the growth case
that failed the flat layout — now costs 26 px rather than failing the build.

### 7.2. Controls and input

```cpp
enum class Control : std::uint8_t {
  kNav1 = 0,          ///< subject pane cursor
  kNav2,              ///< item axis cursor
  kEnc0,              ///< parameter column 0 .. kColumns-1
  kEncLast  = kEnc0 + geom::kColumns - 1,
  kPart0, kPart1, kPart2, kPart3,
  kMod,               ///< momentary (arm) and tap (view)
  kPerf,              ///< latching, reserved
  kGroup,             ///< momentary, column-group cycle
  kOut,               ///< momentary, jump to kOut and back
  kCount,
};

enum class Edge : std::uint8_t { kNone, kDown, kUp };

struct InputEvent {
  Control       control;
  std::int8_t   detents;   ///< signed; 0 for a pure button event
  Edge          edge;      ///< kNone for a pure turn
  std::uint32_t t_ms;      ///< monotonic
};

enum class Gesture : std::uint8_t {
  kTurn,        ///< detents, no press held
  kHoldTurn,    ///< detents while pressed — fine adjust
  kPressShort,  ///< press and release under kLongPressMs, no detent
  kPressLong,   ///< press held past FeelProfile::long_press_ms, no detent
};
```

Press timing is not a constant here; it is `g_feel.long_press_ms` (§7.10), starting at 500 ms.

### 7.3. Navigation state

```cpp
enum class SubjectId : std::uint8_t {
  kPart = 0,                       ///< part-level settings
  kFilt, kAmp, kMod,
  kOsc1, kOsc2, kOsc3, kOsc4,
  kEnv1, kEnv2, kEnv3,
  kLfo1, kLfo2, kLfo3,
  kOutScope, kOutCycle, kOutSpec,  ///< globals, below the pane rule
  kFx,
  kPatch, kConf,
  kCount,                          ///< 20
};

enum class ViewMode : std::uint8_t {
  kEdit = 0,   ///< default
  kModArm,     ///< MOD held — momentary
  kModView,    ///< MOD tapped — latched, LED lit
  kPerform,    ///< PERF — latched, LED lit
};

/// A navigation position: everything the OUT button must restore.
struct NavPos {
  SubjectId    subject;
  std::uint8_t group;
  std::int8_t  focus_col;
};

struct NavState {
  std::uint8_t part;                  ///< [0, kNumParts)
  SubjectId    subject;               ///< pane cursor; global across parts
  std::uint8_t group;                 ///< active column group
  std::uint8_t item[static_cast<int>(SubjectId::kCount)];  ///< per-page item cursor
  std::int8_t  focus_col;             ///< focused column, -1 = none
  ViewMode     mode;
  ModSourceId  armed_source;          ///< persists between kModArm entries
  NavPos       prev;                  ///< return position for the OUT button only
};
```

`subject` and `group` are deliberately *not* per-part: a part change alters values only
(study §4.11). `item` is per-subject because a page's item cursor is a property of that page,
not of the navigation as a whole.

### 7.4. Pages

A column does not always drive an engine parameter. The MOD page's columns are fields of a
route, the PATCH page's are view controls, and the CONF page's are `FeelProfile` fields. A
column is therefore a tagged reference, not a `ParamId`:

```cpp
enum class ColumnKind : std::uint8_t {
  kNone = 0,     ///< past the end of a partial final group
  kParam,        ///< an engine parameter of the current subject
  kPending,      ///< declared, but ParamId does not define it yet
  kRouteField,   ///< a field of the item the page's item axis selects
  kViewCtl,      ///< a browser or settings control
};

enum class RouteField : std::uint8_t { kSource, kDest, kAmount, kCurve, kEnable };
enum class ViewCtl    : std::uint8_t {
  kCategory, kSort, kFavourite, kAction,        // PATCH
  kDetents, kAccelMax, kAccelThresh, kLongPress, kFineDiv,  // CONF
};

struct ColumnSpec {
  ColumnKind kind;
  union {
    ParamId    param;
    RouteField field;
    ViewCtl    ctl;
  };
};

enum class ItemAxis : std::uint8_t {
  kNone = 0,   ///< NAV2 idle
  kSlots,      ///< modulation slots
  kPatches,    ///< patch list within the selected category
  kRoutes,     ///< route list of the focused column
};

struct PageDesc {
  SubjectId         subject;
  const char       *label;    ///< pane text; see the length invariant in §9
  const ColumnSpec *cols;     ///< ORDERED. Head of the list is the hot set.
  std::uint8_t      n_cols;   ///< grouping is derived, not authored
  ItemAxis          item_axis;
  std::int8_t       dyn_slot; ///< plot slot, or -1
};

/// Groups are ceil(n/E) slices of `cols`. Changing geom::kColumns re-groups
/// every page; no page is re-authored.
constexpr int GroupCount(const PageDesc &p) {
  return (p.n_cols + geom::kColumns - 1) / geom::kColumns;
}

constexpr ColumnSpec Column(const PageDesc &p, int group, int col) {
  const int i = group * geom::kColumns + col;
  return i < p.n_cols ? p.cols[i] : ColumnSpec{ColumnKind::kNone, {}};
}

extern const PageDesc g_pages[static_cast<int>(SubjectId::kCount)];
```

`kPending` renders its header dim with an empty value and ignores input. It keeps the page
taxonomy complete and honest while `ParamId` grows from its current 11 entries. `kNone` is a
column past the end of a partial final group: nothing drawn, encoder inert.

Ordering is the page author's only control over packing, and it is enough. Put the columns
that carry most of the editing first and they occupy group 0 at $E = 5$; at $E = 6$ they
occupy group 0 with a sixth alongside. Nothing is re-authored either way.

### 7.5. `ParamId` names a kind, not an instance

Under product addressing (study §4.1) a parameter is $(p, m, i, k)$. The part comes from
`NavState::part` and the instance from `SubjectId` — `kOsc3` *is* $i = 3$ — so `ParamId` needs
to carry only $m$ and $k$: the kind of parameter within a module. `kOscCoarse`, not
`kOsc3Coarse`.

This is a decision, and it has a large consequence. Per-instance enumerators would make
`ParamId` roughly $4 \times 7 + 3 \times 6 + 3 \times 7 + \ldots \approx 130$ entries and
would duplicate every oscillator page four times in `g_pages`. Per-kind makes it about 39, and
the four oscillator pages share one column list. It also means the engine's parameter API
needs an instance argument it does not currently have — `EngineSetParam(part, instance, id,
value)` — which is engine work this design depends on and does not perform.

### 7.6. The page table

Ordered by pane position. `∗` marks a column whose `ParamId` exists today; everything else is
`kPending`. A label written `OSC`/`2` is a class label plus a strip cell: one pane row for the
class, one cell per instance, and the cell is what NAV1 selects. Every subject also carries a
long-form name for the title bar — `OSCILLATOR 2`, `MODULATION`, `CONFIGURATION` — spelled out
except where the acronym is the established term (`LFO`). Group boundaries are derived at $E = 5$ and shown only to make the packing visible.

| Subject | Label | Columns, in order (group 0 ‖ group 1) | Item axis | Plot |
|---|---|---|---|---|
| `kPart` | `PART` | chan, voices, transpose, glide, mono/poly ‖ bend range | — | — |
| `kOsc1..4` | `OSC`/`1`..`4` | wave, coarse∗, fine, level, shape ‖ pan, sync | — | wave |
| `kFilt` | `FILT` | cutoff∗, resonance∗, env amt, drive∗, keytrack∗ ‖ mode | — | response |
| `kAmp` | `AMP` | level∗, pan, velo sens, send A, send B | — | — |
| `kEnv1..3` | `ENV`/`1`..`3` | A∗, D∗, S∗, R∗, curve ‖ velo sens | — | envelope |
| `kLfo1..3` | `LFO`/`1`..`3` | rate, shape, depth, sync, fade ‖ phase, retrig | — | shape |
| `kMod` | `MOD` | source, dest, amount, curve, enable | slots | — |
| `kOutScope` | `OUT`/`SC` | source∗, timebase, scale, trigger, hold | — | scope |
| `kOutCycle` | `OUT`/`CY` | source∗, cycles, scale, align, hold | — | single cycle |
| `kOutSpec` | `OUT`/`SP` | source∗, range, scale, average, window | — | spectrum |
| `kFx` | `FX` | *pending* | — | — |
| `kPatch` | `PATCH` | category, sort, favourite, action | patches | — |
| `kConf` | `CONF` | detents/rev, accel max, accel thresh, long press, fine div | — | — |

Three pages are the natural first screens: `kFilt`, `kEnv1..3` and `kOut`, the last because its
machinery — `PlotOut`, the FFT, `TraceState`, `ColumnUpdate` and the scope/cycle/spectrum
toggle — is the most complete in `panel.cc`.

**"Buildable today" is narrower than the ∗ marks suggest.** The four existing filter columns
and four existing envelope columns are all *continuous*. Filter mode and envelope curve are
discrete, and `synth-routing_arch-design.md` scopes `ParamId` to modulatable parameters — so
discrete parameters are outside it by definition, not merely absent from it. They render as
`kPending` until `ParamId` covers non-modulatable parameters, which is part of the engine
dependency in §13.6, not a matter of adding enumerators. The same applies to every discrete
column in §7.6: wave select, LFO shape and sync, mono/poly, and the OUT views' window and
trigger settings.

**`kOut` is the visual keystone, and it is global.** The output section is where the user sees
what the engine is actually doing, so it is the page the instrument is left sitting on and the
one glanced at mid-edit. It monitors the master bus (`output-stage_arch-design.md`), but its
first column selects the source — master, or one part — so scoping a single part while
dialling it needs no mode and no second page.

Its plot is 900 × 404 like every other page's. The keystone quality comes from availability,
not size: a larger plot would break the band registration that makes switching pages cheap to
read.

Being global has a visible consequence. A global subject is not owned by a part, so the title
bar's part indicator says so rather than naming a part that has nothing to do with what is on
screen: the four swatches go to outline and `P<n>` becomes `GL`. Without that, `SOURCE MASTER`
and `P1` sit on one screen contradicting each other, and pressing a part button appears to do
nothing.

**`kMix` is gone, folded into `kAmp`.** Level, pan, velo sens, send A and send B is one clean
group, and the two subjects were never distinct — both answer "how much of this part, and
where." The saving is no longer load-bearing — the strip pane has headroom the flat one did
not — but the merge stands on its own terms.

**Ordering rationale, where it is not obvious.** Filter puts drive ahead of keytrack because
drive is dialled while listening and keytrack is set once per patch. Filter mode is in group 1
for the same reason — discrete, set early, rarely revisited — even though it is arguably more
fundamental than anything in group 0. Oscillator pan and sync sit in group 1 on the same
test. Envelope curve is in group 0 rather than velo sens because curve changes the shape you
are looking at in the plot directly above it.

### 7.7. Bindings

```cpp
enum class BindKind : std::uint8_t {
  kNone = 0,
  kParam,        ///< ColumnKind::kParam — a parameter of the current subject
  kRouteField,   ///< ColumnKind::kRouteField — a field of the item NAV2 selects
  kViewCtl,      ///< ColumnKind::kViewCtl — a browser or settings control
  kRouteAmount,  ///< a route amount reached through kModArm / kModView
  kNavSubject, kNavItem,
  kPartSelect, kModeToggle, kGroupCycle, kOutToggle,
  kPending,      ///< ColumnKind::kPending — declared, unimplemented
};

struct Binding {
  BindKind    kind;
  union {
    ParamId    param;   ///< kParam, kRouteAmount
    RouteField field;   ///< kRouteField
    ViewCtl    ctl;     ///< kViewCtl
  };
  std::int8_t slot;     ///< kRouteAmount / kRouteField: engine route slot, -1 to allocate
  std::int8_t column;   ///< originating column, -1 if not column-derived
};

Binding ResolveBinding(const NavState &nav, Control c);

`ColumnKind` and `BindKind` are in one-to-one correspondence for the four column kinds, so
resolving a column encoder is a tag copy plus the operand. The resolution rules for the three
non-parameter kinds:

| `ColumnKind` | Turn | Short press | Long press | Hold + turn |
|---|---|---|---|---|
| `kRouteField` | change the field of the item at `item[subject]` — `kSource` and `kDest` cycle enums, `kAmount` is continuous, `kEnable` toggles | — | clear the slot (`kSource`/`kDest`), else revert | fine adjust, `kAmount` only |
| `kViewCtl` | change the view control; no engine write for `kCategory`/`kSort`/`kFavourite`, a `g_feel` write for the CONF controls | `kAction` executes; others none | revert to default | fine adjust where continuous |
| `kPending` | nothing | nothing | nothing | nothing |

`kRouteField` is the one kind whose operand is not addressed by the column alone: it needs
`item[subject]` for the slot, which is why `Binding::slot` is populated for it.
```

### 7.8. Change required in `engine/params.h`

Acceleration is a property of the parameter, so `ParamDesc` gains two fields:

```cpp
struct ParamDesc {
  // ... existing: name, unit, disp_min, disp_max, def, curve, offset, comb
  std::uint8_t accel_max;   ///< 1 = none; 3 = capped 3x (modulation amounts)
  bool         zero_notch;  ///< require one extra detent to cross zero
};
```

This is the only change this design imposes outside `nostromo/`. `accel_max` defaults to
`FeelProfile::accel_max_default` (§7.10); `zero_notch` is true for bipolar amounts, whose zero
state the split well renders distinctly.

### 7.9. Surface profile

```cpp
struct ControlMap {
  std::uint16_t physical;   ///< MIDI CC (host) or scan index (target)
  Control       logical;
};

struct SurfaceProfile {
  const char       *name;        ///< "xtouch-compact", "proto-a", "noisetromo-v1"
  const ControlMap *map;
  std::uint8_t      n_map;
  std::uint8_t      n_encoders;  ///< parameter encoders physically present
  std::uint8_t      n_buttons;
  bool              has_rings;   ///< LED rings on the parameter encoders
};

const SurfaceProfile &Surface();
void SetSurface(const SurfaceProfile &p);
```

A prototype is a `ControlMap` table and nothing else. Profiles may be **supersets** — the
X-Touch's ringed encoder bank exceeds $E$, and the surplus is simply unmapped, which is what
makes it usable for comparing $E$ values by masking rather than rebuilding. Profiles may also
be **subsets**: when `n_encoders < geom::kColumns`, columns beyond `n_encoders` render
normally but are reachable only through the group button, and the shortfall is reported once
at startup rather than silently tolerated.

`has_rings` is advisory. Nothing in the interaction model depends on rings; the split wells
carry value feedback (study §4.5). A profile with rings drives them from the same resolved
binding the column header uses, so the two cannot disagree.

### 7.10. Feel profile

```cpp
struct FeelProfile {
  std::uint8_t  detents_per_rev;      ///< the encoder's own detent count
  std::uint8_t  accel_max_default;    ///< ordinary parameters; 1 = none
  std::uint16_t accel_threshold_dps;  ///< detents/second above which accel engages
  std::uint32_t long_press_ms;
  std::uint8_t  fine_divisor;         ///< hold-and-turn divisor (10 = x1/10)
};

extern FeelProfile g_feel;   ///< mutable; edited from the CONF page
```

These are the values judged by hand, so they are runtime state rather than constants. Each is
a column on the CONF page, which makes a feel experiment a turn of an encoder instead of a
rebuild and a reflash — and makes the instrument the tool used to tune the instrument.

`detents_per_rev` is what makes a setting portable across prototypes: acceleration and fine
adjust are specified per revolution and converted to detents, so a profile carried from a
24-detent encoder to a 30-detent one keeps its feel rather than its numbers.

## 8. Contracts

### InteractionOnInput

```cpp
void InteractionOnInput(const InputEvent &ev);
```

- **Precondition**: `ev.control < Control::kCount`; `ev.t_ms` monotonic non-decreasing.
- **Postcondition**: zero or one gesture applied; `NavState` updated; `MarkDirty` called
  exactly once for every slot whose rendered content changed, and for no other. No primitive
  is called and no invalidation field is written directly.
- **Detent absorption**: if a detent arrives while a control is pressed, the gesture is
  `kHoldTurn` and the subsequent release emits nothing. This is what prevents an accidental
  micro-turn during a press from being read as a short press.
- **Thread safety**: single-threaded, called from the M33 input task only.

### ResolveBinding

```cpp
Binding ResolveBinding(const NavState &nav, Control c);
```

- **Pure.** No side effects, no engine reads, no globals beyond `g_pages`.
- **Total.** Every `(nav, c)` pair resolves; unreachable combinations return
  `BindKind::kNone`.
- **Postcondition on `kParam`**: `param` is a valid `ParamId` — never a pending column, which
  returns `kPending` instead.

Resolution by mode, for a column encoder $n$ in group $g$:

| Mode | Turn | Short press | Long press | Hold + turn |
|---|---|---|---|---|
| `kEdit` | set `cols[n]` | descend, if the page marks the column descendable | revert to default | fine adjust ×⅒ |
| `kModArm` | write route `armed_source → cols[n]`, amount from the turn | — | — | fine adjust of the amount |
| `kModView` | set `cols[n]` | focus column $n$; NAV2 walks its routes | revert to default | fine adjust |
| `kPerform` | reserved | reserved | reserved | reserved |

### Navigation contracts

- **NAV1 turn** — moves `subject` by one, wrapping over `SubjectId::kCount`. Marks pane and
  all column slots dirty.
- **NAV1 press** — no effect. The pane is flat and turning already commits.
- **NAV2 turn** — moves `item[subject]` along the page's `item_axis`, wrapping. `kNone`
  pages ignore it.
- **NAV2 press** — enters or leaves whatever NAV2 is currently walking. One rule for the
  browser, the modulation view and modals.
- **Part button** — sets `part`. Leaves `subject`, `group`, `item` and `mode` untouched.
- **MOD down** — enters `kModArm`. **MOD up within `kLongPressMs` with no other input** —
  toggles `kModView` instead. **MOD up otherwise** — returns to the prior mode.
- **Group button** — cycles `group` over `[0, n_groups)`.
- **OUT button** — if `subject != kOut`, writes `{subject, group, focus_col}` into
  `NavState::prev` and jumps to `kOut`; otherwise restores all three from `prev`. Self-inverse, so it needs no LED and cannot strand
  the user on a page they did not choose. `item` is per-subject and survives on its own.
  The round trip is **lossless**: a glance at `kOut` mid-edit returns to the exact column
  group and focused column that was left, because a glance that costs you your place is not a
  glance. `kOut`'s own group is always 0 — it has one group — so nothing needs saving on that
  side.

### Route creation

```cpp
bool InteractionCreateRoute(std::uint8_t part, ModSourceId src, ParamId dst, float amount);
```

- Finds the existing route matching `(part, src, dst)` and updates its amount, or allocates
  the lowest free slot.
- **Returns false** when no slot is free. The caller renders the alert — the emphasis
  ladder's one bright fill per screen (`panel-ui-design-state.md` §4).
- Setting an amount to exactly zero does **not** free the slot: zero is a distinct, visible
  state ("present but silent"), and the split well renders it as such.

## 9. System Invariants

1. **Input never draws, and never invalidates directly.** No code path from
   `InteractionOnInput` reaches a drawing primitive or writes `DynSlot::dirty`,
   `Panel::pending` or the damage list. The only output channels are the engine API and
   `MarkDirty`.
2. **Every column resolves.** For every page and group, `n_cols <= geom::kColumns`, and every
   entry in `cols[0..n_cols)` carries a valid operand for its `ColumnKind`, or is `kPending`.
3. **Pane labels fit.** Every `PageDesc::label` is at most
   $\lfloor (\text{kPaneW} - \text{bracket} - 2\cdot\text{pad}) / \text{advance} \rfloor$
   characters. The give is the pane slack and the left margin, never the column pitch (study
   §4.4).
4. **No latched mode without a physical indicator.** `kModView` and `kPerform` are latched and
   each drives an LED. `kModArm` is momentary. No mode is signalled by screen state alone.
5. **At most one latched mode.** `kModView` and `kPerform` are mutually exclusive.
6. **A part change alters values only.** `subject`, `group`, `item`, `focus_col` and `mode`
   are invariant across a part button press, so chrome is never re-interpreted.
7. **No modifier.** No gesture in this design requires a general-purpose shift. A function
   unreachable without one indicates a layout defect, not a missing control.
8. **No literal geometry.** Layout numbers appear only in `geom`, derived from its parameters.
9. **No literal control counts.** No code outside `geom` and `SurfaceProfile` assumes a number
   of encoders, columns or buttons. A loop over columns bounds on `geom::kColumns`; a loop
   over physical controls bounds on `Surface().n_map`.
10. **Grouping is derived.** No `PageDesc` states a group count or a per-group column list;
    both come from `GroupCount` and `Column`. A page authored at one $E$ is valid at
    every $E$.
11. **Feel never changes shape.** No field of `FeelProfile` can alter which parameter a control
    drives, how many columns exist, or what is drawn where — only how far a turn moves and how
    long a press must be held. A tunable that changes a binding is a design parameter and
    belongs in `geom` or `PageDesc`.
12. **Bounded work per event.** No allocation, no unbounded loop; resolution is a table lookup.

## 10. Test Architecture

Three tiers, two of which need no framebuffer.

**Gesture recognition** — a table-driven unit test over `(InputEvent…) → Gesture…`. The cases
that matter are the disambiguation boundaries: press with no detent under 500 ms; press with
no detent over 500 ms; press with a detent at 10 ms; press with a detent at 490 ms; detent
after release. Pure, no dependencies.

**Binding resolution** — exhaustive. `ResolveBinding` is pure and its domain is finite:
$|\text{SubjectId}| \times |\text{Control}| \times |\text{ViewMode}| \times \text{groups}$,
a few thousand cases. Assert totality, assert invariant 2, assert `kPending` never leaks as
`kParam`. This is the highest-value test in the component and it runs in milliseconds.

**Page table validation** — a test that walks `g_pages` and checks invariants 2 and 3, plus a
`static_assert` sweep on label lengths. Because grouping is derived, the useful form is a
**parameter sweep**: compile the page table at $E \in \{4,5,6,7\}$ and assert every page
remains coherent at each. That converts "does $E = 5$ work" from an argument into a build.

**Surface profiles** — one test per profile asserting the `ControlMap` is injective in both
directions over the controls it claims, and that every `Control` the page table can resolve is
either mapped or explicitly absent. A prototype whose table forgets the group button should
fail a test, not a session.

**Golden images** — the existing `test_panel` hash and `test_mockup_chrome` continue to cover
rendering. This component adds no drawing, so it adds no golden images; it adds *state* that
the DYN hooks read, so the mockup tool gains canned `NavState` values.

**Interaction cost instrumentation** — the simulator logs $S(\tau)$, page and modifier actions
per task, separately from $A(\tau)$, total discrete actions, over a fixed task suite. $S(\tau)$
is a property of the layout rather than of the encoder, so it validates $E$ without hardware.

## 11. Acceptance Criteria

- [ ] Given any `(NavState, Control)` pair, `ResolveBinding` returns without reading or
      writing global state beyond `g_pages`.
- [ ] Given a column bound to a valid `ParamId`, a turn of that encoder calls
      `EngineSetParam` for the current part and no other parameter changes.
- [ ] Given a press held past 500 ms with no detent, the parameter returns to
      `g_params[id].def` and no intermediate value is written.
- [ ] Given a press with a detent arriving at any point before release, the gesture is
      `kHoldTurn` and no press gesture is emitted on release.
- [ ] Given `kEdit` and a part button press, `subject`, `group`, `item`, `focus_col` and
      `mode` are unchanged and only value slots are marked dirty.
- [ ] Given MOD held, an armed source, and a turn of encoder $n$, a route from the armed
      source to `cols[n]` exists with the turned amount; on MOD release, `mode` returns to
      its prior value and the columns show values again.
- [ ] Given MOD held with all route slots occupied and no matching route, no route is created
      and the caller receives `false`.
- [ ] Given a route amount turned to exactly zero, the route remains allocated.
- [ ] Given NAV1 turned `kCount` times from any subject, the pane cursor returns to its
      starting subject.
- [ ] Given a page with `n_groups > 1`, the group button cycles groups and each column
      encoder drives the parameter in the active group.
- [ ] Given a column marked `kPending`, a turn of that encoder changes no engine state.
- [ ] Given a `kRouteField` column, its binding carries the slot from `item[subject]`.
- [ ] Given the OUT button pressed twice from any page, `subject`, `group` and `focus_col`
      are identical to their values before the first press.
- [ ] Every `PageDesc` satisfies `n_cols <= geom::kColumns` for all groups, checked at build
      time.
- [ ] Changing `geom::kColumns` and rebuilding produces a coherent layout or a compile error;
      it never produces a silently overlapping one.
- [ ] The page table compiles and validates at $E \in \{4,5,6,7\}$ with no page edited.
- [ ] Given a surface profile with more encoders than `geom::kColumns`, the surplus is
      unmapped and generates no events.
- [ ] Given a surface profile with fewer encoders than `geom::kColumns`, every column remains
      reachable through the group button and the shortfall is reported once at startup.
- [ ] Given a change to any `FeelProfile` field, no binding changes: the same control drives
      the same parameter before and after.
- [ ] Given the same `FeelProfile` applied to surfaces with different `detents_per_rev`, a
      full revolution moves a parameter by the same amount.
- [ ] No symbol in `nostromo/interaction.*` references a drawing primitive, `DynSlot::dirty`,
      `Panel::pending` or `spike::Damage`.
- [ ] Given any state change that alters a rendered region, `MarkDirty` is called for that
      slot and the region is repainted into both buffers with no visible flicker on swap.

## 12. Code Pointers

| File | Purpose |
|---|---|
| `nostromo/geom.h` | Parameterised geometry; the only home for layout numbers |
| `nostromo/interaction.{h,cc}` | `NavState`, `GestureRecognizer`, `Dispatcher`, `InteractionOnInput` |
| `nostromo/pages.{h,cc}` | `PageDesc`, `ColumnSpec`, `g_pages`, `GroupCount`, `Column`, `ResolveBinding` |
| `nostromo/surface.{h,cc}` | `SurfaceProfile`, `ControlMap`, one table per prototype |
| `nostromo/feel.{h,cc}` | `FeelProfile`, `g_feel`, CONF-page bindings |
| `nostromo/screens.{h,cc}` | Existing — descriptor chrome and DYN hooks; hooks read `NavState` |
| `nostromo/panel.{h,cc}` | Existing — owns `spike::Damage`, `pending[]`, and `MarkDirty`; the sole invalidation entry point |
| `spike/descriptor.h` | Existing — `DynSlot`, `DescriptorCtx` |
| `engine/params.h` | `ParamDesc` gains `accel_max`, `zero_notch` (§7.8) |
| `engine/engine.h` | **Changes required** — `ParamId` semantics and the `EngineSetParam` signature (§7.5, §13.6). `EngineSetRoute` and `ModSourceId` unchanged |
| `tests/test_bindings.cc` | Exhaustive resolution and page-table validation |
| `tests/test_gestures.cc` | Gesture recognition boundaries |

## 13. Open Questions

Draft-only. Each must close or move before `approved`.

1. **Ordinary-parameter acceleration coefficient.** The tunable carried from study §7.1.
   Starting value: `accel_max = 4` above 8 detents/second, `fine_divisor = 10`. This is *not*
   gated on final hardware — it is measured on each prototype and travels with its
   `SurfaceProfile`, normalised by `detents_per_rev`. It closes when a value survives two
   successive prototypes unchanged.
2. **Part hues.** The tunable carried from study §7.4. Four hues against the five-entry
   palette (`kBg/kFaint/kDim/kMid/kBright`), carrying one meaning only — part identity, on
   the title-bar indicator and the button LEDs. Verified on the panel.
3. **The pending parameter surface.** Most pages are declared with `kPending` columns
   because `ParamId` holds 11 entries against a target near 130. Closing this is engine work,
   not interaction work. It does **not** gate starting: build the mechanism against the
   `ParamId`s that exist, let `kPending` carry the rest, and treat `approved` as the
   milestone at which the taxonomy is complete rather than a precondition for implementation.
   The project is at a learning stage and iteration speed matters more than a complete table.
   See §13.4 for the sizing question and §13.6 for the structural one, which does block.
4. **Whether $E = 5$ survives the full parameter surface.** The study's five-parameter module
   lists were drawn from the routing study's *modulation destination* list.
   [`synth-routing_arch-design.md`](synth-routing_arch-design.md) makes the exclusion
   explicit: it defines a destination as "a modulatable parameter" and scopes `ParamId` to
   that subset — osc pitch coarse/fine, wave/shape index, cutoff, resonance, amp, pan, the
   three LFO rates, the three envelopes' A/D/S/R, and the two send amounts. Discrete and
   configuration parameters are therefore outside `ParamId`'s stated scope in the document
   that owns it: filter mode, oscillator wave select, LFO shape and sync, envelope curve,
   MIDI channel, mono/poly, glide, bend range.
   Counting those, a first pass puts oscillator near 7, LFO near 7, filter and envelope and
   part near 6. If that holds, most pages carry two column groups at $E = 5$ rather than the
   one the study projected. This does not invalidate $E = 5$ — §4.7 sanctions pagination and
   `PageDesc` already carries `n_groups` — but it does weaken the §4.4 argument that five
   covers the common case, and it is the reason `geom::kColumns` is a parameter rather than a
   constant. Resolve by completing §13.3 and re-running the count. If most pages still need
   two groups at six, that is a new design study, not an edit to this one.

   **First count, from the page table (§7.6).** At $E = 5$, thirteen of eighteen pages carry
   two column groups: `kPart`, the four oscillators, `kFilt`, the three envelopes, the three
   LFOs, and `kFx`. At $E = 6$ only the oscillators and LFOs do — seven of eighteen. At
   $E = 7$ every page fits in one, but pitch falls to 19.4 mm, below the ergonomic floor the
   `static_assert` enforces. So the real choice is five against six, at 27.1 mm and 22.6 mm
   pitch respectively, and the study's claim that five covers the common case does not
   survive its own parameter lists once discrete parameters are counted. This is not resolved
   here: `geom::kColumns` is a parameter precisely so the answer can come from a prototype
   rather than from arithmetic.
5. **A slot's damage extent is described twice.** `MarkDirty` hardcodes the fact that plot
   slot *n*'s hook also paints a readout at `kReadoutY`, outside the slot's own rect. The hook
   and `MarkDirty` are two descriptions of what a region touches with nothing enforcing
   agreement — the `DrawChrome`/`grat[]` hazard (`panel-ui-design-state.md` §7) in a second
   place. It is tolerable for four plots and will not be for live columns, where every column
   paints a header, a value, a well and possibly a route list. Resolve by giving the slot its
   full extent — a second rect on `DynSlot`, or a DYN op reserving the union — before the
   column slots are built. *Affects `nostromo/panel.cc`; not blocking, but cheapest now.*
6. **The engine parameter API (structural, blocking the addressing model).** §7.5 requires two
   changes to `ParamId` that are semantic, not additive, and neither is covered by §13.3:
   - **An instance argument.** `EngineSetParam(id, value)` becomes
     `EngineSetParam(part, instance, id, value)`. Kind-not-instance addressing is what keeps
     `ParamId` near 39 entries instead of 130 and lets the four oscillator pages share one
     column list; without it, `g_pages` and the enum both quadruple.
   - **Scope beyond modulatable parameters.** `synth-routing_arch-design.md` defines `ParamId`
     as the set of *modulatable* destinations. The interaction layer addresses every
     parameter, including discrete ones — filter mode, wave select, LFO shape and sync,
     mono/poly. Either `ParamId` widens and modulation takes a subset of it, or the two
     addressing spaces diverge and every column carries a tag saying which it is. The first is
     simpler; both are engine decisions.

   This is engine-wide and structural, so it is a *dependency*, not interaction work: this
   document cannot be planned against until it is settled, whereas §13.3's growth of the
   parameter set can proceed alongside. *Owner: `synth-routing_arch-design.md` and
   `engine/engine.h`. Blocks: §7.5's addressing model, §7.6's discrete columns, §7.7's
   `kParam` resolution, and the plan.*
7. **`MarkDirty`'s signature.** It is an internal helper taking a bare `int`; `MarkDirty(p, 3)`
   at `panel.cc:894` means the output plot only by convention. For this layer to call it, it
   needs to be a declared entry point in `panel.h` taking `SlotIdx`. Trivial, and it should
   land with the first interaction code rather than after it.

**Not a defect:** `Panel::pending[]` and `spike::Damage`'s two-frame rule look like duplicate
mechanisms and are complementary — `pending` decides whether a *hook re-runs* into the second
buffer, `damage` decides which *rects* are repainted. Both are required under double
buffering. Worth a comment in the code, since the next reader will suspect duplication.
