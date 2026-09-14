---
title: Nostromo Interaction Layer -- Implementation Plan
status: draft
date: 2026-09-14
author: Dizan Vasquez
arch-design: ../arch-designs/nostromo-interaction_arch-design.md
---

# Nostromo Interaction Layer

This plan is the *how* for the interaction layer defined in
`../arch-designs/nostromo-interaction_arch-design.md`. The *what* and *why* are the arch-design
and its design study (`../design-studies/2026-09-12_panel-interaction-model_design-study.md`);
this plan sequences the work and names the files without duplicating their contracts. The
parameter-surface contract the layer consumes is
`../arch-designs/engine-parameter-surface_arch-design.md`.

Scope: the five components of §5 — `SurfaceProfile`, `GestureRecognizer`, `NavState`,
`PageTable`/`BindingResolver`, `Dispatcher` — their unit tests, and the host wiring that puts
them in front of the simulator's input. The §13 tunables (acceleration coefficient, part hues,
$E = 5$ vs 6) are measured *during* this work, not resolved beforehand; the parameter-surface
growth (§13.3) is additive and non-blocking. `geom.h`, `screens.*`, `panel.*`, and
`spike/descriptor.h` already exist and are consumed, not rewritten.

## 1. Phases

1. **Vocabulary** — type definitions and function declarations across the four new `nostromo/`
   headers, plus the `ParamDesc` acceleration fields. No behavior (no deps).
2. **Static data** — `g_pages`, `g_feel`, and the first `SurfaceProfile` table. Depends on phase 1.
3. **Engine route read accessor** — `EngineGetRoute` + `ParamBlock::GetRoute` (the one
   deviation from §4; see Design Decisions). Depends on nothing; phase 5 consumes it.
4. **Pure logic + tests** — `ResolveBinding`, `GestureRecognizer`, and their exhaustive tests.
   Depends on phases 1–2.
5. **Dispatch + lifecycle** — `Dispatcher`, `InteractionOnInput`, `InteractionCreateRoute`,
   `InteractionInit`, and the end-to-end test. Depends on phases 3–4.
6. **Host integration** — wire the simulator's input through the layer. Depends on phase 5.
7. **Documentation** — record the deviations and the `ParamDesc` growth in the two arch-designs.
   Depends on all implementation phases.

## 2. Implementation Status

