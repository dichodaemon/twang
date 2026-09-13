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
  controls (§7.7), so a new prototype is a table rather than a code change.
- **Feel is runtime-tunable.** Detent scaling, acceleration threshold, long-press duration and
  the fine divisor live in a mutable `FeelProfile` edited from the CONF page (§7.8). A feel
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
  `EngineSetRoute` surface. No new engine entry points, with one exception in §7.6.
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
design. It belongs in `ParamDesc` beside range and formatter (§7.6).

**Column assignments are data, not code.** `PageTable` is a table so that pages fill in as the
engine's parameter surface grows, without touching this component.

**Column grouping is computed, not authored.** A page declares an ordered `ParamId` list;
groups are $\lceil n/E \rceil$ slices of it. Order carries meaning — the head of the list is
the page's hot set and stays the first group at any $E$ — which expresses the study's
"declared hot set" in the type rather than in a comment.

**Feel is state, not constants.** Anything judged by hand is tunable at runtime and editable
from the CONF page. Anything that changes the shape of the design is compile-time. That line
is the line between §7.8 and §7.1.

**One owner for invalidation.** This layer could set `DynSlot::dirty` itself and save a call,
but the flag is one of three things `MarkDirty` updates together, and a caller that knows only
about the flag cannot know a slot's full damage extent. Two writers of one invariant is the
`DrawChrome`/`grat[]` failure recorded in `panel-ui-design-state.md` §7. The panel keeps
ownership; this layer reports.

## 6. Component Lifecycle

The layer is a singleton initialised once and never destroyed.

1. **Init** — `InteractionInit(DynSlot *slots, int n_slots, const SurfaceProfile &surface)`.
   Binds the slot array and the surface, loads `g_feel` from persisted settings or its
   defaults, zeroes `NavState` to part 0, subject `kFilt`, group 0, mode `kEdit`, and marks
   all slots dirty. Reports once if `surface.n_encoders < geom::kColumns`.
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

// --- Derived. Never write these as literals elsewhere. ---
inline constexpr float kPxMm    = kPanelWmm / kFbWidth;    // 0.150600
inline constexpr float kPyMm    = kPanelHmm / kFbHeight;   // 0.143200
inline constexpr float kAspect  = kPxMm / kPyMm;           // 1.0517 — pixels are wider
inline constexpr int   kColW    = (kFbWidth - 2 * kMargin - kPaneW) / kColumns;
inline constexpr int   kColX(int n) { return kMargin + kPaneW + n * kColW; }
inline constexpr float kPitchMm = kColW * kPxMm;

// A configuration that cannot be built must not compile.
static_assert(kColumns * kColW + kPaneW + 2 * kMargin == kFbWidth,
              "columns must tile the content width exactly");
static_assert(kPitchMm >= kKnobDiaMm + kFingerGapMm,
              "encoder pitch below the ergonomic floor: reduce kColumns "
              "or widen the panel");
}  // namespace nostromo::geom
```

`kAspect` is the correction factor for any drawing that must read as geometrically square:
a shape intended to be $h$ px tall and visually square is $\lceil h / \text{kAspect} \rceil$
px wide.

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

Press timing is not a constant here; it is `g_feel.long_press_ms` (§7.8), starting at 500 ms.

### 7.3. Navigation state

```cpp
enum class SubjectId : std::uint8_t {
  kPart = 0,                       ///< part-level settings
  kOsc1, kOsc2, kOsc3, kOsc4,
  kMix, kFilt, kAmp,
  kEnv1, kEnv2, kEnv3,
  kLfo1, kLfo2, kLfo3,
  kMod, kFx,
  kPatch, kConf,                   ///< globals, below the pane rule
  kCount,
};

enum class ViewMode : std::uint8_t {
  kEdit = 0,   ///< default
  kModArm,     ///< MOD held — momentary
  kModView,    ///< MOD tapped — latched, LED lit
  kPerform,    ///< PERF — latched, LED lit
};

struct NavState {
  std::uint8_t part;                  ///< [0, kNumParts)
  SubjectId    subject;               ///< pane cursor; global across parts
  std::uint8_t group;                 ///< active column group
  std::uint8_t item[static_cast<int>(SubjectId::kCount)];  ///< per-page item cursor
  std::int8_t  focus_col;             ///< focused column, -1 = none
  ViewMode     mode;
  ModSourceId  armed_source;          ///< persists between kModArm entries
};
```

`subject` and `group` are deliberately *not* per-part: a part change alters values only
(study §4.11). `item` is per-subject because a page's item cursor is a property of that page,
not of the navigation as a whole.

### 7.4. Pages

```cpp
inline constexpr ParamId kParamPending = ParamId::kCount;             ///< declared, engine lacks it
inline constexpr ParamId kParamNone    = static_cast<ParamId>(0xFF);  ///< past the end of a group

enum class ItemAxis : std::uint8_t {
  kNone = 0,   ///< NAV2 idle
  kSlots,      ///< modulation slots
  kPatches,    ///< patch list within the selected category
  kRoutes,     ///< route list of the focused column
};

struct PageDesc {
  SubjectId      subject;
  const char    *label;     ///< pane text; see the length invariant in §9
  const ParamId *params;    ///< ORDERED. Head of the list is the hot set.
  std::uint8_t   n_params;  ///< grouping is derived, not authored
  ItemAxis       item_axis;
  std::int8_t    dyn_slot;  ///< plot slot, or -1
};

/// Groups are ceil(n/E) slices of `params`. Changing geom::kColumns re-groups
/// every page; no page is re-authored.
constexpr int GroupCount(const PageDesc &p) {
  return (p.n_params + geom::kColumns - 1) / geom::kColumns;
}