| # | Task | Status |
|---|---|---|
| 1.1 | Add `accel_max` (`std::uint8_t`) + `zero_notch` (`bool`) to `ParamDesc` in `engine/params.h`; populate `g_params` in `engine/params.cc` (`accel_max` = 4 for ordinary params, `zero_notch` = true for bipolar `kPitchCoarse`/`kPitchBend`) | Pending |
| 1.2 | Define `Control`, `Edge`, `InputEvent`, `Gesture`, `SubjectId`, `ViewMode`, `NavPos`, `NavState` in `nostromo/interaction.h`; declare `InteractionInit`, `InteractionOnInput`, `InteractionCreateRoute` | Pending |
| 1.3 | Define `ColumnKind`, `RouteField` (kSource/kDest/kAmount), `ViewCtl`, `ColumnSpec`, `ItemAxis`, `PageDesc`, `BindKind`, `Binding`, `GroupCount<E>`, `Column<E>` in `nostromo/pages.h`; declare `ResolveBinding` and `extern g_pages` | Pending |
| 1.4 | Define `FeelProfile` + `extern g_feel` in `nostromo/feel.h` | Pending |
| 1.5 | Define `ControlMap`, `SurfaceProfile`, `Surface()`, `SetSurface()` in `nostromo/surface.h` | Pending |
| 1.6 | Verify: `cmake --build /tmp/twang-build` compiles with the new headers | Pending |
| 2.1 | Author `g_pages` (20 pages per arch-design §7.6) in `nostromo/pages.cc`; `curve`/`enable` MOD columns are `kPending`; register `pages.cc` in `CMakeLists.txt` | Pending |
| 2.2 | Define `g_feel` defaults (detents_per_rev 24, accel_max_default 4, accel_threshold_dps 8, long_press_ms 500, fine_divisor 10) in `nostromo/feel.cc`; register in `CMakeLists.txt` | Pending |
| 2.3 | Implement `Surface()`/`SetSurface()` + the `xtouch-compact` `SurfaceProfile` table in `nostromo/surface.cc`; register in `CMakeLists.txt` | Pending |
| 2.4 | Verify: `cmake --build /tmp/twang-build`; `static_assert(SubjectId::kCount == geom::kSubjectCount)` holds | Pending |
| 3.1 | Add `bool ParamBlock::GetRoute(int part, int slot, ModRoute *out) const` in `engine/ipc.h`/`engine/ipc.cc` | Pending |
| 3.2 | Add `bool EngineGetRoute(int part, int slot, ModRoute *out)` in `engine/engine.h`/`engine/engine.cc` | Pending |
| 3.3 | Write test: extend `tests/test_mod_route.cc` with a route-read round-trip (`SetRoute` then `GetRoute` returns it; empty slot returns `false`) | Pending |
| 3.4 | Verify: `cmake --build /tmp/twang-build --target engine test_mod_route && /tmp/twang-build/test_mod_route` | Pending |
| 4.1 | Implement `ResolveBinding` (pure) in `nostromo/pages.cc` per §7.7/§8 resolution tables | Pending |
| 4.2 | Write test: `tests/test_bindings.cc` — exhaustive `(nav, control)` resolution, invariants 2/3, `kPending` never leaks as `kParam`, and the $E \in \{4,5,6,7\}$ page-table sweep; register in `CMakeLists.txt` | Pending |
| 4.3 | Implement `GestureRecognizer` (stateful per control) in `nostromo/interaction.cc`; register `interaction.cc` in `CMakeLists.txt` | Pending |
| 4.4 | Write test: `tests/test_gestures.cc` — press/turn disambiguation boundaries; register in `CMakeLists.txt` | Pending |
| 4.5 | Write test: `tests/test_surface.cc` — `ControlMap` injectivity (both directions); register in `CMakeLists.txt` | Pending |
| 4.6 | Verify: `cmake --build /tmp/twang-build --target test_bindings test_gestures test_surface && /tmp/twang-build/test_bindings && /tmp/twang-build/test_gestures && /tmp/twang-build/test_surface` | Pending |
| 5.1 | Implement `Dispatcher` (apply `Gesture`→`Binding`; engine writes + `MarkDirty`; acceleration) in `nostromo/interaction.cc` | Pending |
| 5.2 | Implement `InteractionOnInput` (per-event flow; §8 contract) in `nostromo/interaction.cc` | Pending |
| 5.3 | Implement `InteractionCreateRoute` (find-or-allocate via `EngineGetRoute`; §8) in `nostromo/interaction.cc` | Pending |
| 5.4 | Implement `InteractionInit` (bind slots + surface; zero `NavState`; §6) in `nostromo/interaction.cc` | Pending |
| 5.5 | Write test: `tests/test_interaction.cc` — turn→`EngineSetParam`, MOD-arm→route creation, part change leaves `subject`/`group`/`item`/`mode` untouched, `MarkDirty` observed via `PanelPlotDraws`; register in `CMakeLists.txt` | Pending |
| 5.6 | Verify: `cmake --build /tmp/twang-build --target test_interaction && /tmp/twang-build/test_interaction` | Pending |
| 6.1 | Replace the `MidiCc`→param path in `host/midi_io.cc` with `SurfaceProfile` mapping → `InteractionOnInput` (CC→`Control`, detent derivation from deltas) | Pending |
| 6.2 | Call `InteractionInit` at startup in `host/main.cc`; add keyboard→`InputEvent` (NAV1/NAV2/encoders for dev) in `host/sdl_backend.cc` | Pending |
| 6.3 | Verify: `cmake --build /tmp/twang-build`; simulator smoke test (user-driven: encoders drive parameters, MOD arms a route, NAV1 walks the pane) | Pending |
| 7.1 | Update `nostromo-interaction_arch-design.md`: record the `EngineGetRoute` deviation (§4), the `curve`/`enable`-as-pending decision (§7.4), the `SubjectId` enum/pane order reconciliation (§7.3 vs §7.6) | Pending |
| 7.2 | Update `engine-parameter-surface_arch-design.md` §6: add `accel_max`/`zero_notch` to the `ParamDesc` struct | Pending |
| 7.3 | Verify: full `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` green (18 existing + 5 new tests) | Pending |

## 3. Architecture

### 3.1 Directory layout

| File | Change |
|---|---|
| `engine/params.h`, `engine/params.cc` | Add `accel_max`/`zero_notch` to `ParamDesc`; set values in `g_params` |
| `engine/ipc.h`, `engine/ipc.cc` | Add `ParamBlock::GetRoute` |
| `engine/engine.h`, `engine/engine.cc` | Add `EngineGetRoute` |
| `nostromo/interaction.h` | New: `Control`, `Edge`, `InputEvent`, `Gesture`, `SubjectId`, `ViewMode`, `NavPos`, `NavState` + `Interaction*` declarations |
| `nostromo/interaction.cc` | New: `GestureRecognizer`, `Dispatcher`, `InteractionOnInput`, `InteractionCreateRoute`, `InteractionInit` |
| `nostromo/pages.h` | New: `ColumnKind`, `RouteField`, `ViewCtl`, `ColumnSpec`, `ItemAxis`, `PageDesc`, `BindKind`, `Binding`, `GroupCount<E>`, `Column<E>`, `ResolveBinding` |
| `nostromo/pages.cc` | New: `g_pages`, `ResolveBinding` |
| `nostromo/surface.h`, `nostromo/surface.cc` | New: `ControlMap`, `SurfaceProfile`, `Surface()`/`SetSurface()`, `xtouch-compact` table |
| `nostromo/feel.h`, `nostromo/feel.cc` | New: `FeelProfile`, `g_feel` |
| `tests/test_bindings.cc`, `tests/test_gestures.cc`, `tests/test_surface.cc`, `tests/test_interaction.cc` | New: unit + integration tests |
| `tests/test_mod_route.cc` | Add route-read round-trip checks |
| `host/midi_io.cc`, `host/midi_io.h` | Replace `MidiCc`→param path with `InteractionOnInput` |
| `host/main.cc`, `host/sdl_backend.cc` | `InteractionInit` at startup; keyboard→`InputEvent` |
| `CMakeLists.txt` | Add the 4 new `nostromo/` sources; register 4 new test executables |
| `nostromo/geom.h`, `nostromo/screens.{h,cc}`, `nostromo/panel.{h,cc}`, `spike/descriptor.h` | Existing — consumed, no change |

### 3.2 Dependency graph

```
nostromo/pages.{h,cc}   ──> nostromo/interaction.h (SubjectId, Control, NavState)
                            engine/engine.h (ParamId), nostromo/geom.h (kColumns)
nostromo/interaction.cc ──> nostromo/pages.h (ResolveBinding, g_pages)
                            nostromo/surface.h, nostromo/feel.h, nostromo/panel.h (MarkDirty)
                            engine/engine.h (EngineSetParam, EngineGetRoute, EngineSetRoute)
host/midi_io.cc         ──> nostromo/surface.h, nostromo/interaction.h
tests/test_*            ──> nostromo (lib), engine
```

No new inter-package edge: `nostromo` already links `engine` and `spike`. The one new *engine*
symbol is `EngineGetRoute` — a read mirror of the existing `EngineSetRoute`, and the only
engine change this layer requires beyond the `ParamDesc` fields (§7.8).

## 4. Interface Changes

Grouped by file. The arch-design §7 is the authority for semantics; these are the exact
signatures the implementer matches.

### `engine/params.h` — `ParamDesc` grows two fields

```cpp
struct ParamDesc {
    // ... existing: name, unit, disp_min, disp_max, def, curve, base, stride,
    //              modulatable, labels, n_labels, comb
    std::uint8_t accel_max;   ///< acceleration cap: 1 = none, N = capped at Nx
    bool         zero_notch;  ///< require one extra detent to cross zero (bipolar)
};
```