constexpr ParamId ColumnParam(const PageDesc &p, int group, int col) {
  const int i = group * geom::kColumns + col;
  return i < p.n_params ? p.params[i] : kParamNone;
}

extern const PageDesc g_pages[static_cast<int>(SubjectId::kCount)];
```

Two sentinels, rendered differently. `kParamPending` marks a column whose parameter the engine
does not yet define: header dim, value empty, input ignored. It keeps the page taxonomy
complete and honest while `ParamId` grows from its current 11 entries toward the target
surface. `kParamNone` marks a column past the end of a partial final group: nothing drawn, the
encoder inert.

Ordering is the page author's only control over packing, and it is enough. Put the parameters
that carry most of the editing first and they occupy group 0 at $E = 5$; at $E = 6$ they
occupy group 0 with a sixth alongside. Nothing is re-authored either way.

### 7.5. Bindings

```cpp
enum class BindKind : std::uint8_t {
  kNone = 0,
  kParam,        ///< a parameter of the current subject
  kRouteAmount,  ///< a route amount (kModArm / kModView)
  kNavSubject, kNavItem,
  kPartSelect, kModeToggle, kGroupCycle,
  kPending,      ///< a declared but unimplemented column
};

struct Binding {
  BindKind    kind;
  ParamId     param;    ///< kParam, kRouteAmount
  std::int8_t slot;     ///< kRouteAmount: engine route slot, -1 to allocate
  std::int8_t column;   ///< originating column, -1 if not column-derived
};

Binding ResolveBinding(const NavState &nav, Control c);
```

### 7.6. Change required in `engine/params.h`

Acceleration is a property of the parameter, so `ParamDesc` gains two fields:

```cpp
struct ParamDesc {
  // ... existing: name, unit, disp_min, disp_max, def, curve, offset, comb
  std::uint8_t accel_max;   ///< 1 = none; 3 = capped 3x (modulation amounts)
  bool         zero_notch;  ///< require one extra detent to cross zero
};
```

This is the only change this design imposes outside `nostromo/`. `accel_max` defaults to
`FeelProfile::accel_max_default` (§7.8); `zero_notch` is true for bipolar amounts, whose zero
state the split well renders distinctly.

### 7.7. Surface profile

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

### 7.8. Feel profile

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
- **Postcondition on `kParam`**: `param` is a valid `ParamId` — never `kParamPending`, which
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
   entry in `cols[0..n_cols)` is either a valid `ParamId` or `kParamPending`.
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
    both come from `GroupCount` and `ColumnParam`. A page authored at one $E$ is valid at
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
- [ ] Given a column marked `kParamPending`, a turn of that encoder changes no engine state.
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
| `nostromo/pages.{h,cc}` | `PageDesc`, `g_pages`, `GroupCount`, `ColumnParam`, `ResolveBinding` |
| `nostromo/surface.{h,cc}` | `SurfaceProfile`, `ControlMap`, one table per prototype |
| `nostromo/feel.{h,cc}` | `FeelProfile`, `g_feel`, CONF-page bindings |
| `nostromo/screens.{h,cc}` | Existing — descriptor chrome and DYN hooks; hooks read `NavState` |
| `nostromo/panel.{h,cc}` | Existing — owns `spike::Damage`, `pending[]`, and `MarkDirty`; the sole invalidation entry point |
| `spike/descriptor.h` | Existing — `DynSlot`, `DescriptorCtx` |
| `engine/params.h` | `ParamDesc` gains `accel_max`, `zero_notch` (§7.6) |
| `engine/engine.h` | Existing — `EngineSetParam`, `EngineSetRoute`, `ModSourceId`, `ParamId` |
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
3. **The pending parameter surface.** Most pages are declared with `kParamPending` columns
   because `ParamId` holds 11 entries against a target near 130. Closing this is engine work,
   not interaction work. It does **not** gate starting: build the mechanism against the
   `ParamId`s that exist, let `kParamPending` carry the rest, and treat `approved` as the
   milestone at which the taxonomy is complete rather than a precondition for implementation.
   The project is at a learning stage and iteration speed matters more than a complete table.
   See §13.4 for the part that does need resolving.
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
5. **A slot's damage extent is described twice.** `MarkDirty` hardcodes the fact that plot
   slot *n*'s hook also paints a readout at `kReadoutY`, outside the slot's own rect. The hook
   and `MarkDirty` are two descriptions of what a region touches with nothing enforcing
   agreement — the `DrawChrome`/`grat[]` hazard (`panel-ui-design-state.md` §7) in a second
   place. It is tolerable for four plots and will not be for live columns, where every column
   paints a header, a value, a well and possibly a route list. Resolve by giving the slot its
   full extent — a second rect on `DynSlot`, or a DYN op reserving the union — before the
   column slots are built. *Affects `nostromo/panel.cc`; not blocking, but cheapest now.*
6. **`MarkDirty`'s signature.** It is an internal helper taking a bare `int`; `MarkDirty(p, 3)`
   at `panel.cc:894` means the output plot only by convention. For this layer to call it, it
   needs to be a declared entry point in `panel.h` taking `SlotIdx`. Trivial, and it should
   land with the first interaction code rather than after it.

**Not a defect:** `Panel::pending[]` and `spike::Damage`'s two-frame rule look like duplicate
mechanisms and are complementary — `pending` decides whether a *hook re-runs* into the second
buffer, `damage` decides which *rects* are repainted. Both are required under double
buffering. Worth a comment in the code, since the next reader will suspect duplication.