Values in `g_params` (task 1.1): `accel_max = 4` for ordinary parameters, `zero_notch = true`
for bipolar `kPitchCoarse`/`kPitchBend`. The "modulation amount" policy (§7.8's "3 = capped
3x") is *not* a `ParamDesc` field — amounts are route fields, not parameters — and is applied
as a constant in the `Dispatcher` (see Design Decisions).

### `engine/engine.h` + `engine/ipc.h` — route read accessor (deviation from §4)

```cpp
/// @brief Read one modulation route for a part (control thread).
/// @return true if the slot holds a live route; false if empty or invalid.
bool EngineGetRoute(int part, int slot, ModRoute *out);

// ipc.h
bool ParamBlock::GetRoute(int part, int slot, ModRoute *out) const;  // reads pending_[part]
```

Reads `pending_[part].routes[slot]` — the same authoritative state `SetRoute` writes. `false`
when `part`/`slot` out of range or `source == kNone`. §4's "no new engine entry points" is
amended: this is the one accessor `InteractionCreateRoute` needs to find a matching route or a
free slot.

### `nostromo/interaction.h` — navigation and input types (new)

```cpp
enum class Control : std::uint8_t {
  kNav1 = 0, kNav2, kEnc0, kEncLast = kEnc0 + geom::kColumns - 1,
  kPart0, kPart1, kPart2, kPart3,
  kMod, kPerf, kGroup, kOut, kCount,
};
enum class Edge : std::uint8_t { kNone, kDown, kUp };
struct InputEvent { Control control; std::int8_t detents; Edge edge; std::uint32_t t_ms; };
enum class Gesture : std::uint8_t { kTurn, kHoldTurn, kPressShort, kPressLong };

enum class SubjectId : std::uint8_t { /* 20 subjects, pane order — see Design Decisions */ };
enum class ViewMode : std::uint8_t { kEdit = 0, kModArm, kModView, kPerform };
struct NavPos { SubjectId subject; std::uint8_t group; std::int8_t focus_col; };
struct NavState {
  std::uint8_t part; SubjectId subject; std::uint8_t group;
  std::uint8_t item[static_cast<int>(SubjectId::kCount)];
  std::int8_t focus_col; ViewMode mode; ModSourceId armed_source; NavPos prev;
};

void InteractionInit(spike::DynSlot *slots, int n_slots, const SurfaceProfile &surface);
void InteractionOnInput(const InputEvent &ev);
bool InteractionCreateRoute(std::uint8_t part, ModSourceId src, ParamId dst, float amount);
```

`SubjectId` carries the §7.6 pane order (kPart, kOsc1..4, kFilt, kAmp, kEnv1..3, kLfo1..3,
kMod, kOutScope/Cycle/Spec, kFx, kPatch, kConf), *not* the §7.3 enum order — the two disagree,
and pane order wins because `g_pages` is indexed by `SubjectId` and NAV1 walks index order.
See Design Decisions.

### `nostromo/pages.h` — page and binding types (new)

```cpp
enum class ColumnKind : std::uint8_t { kNone = 0, kParam, kPending, kRouteField, kViewCtl };
enum class RouteField  : std::uint8_t { kSource, kDest, kAmount };
enum class ViewCtl    : std::uint8_t { kCategory, kSort, kFavourite, kAction,   // PATCH
                                       kDetents, kAccelMax, kAccelThresh, kLongPress, kFineDiv };
struct ColumnSpec { ColumnKind kind; union { ParamId param; RouteField field; ViewCtl ctl; }; };
enum class ItemAxis : std::uint8_t { kNone = 0, kSlots, kPatches, kRoutes };
struct PageDesc { SubjectId subject; const char *label; const ColumnSpec *cols;
                  std::uint8_t n_cols; ItemAxis item_axis; std::int8_t dyn_slot; };

template <int E = geom::kColumns>
constexpr int GroupCount(const PageDesc &p) { return (p.n_cols + E - 1) / E; }
template <int E = geom::kColumns>
constexpr ColumnSpec Column(const PageDesc &p, int group, int col);

enum class BindKind : std::uint8_t { kNone = 0, kParam, kRouteField, kViewCtl, kRouteAmount,
                                     kNavSubject, kNavItem, kPartSelect, kModeToggle,
                                     kGroupCycle, kOutToggle, kPending };
struct Binding { BindKind kind; union { ParamId param; RouteField field; ViewCtl ctl; };
                 std::int8_t slot; std::int8_t column; };

extern const PageDesc g_pages[static_cast<int>(SubjectId::kCount)];
Binding ResolveBinding(const NavState &nav, Control c);
```

`RouteField` holds only `kSource`/`kDest`/`kAmount` — the three fields `ModRoute` actually has.
The MOD page's `curve`/`enable` columns are declared `ColumnKind::kPending` (the arch-design's
`kCurve`/`kEnable` are reserved for when `ModRoute` grows; see Design Decisions). `GroupCount`
and `Column` are templates over $E$ so the page-table sweep in `test_bindings.cc` can
instantiate $E \in \{4,5,6,7\}$ without editing `geom::kColumns`.

### `nostromo/feel.h` — `FeelProfile` (new)

```cpp
struct FeelProfile {
  std::uint8_t  detents_per_rev;      ///< encoder detent count
  std::uint8_t  accel_max_default;    ///< ordinary-parameter cap; 1 = none
  std::uint16_t accel_threshold_dps;  ///< detents/second above which accel engages
  std::uint32_t long_press_ms;
  std::uint8_t  fine_divisor;         ///< hold-and-turn divisor (10 = x1/10)
};
extern FeelProfile g_feel;   ///< mutable; edited from the CONF page
```

### `nostromo/surface.h` — `SurfaceProfile` (new)

```cpp
struct ControlMap { std::uint16_t physical; Control logical; };
struct SurfaceProfile { const char *name; const ControlMap *map; std::uint8_t n_map;
                        std::uint8_t n_encoders; std::uint8_t n_buttons; bool has_rings; };
const SurfaceProfile &Surface();
void SetSurface(const SurfaceProfile &p);
```

## 5. Solution Breakdown

### 5.1 `g_pages` (task 2.1)

`nostromo/pages.cc` holds the one `const PageDesc g_pages[SubjectId::kCount]`, authored in
§7.6's pane order. Each row: `subject`, `label`, an ordered `cols[]`, `n_cols`, `item_axis`,
`dyn_slot`. Columns whose `ParamId` exists (`kCutoff`, `kResonance`, `kAttack`, `kDecay`,
`kSustain`, `kRelease`, `kAmp`, `kPitchCoarse`, `kDrive`, `kKeyFollowDepth`) are `kParam`; all
others are `kPending`. The MOD page's `curve`/`enable` columns are `kPending`. `dyn_slot` maps
the page's plot: `kSlotOsc` (osc), `kSlotFilter` (filt), `kSlotEnv` (env/lfo), `kSlotOut`
(out views), `-1` elsewhere.

- **Edge cases**: `n_cols` may exceed `geom::kColumns` (multi-group pages — §13.4's count says
  most pages are); a partial final group is legal and `Column` returns `kNone` past its end.
- **Dependencies**: consumes phase 1 types; produces the table phase 4 resolves against.
- **Done**: task 2.4 — build + `kSubjectCount` assert.

### 5.2 `ResolveBinding` (task 4.1)

Pure `(NavState, Control) → Binding`. Resolution by control class:

- **`kEnc0+n`** — resolve `Column(&g_pages[nav.subject], nav.group, n)`; tag-copy `ColumnKind`
  → `BindKind` (`kParam`→`kParam`, `kPending`→`kPending`, `kRouteField`→`kRouteField`,
  `kViewCtl`→`kViewCtl`, `kNone`→`kNone`). For `kRouteField`, populate `Binding::slot` from
  `nav.item[subject]` (the NAV2 route/slot cursor).
- **`kNav1`** — `kNavSubject`; **`kNav2`** — `kNavItem` (or `kNone` on an `ItemAxis::kNone`
  page).
- **`kPart0..3`** — `kPartSelect`; **`kMod`** — `kModeToggle`; **`kGroup`** — `kGroupCycle`;
  **`kOut`** — `kOutToggle`; **`kPerf`** — `kNone` (reserved, §2).
- **Mode overlay** — in `kModArm`, a column encoder whose column is modulatable resolves
  `kRouteAmount` (route `armed_source → cols[n]`); in `kModView`, `kMod` still toggles the
  mode, and column encoders resolve `kRouteField`/`kRouteAmount` per §8.

- **Edge cases**: a `kPending` column must return `BindKind::kPending`, never `kParam`
  (invariant 2). Unreachable combinations return `kNone` (totality). No engine reads, no
  globals beyond `g_pages`.
- **Dependencies**: consumes `g_pages` (2.1), `NavState`/`Control` (1.2).
- **Done**: task 4.6 — `test_bindings.cc` exhaustive sweep passes.

### 5.3 `GestureRecognizer` (task 4.3)

Stateful per `Control`: holds a press timestamp and a "detent arrived during press" flag. On a
button `InputEvent`, edge `kDown` starts a press; a turn with `detents != 0` while pressed sets
the flag; `kUp` emits `kPressShort` (if under `g_feel.long_press_ms` and no detent),
`kPressLong` (if over), or nothing (if a detent arrived — the §8 detent-absorption rule). A
turn with no press emits `kTurn`; a turn during press emits `kHoldTurn`.

- **Edge cases**: the disambiguation boundaries §10 lists — press/no-detent under 500 ms, over
  500 ms, detent at 10 ms, detent at 490 ms, detent after release. All read `g_feel`, no
  constants.
- **Dependencies**: consumes `FeelProfile` (1.4), `InputEvent`/`Gesture` (1.2).
- **Done**: task 4.6 — `test_gestures.cc` boundaries pass.

### 5.4 `Dispatcher` (task 5.1)

The only side-effecting component. Applies a `(Gesture, Binding)` pair:

- **`kParam` turn** — read `EngineGetParam(part, {instance, id})`, add
  `detents × step(desc, feel)` (step applies the `accel_max` cap above
  `accel_threshold_dps`, `fine_divisor` on `kHoldTurn`, `zero_notch` crossing), clamp,
  `EngineSetParam`; `MarkDirty` the page's `dyn_slot`. The instance is derived from the
  subject (`kOsc3` → instance 2) for multi-instance kinds.
- **`kPressShort` on `kParam`** — descend, if the column is descendable; **`kPressLong`** —
  revert to `g_params[id].def` (no intermediate write).
- **`kRouteAmount` turn** — `InteractionCreateRoute(part, armed_source, dst, amount)`.
- **`kNavSubject`/`kNavItem`/`kPartSelect`/`kModeToggle`/`kGroupCycle`/`kOutToggle`** — update
  `NavState` and `MarkDirty` per §8's navigation and mode contracts.
- **`kPending`** — no-op.

- **Edge cases**: `kPressLong` revert writes the default exactly once, not per detent; a part
  change leaves `subject`/`group`/`item`/`mode` invariant (invariant 6); mode entry/exit
  (`kModArm`↔`kEdit`) dirties pane + all column slots (§6.3).
- **Dependencies**: consumes `ResolveBinding` (4.1), `EngineGetRoute` (3.2), `EngineSetParam`,
  `MarkDirty`.
- **Done**: task 5.6 — `test_interaction.cc` end-to-end passes.

### 5.5 `InteractionCreateRoute` (task 5.3)

Walks `EngineGetRoute(part, slot, &r)` for `slot ∈ [0, kModSlots)`: if `r.source == src &&
r.dst.id == dst`, `EngineSetRoute(part, slot, src, dst, new_amount)` and return `true`; else
remember the lowest `source == kNone` slot. If none matched, `EngineSetRoute` into the free
slot (return `false` if none). Zero amount keeps the slot allocated (§8). Amount changes use
the `kRouteAmount` acceleration cap (constant 3) + `zero_notch`.

- **Edge cases**: full route table with no match → `false` (caller renders the alert). Zero
  amount → route remains (distinct "present but silent" state).
- **Dependencies**: consumes `EngineGetRoute` (3.2) + `EngineSetRoute`.
- **Done**: task 5.6.

### 5.6 `InteractionInit` (task 5.4)

Binds `slots`/`n_slots` (for the init-time full dirty pass and to hand `NavState` to DYN hooks
via their `state` pointer), stores the surface, loads `g_feel` defaults, zeroes `NavState`
(part 0, subject `kOutScope`, `prev {kFilt, 0, -1}`, group 0, `kEdit`), and marks all slots
dirty. Reports once if `surface.n_encoders < geom::kColumns`.

- **Edge cases**: `kOutScope` power-on page (§6.1); the shortfall report is once-only.
- **Dependencies**: consumes `SurfaceProfile`, `MarkDirty`.
- **Done**: task 5.6.

### 5.7 Host wiring (tasks 6.1–6.2)

`midi_io.cc` stops calling `engine::MidiCc(layout, 0, cc, val)` for panel controls; instead it
maps the incoming CC to a `Control` via `Surface().map`, derives signed detents from CC deltas
for encoders and edges for buttons, and calls `InteractionOnInput`. Note-on/off still routes to
`PanelNoteOn`/`PanelNoteOff` (touch/notes are out of scope, §2). `main.cc` calls
`InteractionInit` after `PanelCreate`; `sdl_backend.cc` maps a dev key set (arrows, keys) to
NAV/encoder `InputEvent`s so the layer is drivable without hardware.

- **Edge cases**: the X-Touch's absolute CC encoders need delta-tracking to produce `detents`
  (a relative encoder would emit signed CC deltas directly); the first pass adapts the existing
  absolute path. `engine::MidiCc`/`engine::kXtouchCompact` lose their only host caller when this
  lands — they stay (the target's Zephyr stack still uses `engine/midi.h` per its header), but
  the CC→param layout stops driving the panel; note-on/off still routes to `PanelNoteOn/Off`.
- **Dependencies**: consumes `Surface()` (2.3), `InteractionOnInput` (5.2).
- **Done**: task 6.3 — simulator smoke test.

### 5.8 Engine route read accessor (tasks 3.1–3.2)

`ParamBlock::GetRoute` reads `pending_[part].routes[slot]` (the same authoritative state
`SetRoute` writes); `EngineGetRoute` is its public wrapper. Returns `false` when `part`/`slot`
is out of range or `source == kNone`; copies the route into `*out` otherwise.

- **Edge cases**: `false` on an empty slot is what lets `InteractionCreateRoute` (5.5) tell
  "free slot" from "matching route" without a second representation of the table.
- **Dependencies**: none; produces the read path 5.5 consumes.
- **Done**: task 3.4 — `test_mod_route` route-read round-trip.

### 5.9 `InteractionOnInput` (task 5.2)

The per-event entry point (§8 contract): map the physical event to a `Gesture` via the
`GestureRecognizer`, resolve the control against `NavState` via `ResolveBinding`, apply via the
`Dispatcher`, then call `MarkDirty` exactly once per slot whose content changed. Precondition
`ev.control < Control::kCount`, monotonic `t_ms`; postcondition zero-or-one gesture applied and
no drawing primitive called.

- **Edge cases**: the detent-absorption rule (§8) — a detent during a press is `kHoldTurn`, and
  the release emits nothing — is enforced by the recognizer *before* resolution, so a press can
  never double-fire as a turn.
- **Dependencies**: consumes the recognizer (4.3), `ResolveBinding` (4.1), the `Dispatcher`
  (5.1).
- **Done**: task 5.6 — `test_interaction.cc` end-to-end.

### 5.10 Surface table (tasks 2.3)

`Surface()` returns the active profile; `SetSurface()` swaps it. The first profile,
`"xtouch-compact"`, is a `ControlMap[]` from the X-Touch's CC numbers to `Control`s (nav,
encoders, part buttons, MOD/PERF/GROUP/OUT), with `n_encoders` reporting the physically present
encoder count. Surplus encoders beyond `geom::kColumns` are unmapped; the shortfall below it is
reported once at startup.

- **Edge cases**: a profile may be a superset or a subset of `geom::kColumns`; injectivity is
  asserted by `test_surface.cc` (4.5), not left to the table author.
- **Dependencies**: consumes `ControlMap`/`SurfaceProfile` (1.5).
- **Done**: task 4.6 — `test_surface.cc` injectivity passes.

## 6. Design Decisions

**`EngineGetRoute` is added despite §4's "no new entry points".** `InteractionCreateRoute`
must find a matching route or the lowest free slot, and the engine exposes only `EngineSetRoute`
(write). A shadow route copy in the layer would duplicate the engine's authoritative state and
violate §5's "no second description of a fact". The read accessor is the smaller deviation and
keeps the engine the single source of truth. §4 is amended accordingly (task 7.1).

**`curve`/`enable` are `kPending`, and `RouteField` holds three members.** The MOD page (§7.6)
declares five route fields, but `ModRoute` has three (`source`, `dst`, `amount`). Adding the
fields is modulation-semantics work owned by `synth-routing` (§2 non-goals). They render dim and
inert until the engine grows, exactly like the discrete `kPending` parameters; the page declares
them so the taxonomy stays honest.

**`SubjectId` uses the §7.6 pane order, not the §7.3 enum order.** The two disagree (`kFilt`/
`kAmp`/`kMod` sit at indices 1–3 in §7.3 but at 6/7/14 in the §7.6 pane). Because `g_pages` is
indexed by `SubjectId` and NAV1 walks subjects one detent each in reading order, the enum order
*is* the pane order. §7.6 wins; §7.3's comment is corrected in task 7.1.

**`GroupCount`/`Column` are templates over $E$.** The page-table sweep (§10) must validate at
$E \in \{4,5,6,7\}$ without editing `geom::kColumns`. Templating the two pure helpers lets
`test_bindings.cc` instantiate each $E$ and assert coherence — converting "does $E=5$ work"
into a build. Runtime call sites use the default `E = geom::kColumns`.

**The modulation-amount acceleration cap is a `Dispatcher` constant, not a `ParamDesc` field.**
§7.8 lists "3 = capped 3x (modulation amounts)" as an `accel_max` value, but a route amount is
a `RouteField`, not a `ParamId`, so no `ParamDesc` row can carry it. The cap (3) + `zero_notch`
are applied at the `kRouteAmount` binding in the `Dispatcher`; `ParamDesc::accel_max` covers
parameters only. §7.8's comment is reconciled in task 7.1.

**`armed_source` is set by NAV2 over the MOD-held source list.** The study (Option 2, "MOD
held") specifies: MOD held turns the pane into the source list, NAV2 selects the source, and
encoder $n$ writes `armed_source → cols[n]`. The field persists across `kModArm` entries so a
re-arm keeps the last source.

## 7. Success Criteria

Each traces to a Verify task.

- [ ] `ResolveBinding` is total: every `(NavState, Control)` resolves without reading globals
  beyond `g_pages` (task 4.6).
- [ ] A `kPending` column resolves `kPending`, never `kParam` (task 4.6).
- [ ] Every `PageDesc` satisfies `n_cols ≤ geom::kColumns` per group, and `label` fits the pane
  (task 4.6).
- [ ] The page table is coherent at $E \in \{4,5,6,7\}$ (task 4.6).
- [ ] A turn on a `kParam` column calls `EngineSetParam` for the current part and no other
  parameter (task 5.6).
- [ ] A press past `long_press_ms` with no detent reverts to `g_params[id].def` with no
  intermediate write (task 5.6).
- [ ] MOD-held + armed source + encoder turn creates/updates a route; MOD release returns the
  prior mode (task 5.6).
- [ ] Full route table with no match returns `false`; a zero amount keeps the slot (task 5.6).
- [ ] A part change leaves `subject`/`group`/`item`/`mode` unchanged, marking value slots only
  (task 5.6).
- [ ] Each `ControlMap` is injective in both directions (task 4.6).
- [ ] No `nostromo/interaction.*` symbol references a drawing primitive, `DynSlot::dirty`,
  `Panel::pending`, or `spike::Damage` — a static check (interaction.cc includes neither
  `fb.h` nor `damage.h`, and links no drawing symbol), verified by code review at task 5.6,
  not a runtime test.

## 8. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/nostromo-interaction_arch-design.md` | Yes — §4's "no new engine entry points" (now `EngineGetRoute`); §7.4's `RouteField` (curve/enable now pending); §7.3 `SubjectId` order vs §7.6 pane order; §7.8's modulation-amount accel note. | Task 7.1 |
| `../arch-designs/engine-parameter-surface_arch-design.md` | Yes — §6 `ParamDesc` lacks `accel_max`/`zero_notch`. | Task 7.2 |
| `../arch-designs/synth-routing_arch-design.md` | No — `ModRoute` (source/dst/amount) is unchanged; the layer reads it, doesn't alter it. | None. |
| `../arch-designs/spike_arch-design.md` | No — `MarkDirty`/`DynSlot`/damage ownership is unchanged; the layer is a caller. | None. |
| `../design-studies/2026-09-12_panel-interaction-model_design-study.md` | No — design-study records reasoning; superseded conclusions are already reflected in the arch-design. | None. |

## 9. Cleanup

No diagnostic instrumentation is added. The §10 interaction-cost logger ($S(\tau)$) is a
measurement feature, not debug instrumentation; if a minimal version lands with the host wiring,
it ships deliberately (not as a cleanup item). The §13 tunables (acceleration, hues, $E$) are
runtime `FeelProfile`/`geom` values measured during execution, not temporary scaffolding.
