---
title: Nostromo Interaction Layer -- Implementation Plan
status: approved
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
`PageTable`/`BindingResolver`, `Dispatcher` — their unit tests, the host wiring that puts them
in front of the simulator's input, and a panel-geometry migration (phase 6) so `NavState` is
actually visible. The §13 tunables (acceleration coefficient, part hues, $E = 5$ vs 6) are
measured *during* this work, not resolved beforehand; the parameter-surface growth (§13.3) is
additive and non-blocking. `geom.h` and `spike/descriptor.h` are consumed; `screens.*`/`panel.*`
are migrated in phase 6, not rewritten as part of the layer itself.

## 1. Phases

1. **Vocabulary** — type definitions and function declarations across the four new `nostromo/`
   headers, plus the `ParamDesc` acceleration fields. No behavior (no deps).
2. **Static data** — `g_pages`, `g_feel`, and the first `SurfaceProfile` table. Depends on phase 1.
3. **Engine route read accessor** — `EngineGetRoute` + `ParamBlock::GetRoute` (the one
   deviation from §4; see Design Decisions). Depends on nothing; phase 5 consumes it — off the
   critical path (1→2→4→5), so it runs in parallel with 1–2.
4. **Pure logic + tests** — `ResolveBinding`, `GestureRecognizer`, and their exhaustive tests.
   Depends on phases 1–2.
5. **Dispatch + lifecycle** — `Dispatcher`, `InteractionOnInput`, `InteractionCreateRoute`,
   `InteractionInit`, and the end-to-end test. Depends on phases 3–4.
6. **Panel geometry migration + edit screen** — move the plot slots from the old four-module
   grid to the new pane+columns band, resize the trace/scratch buffers for the 900-wide plot,
   add the pane + column headers/values so `NavState` is visible, and update `test_panel`'s
   golden hash in the same commit. `screens.cc` still renders the four superseded screens and
   nothing references `kPaneX`/`kPaneW`, so there is no pane to walk. Depends on phase 5.
7. **Host integration** — verify the X-Touch's relative-encoder mode, then wire the simulator's
   input through the layer. Depends on phase 6.
8. **Documentation** — record the deviations and the `ParamDesc` growth in the two arch-designs.
   Depends on all implementation phases.

## 2. Implementation Status

| # | Task | Status |
|---|---|---|
| 1.1 | Add `accel_max` (`std::uint8_t`) + `zero_notch` (`bool`) to `ParamDesc` in `engine/params.h`; populate `g_params` in `engine/params.cc` (`accel_max` = 4 for ordinary params, `zero_notch` = true for bipolar `kPitchCoarse`/`kPitchBend`) | Pending |
| 1.2 | Define `Control`, `Edge`, `InputEvent`, `Gesture`, `SubjectId`, `ViewMode`, `NavPos`, `NavState` in `nostromo/interaction.h`; declare `InteractionInit`, `InteractionOnInput`, `InteractionCreateRoute` | Pending |
| 1.3 | Define `ColumnKind`, `RouteField` (kSource/kDest/kAmount), `ViewCtl`, `ColumnSpec`, `ItemAxis`, `PageDesc`, `BindKind`, `Binding`, `GroupCount<E>`, `Column<E>` in `nostromo/pages.h`; declare `ResolveBinding` and `extern g_pages` | Pending |
| 1.4 | Define `FeelProfile` + `extern g_feel` in `nostromo/feel.h` | Pending |
| 1.5 | Define `ControlMap`, `SurfaceProfile` (with `EncEncoding enc`), `EncEncoding` (kSignedBit / kTwosComplement / kBinaryOffset — quadrature is a separate stateful reader, not an enum member), pure `DecodeEnc`, `Surface()`, `SetSurface()` in `nostromo/surface.h` | Pending |
| 1.6 | Verify: `cmake --build /tmp/twang-build` compiles with the new headers | Pending |
| 2.1 | Author `g_pages` (20 pages per arch-design §7.6) in `nostromo/pages.cc`; `curve`/`enable` MOD columns are `kPending`; register `pages.cc` in `CMakeLists.txt` | Pending |
| 2.2 | Define `g_feel` defaults (detents_per_rev 24, accel_max_default 4, accel_threshold_dps 8, long_press_ms 500, fine_divisor 10) in `nostromo/feel.cc`; register in `CMakeLists.txt` | Pending |
| 2.3 | Implement `Surface()`/`SetSurface()`, `DecodeEnc`, and the `xtouch-compact` `SurfaceProfile` table (its `enc` set to the X-Touch's relative encoding, confirmed in task 7.1) in `nostromo/surface.cc`; register in `CMakeLists.txt` | Pending |
| 2.4 | Verify: `cmake --build /tmp/twang-build`; `static_assert(SubjectId::kCount == geom::kSubjectCount)` holds | Pending |
| 3.1 | Add `bool ParamBlock::GetRoute(int part, int slot, ModRoute *out) const` in `engine/ipc.h`/`engine/ipc.cc` | Pending |
| 3.2 | Add `bool EngineGetRoute(int part, int slot, ModRoute *out)` in `engine/engine.h`/`engine/engine.cc` | Pending |
| 3.3 | Write test: extend `tests/test_mod_route.cc` with a route-read round-trip (`SetRoute` then `GetRoute` returns it; empty slot returns `false`) | Pending |
| 3.4 | Verify: `cmake --build /tmp/twang-build --target engine test_mod_route && /tmp/twang-build/test_mod_route` | Pending |
| 4.1 | Implement `ResolveBinding` (pure) in `nostromo/pages.cc` per §7.7/§8 resolution tables | Pending |
| 4.2 | Write test: `tests/test_bindings.cc` — exhaustive `(nav, control)` resolution, invariants 2/3, `kPending` never leaks as `kParam`, and the $E \in \{4,5,6,7\}$ page-table sweep; register in `CMakeLists.txt` | Pending |
| 4.3 | Implement `GestureRecognizer` (stateful per control) in `nostromo/interaction.cc`; register `interaction.cc` in `CMakeLists.txt` | Pending |
| 4.4 | Write test: `tests/test_gestures.cc` — press/turn disambiguation boundaries; register in `CMakeLists.txt` | Pending |
| 4.5 | Write test: `tests/test_surface.cc` — `ControlMap` injectivity (both directions) + `DecodeEnc` round-trip per scheme (signed-bit / two's-complement / binary-offset, known tick values); register in `CMakeLists.txt` | Pending |
| 4.6 | Verify: `cmake --build /tmp/twang-build --target test_bindings test_gestures test_surface && /tmp/twang-build/test_bindings && /tmp/twang-build/test_gestures && /tmp/twang-build/test_surface` | Pending |
| 5.1 | Implement `Dispatcher` (apply `Gesture`→`Binding`; engine writes + `MarkDirty`; acceleration) in `nostromo/interaction.cc` | Pending |
| 5.2 | Implement `InteractionOnInput` (per-event flow; §8 contract) in `nostromo/interaction.cc` | Pending |
| 5.3 | Implement `InteractionCreateRoute` (find-or-allocate via `EngineGetRoute`; §8) in `nostromo/interaction.cc` | Pending |
| 5.4 | Implement `InteractionInit` (bind slots + surface; zero `NavState`; §6) in `nostromo/interaction.cc` | Pending |
| 5.5 | Write test: `tests/test_interaction.cc` — turn→`EngineSetParam`, MOD-arm→route creation, part change leaves `subject`/`group`/`item`/`mode` untouched, `MarkDirty` observed via `PanelPlotDraws`; register in `CMakeLists.txt` | Pending |
| 5.6 | Verify: `cmake --build /tmp/twang-build --target test_interaction && /tmp/twang-build/test_interaction` | Pending |
| 6.1 | Migrate the four plot `DynSlot` rects from the old four-module grid (`kPx0 = {16,266,516,766}`, 230×232 at y142) to the new plot band (900×404 at x108/y180); retarget `ReadoutRect` and the damage extents in `nostromo/panel.cc` | Pending |
| 6.2 | Resize the trace/scratch buffers in `nostromo/panel.h`/`nostromo/panel.cc`: `TraceState::y0/y1` `uint8_t[230]` → `uint16_t[kPlotW]` (element widens because `kPlotH` = 404 > 255), `col_lo`/`col_hi` `int[256]` → `int[kPlotW]`, scope `buf[256]` → `[kPlotW]`; re-pin the three `static_assert`s to the derived `geom::kPlotW`/`kPlotH` | Pending |
| 6.3 | Add the pane + column chrome in `nostromo/screens.cc`/`nostromo/panel.cc`: subject pane (20 subjects, NAV1 inverse-video cursor) + column headers/values (from `g_pages` + `EngineGetParam`), reading `NavState` via the `InteractionNavState()` accessor | Pending |
| 6.4 | Update `test_panel`'s golden hash (currently `0x815AD46F`) in the same commit as 6.1–6.3, with the diff stating what moved | Pending |
| 6.5 | Verify: `cmake --build /tmp/twang-build`; `ctest -R panel` passes with the new hash; `panel_shot`/`mockup_pages` still render | Pending |
| 7.1 | Confirm the X-Touch Compact's encoder mode before wiring: set relative mode, hardware acceleration off, LED rings off; record which relative encoding it emits (0x41 = binary offset, 0x01 = signed-bit/two's-complement) and set `Surface().enc` accordingly | Pending |
| 7.2 | Replace the `MidiCc`→param path in `host/midi_io.cc` with `DecodeEnc` (via `Surface().enc`) + `SurfaceProfile` mapping → `InteractionOnInput` (CC→`Control`, decode→detents, buttons→edges) | Pending |
| 7.3 | Call `InteractionInit` at startup in `host/main.cc`; add keyboard→`InputEvent` (NAV1/NAV2/encoders for dev) in `host/sdl_backend.cc` | Pending |
| 7.4 | Verify: `cmake --build /tmp/twang-build`; simulator smoke test (user-driven: encoders drive parameters, MOD arms a route, NAV1 walks the pane) | Pending |
| 8.1 | Update `nostromo-interaction_arch-design.md`: record the `EngineGetRoute` deviation (§4), the `curve`/`enable`-as-pending decision (§7.4), the `SubjectId` enum/pane order reconciliation (§7.3 vs §7.6), `Binding` union `ParamId`→`ParamRef` (§7.7), invariant 2 (n_cols is the page total — totality over `(group, col)`), and the `EncEncoding` surface field | Pending |
| 8.2 | Update `engine-parameter-surface_arch-design.md` §6: add `accel_max`/`zero_notch` to the `ParamDesc` struct | Pending |
| 8.3 | Verify: full `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` green (18 existing + 4 new tests = 22) | Pending |

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
| `nostromo/surface.h`, `nostromo/surface.cc` | New: `ControlMap`, `EncEncoding`, `DecodeEnc`, `SurfaceProfile`, `Surface()`/`SetSurface()`, `xtouch-compact` table |
| `nostromo/feel.h`, `nostromo/feel.cc` | New: `FeelProfile`, `g_feel` |
| `tests/test_bindings.cc`, `tests/test_gestures.cc`, `tests/test_surface.cc`, `tests/test_interaction.cc` | New: unit + integration tests |
| `tests/test_mod_route.cc` | Add route-read round-trip checks |
| `host/midi_io.cc`, `host/midi_io.h` | Replace `MidiCc`→param path with `DecodeEnc` + `SurfaceProfile` → `InteractionOnInput` |
| `host/main.cc`, `host/sdl_backend.cc` | `InteractionInit` at startup; keyboard→`InputEvent` |
| `CMakeLists.txt` | Add the 4 new `nostromo/` sources; register 4 new test executables |
| `nostromo/geom.h`, `spike/descriptor.h` | Existing — consumed, no change |
| `nostromo/screens.{h,cc}`, `nostromo/panel.{h,cc}` | Modified — plot migration + buffer resize + pane/column chrome (phase 6) |

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
/// Read-only view of the navigation state for the renderer (pane/header chrome
/// and DYN hooks). Returns a const reference so the screen cannot write back —
/// the layer is the sole writer of `NavState`.
const NavState &InteractionNavState();
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
struct Binding { BindKind kind; union { ParamRef param; RouteField field; ViewCtl ctl; };
                 std::int8_t slot; std::int8_t column; };

extern const PageDesc g_pages[static_cast<int>(SubjectId::kCount)];
Binding ResolveBinding(const NavState &nav, Control c);
```

`RouteField` holds only `kSource`/`kDest`/`kAmount` — the three fields `ModRoute` actually has.
The MOD page's `curve`/`enable` columns are declared `ColumnKind::kPending` (the arch-design's
`kCurve`/`kEnable` are reserved for when `ModRoute` grows; see Design Decisions). `GroupCount`
and `Column` are templates over $E$ so the page-table sweep in `test_bindings.cc` can
instantiate $E \in \{4,5,6,7\}$ without editing `geom::kColumns`.

`ColumnSpec` carries `ParamId` (a column *declares a kind*); `Binding` carries `ParamRef` (a
resolution *names a kind plus an instance*). `ResolveBinding` fills the instance once — from
`SubjectId` (`kOsc3` → instance 2, single-instance → 0) — so the `Dispatcher` applies a complete
address and never re-derives the `subject → instance` mapping. This is an arch-design deviation
(§7.7's `Binding` union still holds `ParamId`), recorded in task 8.1.

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
enum class EncEncoding : std::uint8_t { kSignedBit, kTwosComplement, kBinaryOffset };
/// Pure decode of one relative-encoder byte to −1 / 0 / +1 (tick direction,
/// magnitude clamped). Quadrature is NOT an EncEncoding member: it is a
/// stateful 2-bit Gray-code transition, handled by a separate reader on the
/// GPIO panel (out of scope here).
int DecodeEnc(std::uint8_t raw, EncEncoding enc);

struct ControlMap { std::uint16_t physical; Control logical; };
struct SurfaceProfile { const char *name; const ControlMap *map; std::uint8_t n_map;
                        std::uint8_t n_encoders; std::uint8_t n_buttons; bool has_rings;
                        EncEncoding enc; };
const SurfaceProfile &Surface();
void SetSurface(const SurfaceProfile &p);
```

`EncEncoding` holds exactly the three stateless MIDI relative encodings, decoded by the pure
`DecodeEnc`: `kSignedBit` (bit 6 = sign, bits 0–5 = magnitude: +1 = 0x01, −1 = 0x41),
`kTwosComplement` (7-bit two's-complement: +1 = 0x01, −1 = 0x7F), `kBinaryOffset` (center 0x40:
+1 = 0x41, −1 = 0x3F). Quadrature is deliberately absent — the next panel's GPIO reads A/B
directly and needs previous-state memory, so it gets its own reader rather than a broken
"pure" decode.

`DecodeEnc` clamps magnitude to 1 and returns direction only: a wraparound or aggregate byte
(binary-offset `raw = 0` → −64, signed-bit `raw = 0x7F` → magnitude 63) decodes to −1, never a
64-detent delta, so one event cannot jump a parameter end-to-end past the `Dispatcher`'s clamp.

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
  `kViewCtl`→`kViewCtl`, `kNone`→`kNone`). For `kParam`, fill `Binding::param` as the *full*
  `ParamRef {instance(subject), ColumnSpec.param}` — `kOsc3` → instance 2, single-instance → 0.
  For `kRouteField`, populate `Binding::slot` from `nav.item[subject]` (the NAV2 route/slot
  cursor).
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

- **`kParam` turn** — read `EngineGetParam(part, binding.param)`, add
  `detents × step(desc, feel)` (step applies the `accel_max` cap above
  `accel_threshold_dps`, `fine_divisor` on `kHoldTurn`, `zero_notch` crossing), clamp,
  `EngineSetParam`; `MarkDirty` the page's `dyn_slot`. The `ParamRef` (instance included) is
  already resolved by `ResolveBinding`; the `Dispatcher` never re-derives `subject → instance`.
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

### 5.7 Host wiring (tasks 7.1–7.3)

Task 7.1 is the hardware gate *before* any wiring: put the X-Touch Compact in relative mode
(not absolute), hardware acceleration off, LED rings off, and record which relative encoding it
emits — `0x41` for clockwise is binary offset, `0x01` is signed-bit or two's-complement — then
set `Surface().enc` to match. Absolute mode is rejected outright: an absolute encoder stops
emitting at 0 and 127, which contradicts the endless-encoder assumption the whole design rests on.

`midi_io.cc` then stops calling `engine::MidiCc(layout, 0, cc, val)` for panel controls; instead
it maps the incoming CC to a `Control` via `Surface().map`, decodes each encoder byte through
`DecodeEnc(raw, Surface().enc)` to signed detents (buttons → edges), and calls
`InteractionOnInput`. Note-on/off still routes to `PanelNoteOn`/`PanelNoteOff` (touch/notes are
out of scope, §2). `main.cc` calls `InteractionInit` after `PanelCreate`; `sdl_backend.cc` maps a
dev key set to NAV/encoder `InputEvent`s so the layer is drivable without hardware.

- **Edge cases**: all acceleration stays in `FeelProfile` — the controller's own acceleration is
  off, so `detents_per_rev` keeps settings portable across prototypes.
  `engine::MidiCc`/`engine::kXtouchCompact` lose their only host caller — they stay (the
  target's Zephyr stack still uses `engine/midi.h` per its header).
- **Dependencies**: consumes `Surface()`/`DecodeEnc` (2.3), `InteractionOnInput` (5.2).
- **Done**: task 7.4 — simulator smoke test.

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
encoder count and `enc` the relative encoding. Surplus encoders beyond `geom::kColumns` are
unmapped; the shortfall below it is reported once at startup.

`DecodeEnc(raw, enc)` is a pure, stateless per-byte decode returning −1 / 0 / +1: `kSignedBit`
(bit 6 = sign, bits 0–5 = magnitude, clamped to 1), `kTwosComplement` (7-bit sign-extended,
clamped), `kBinaryOffset` (center 0x40, sign of the offset, clamped). Magnitude is clamped so a
wraparound byte (`raw = 0` → −64) reads as one detent. Quadrature is not here — it is a stateful
2-bit transition, deferred to the GPIO panel.

- **Edge cases**: a profile may be a superset or a subset of `geom::kColumns`; injectivity and
  decode round-trips are asserted by `test_surface.cc` (4.5), not left to the table author. A
  mis-decode is expensive to find by feel (binary-offset read as signed-bit turns clockwise into
  −1 and anticlockwise into +63), so the round-trip is a test, not a manual check.
- **Dependencies**: consumes `ControlMap`/`SurfaceProfile`/`EncEncoding` (1.5).
- **Done**: task 4.6 — `test_surface.cc` injectivity + decode pass.

### 5.11 Panel geometry migration + edit screen (tasks 6.1–6.5)

The panel still renders the superseded signal-flow screen at the old four-module geometry —
`kPx0 = {16, 266, 516, 766}`, `kModW = 242`, plots 230×232 at y142 — while `geom.h` defines the
new pane (x16–108) and plot band (900×404 at x108/y180) that nothing consumes. Module 0 sits
under the pane, so this phase is a *migration*, not chrome porting:

1. **Move the plots** (task 6.1) — re-point the four plot `DynSlot` rects at the new band, and
   retarget `ReadoutRect` (which indexes `kPx0[idx]`) and the damage extents so the painted and
   invalidated regions agree.
2. **Resize the trace buffers** (task 6.2) — `TraceState::y0/y1` go `uint8_t[230]` →
   `uint16_t[kPlotW]` (element widens because `kPlotH` = 404 > 255), `col_lo`/`col_hi` go
   `int[256]` → `int[kPlotW]`, scope `buf[256]` → `[kPlotW]`. Re-pin the three `static_assert`s
   to `geom::kPlotW`/`kPlotH`. These asserts exist because getting the sizes wrong corrupts
   memory silently.
3. **Add the pane + columns** (task 6.3) — subject pane (20 subjects, NAV1 inverse-video cursor)
   + column headers/values from `g_pages` + `EngineGetParam`, reading `NavState` via the
   `InteractionNavState()` accessor.
4. **Update the hash** (task 6.4) — `test_panel`'s golden hash (`0x815AD46F`) changes in the same
   commit, with the diff stating what moved.

- **Edge cases**: the buffer resize and the plot move must land together — a 900-wide plot drawn
  through a `uint8_t[230]` trace overruns. The old four-module chrome (`DrawChrome`, module
  labels) is superseded by the pane; it is removed in the same commit rather than left to
  overlap.
- **Dependencies**: consumes `NavState` (1.2), `g_pages` (2.1), `EngineGetParam`, the existing
  DYN plot hooks.
- **Done**: task 6.5 — `ctest -R panel` with the new hash.

## 6. Design Decisions

**`EngineGetRoute` is added despite §4's "no new entry points".** `InteractionCreateRoute`
must find a matching route or the lowest free slot, and the engine exposes only `EngineSetRoute`
(write). A shadow route copy in the layer would duplicate the engine's authoritative state and
violate §5's "no second description of a fact". The read accessor is the smaller deviation and
keeps the engine the single source of truth. §4 is amended accordingly (task 8.1).

**`curve`/`enable` are `kPending`, and `RouteField` holds three members.** The MOD page (§7.6)
declares five route fields, but `ModRoute` has three (`source`, `dst`, `amount`). Adding the
fields is modulation-semantics work owned by `synth-routing` (§2 non-goals). They render dim and
inert until the engine grows, exactly like the discrete `kPending` parameters; the page declares
them so the taxonomy stays honest.

**`SubjectId` uses the §7.6 pane order, not the §7.3 enum order.** The two disagree (`kFilt`/
`kAmp`/`kMod` sit at indices 1–3 in §7.3 but at 6/7/14 in the §7.6 pane). Because `g_pages` is
indexed by `SubjectId` and NAV1 walks subjects one detent each in reading order, the enum order
*is* the pane order. §7.6 wins; §7.3's comment is corrected in task 8.1.

**`GroupCount`/`Column` are templates over $E$.** The page-table sweep (§10) must validate at
$E \in \{4,5,6,7\}$ without editing `geom::kColumns`. Templating the two pure helpers lets
`test_bindings.cc` instantiate each $E$ and assert coherence — converting "does $E=5$ work"
into a build. Runtime call sites use the default `E = geom::kColumns`.

**The modulation-amount acceleration cap is a `Dispatcher` constant, not a `ParamDesc` field.**
§7.8 lists "3 = capped 3x (modulation amounts)" as an `accel_max` value, but a route amount is
a `RouteField`, not a `ParamId`, so no `ParamDesc` row can carry it. The cap (3) + `zero_notch`
are applied at the `kRouteAmount` binding in the `Dispatcher`; `ParamDesc::accel_max` covers
parameters only. §7.8's comment is reconciled in task 8.1.

**`armed_source` is set by NAV2 over the MOD-held source list.** The study (Option 2, "MOD
held") specifies: MOD held turns the pane into the source list, NAV2 selects the source, and
encoder $n$ writes `armed_source → cols[n]`. The field persists across `kModArm` entries so a
re-arm keeps the last source.

**`Binding` carries `ParamRef`; `ColumnSpec` carries `ParamId`.** A column *declares a kind*; a
binding *resolves a kind plus an instance*. The arch-design's §7.7 `Binding` union holds `ParamId`
(§7.5's "instance comes from `SubjectId`" was never threaded into the binding type after the
engine moved to `ParamRef` in `f8b5286`). Making `ResolveBinding` fill the full `ParamRef` once
is what makes it genuinely total — it returns the complete address, and the `Dispatcher` stops
re-deriving `subject → instance`. Recorded as a §7.7 deviation in task 8.1.

**`EncEncoding` holds three stateless byte decodes; quadrature is a separate stateful reader.**
`kSignedBit`/`kTwosComplement`/`kBinaryOffset` are each a pure per-byte decode; a 2-bit Gray-code
transition needs previous-state memory, so folding it into the same `DecodeEnc(raw, enc)` invites
a broken "pure" quadrature decode. Quadrature is the next panel's GPIO concern and stays out of
this plan. All acceleration stays in `FeelProfile` — the controller's own acceleration is off, so
`detents_per_rev` remains the portability mechanism it was designed to be.

**The edit screen is a plot-geometry migration, not chrome porting.** The four plots live at the
old module grid (`kPx0 = {16,266,516,766}`, 230×232) and module 0 sits under the new pane; moving
them re-points `DynSlot::rect`, `ReadoutRect`, and the damage extents. The trace buffers are
sized to the old plot (`uint8_t[230]`, `int[256]`, `buf[256]`) and pinned by `static_assert`s
that exist because an overrun corrupts memory silently — so the 900-wide plot forces a
`uint16_t[kPlotW]` trace and `[kPlotW]` scratch, in the same commit as the `test_panel` hash
change.

## 7. Success Criteria

Each traces to a Verify task.

- [ ] `ResolveBinding` is total: every `(NavState, Control)` resolves without reading globals
  beyond `g_pages` (task 4.6).
- [ ] A `kPending` column resolves `kPending`, never `kParam` (task 4.6).
- [ ] Every column index resolves: for all `group < GroupCount<E>(p)` and `col < E`,
  `Column<E>(p, group, col)` returns a valid `ColumnSpec` (`kNone` only past `n_cols`), and
  `label` fits the pane (task 4.6).
- [ ] The page table is coherent at $E \in \{4,5,6,7\}$ (task 4.6).
- [ ] A turn on a `kParam` column calls `EngineSetParam` for the current part and no other
  parameter (task 5.6).
- [ ] A `kParam` binding carries the full `ParamRef`: `kOsc3` + a kind column resolves
  `{instance = 2, id}`, single-instance resolves `{instance = 0, id}` (task 4.6).
- [ ] A press past `long_press_ms` with no detent reverts to `g_params[id].def` with no
  intermediate write (task 5.6).
- [ ] MOD-held + armed source + encoder turn creates/updates a route; MOD release returns the
  prior mode (task 5.6).
- [ ] Full route table with no match returns `false`; a zero amount keeps the slot (task 5.6).
- [ ] A part change leaves `subject`/`group`/`item`/`mode` unchanged, marking value slots only
  (task 5.6).
- [ ] Each `ControlMap` is injective in both directions (task 4.6).
- [ ] `DecodeEnc` round-trips each scheme against known tick values (binary-offset 0x41→+1,
  0x3F→−1; signed-bit 0x01→+1, 0x41→−1; two's-complement 0x01→+1, 0x7F→−1) and clamps
  wraparound bytes to ±1 (binary-offset 0x00→−1, signed-bit 0x7F→−1) (task 4.6).
- [ ] No `nostromo/interaction.*` symbol references a drawing primitive, `DynSlot::dirty`,
  `Panel::pending`, or `spike::Damage` — a static check (interaction.cc includes neither
  `fb.h` nor `damage.h`, and links no drawing symbol), verified by code review at task 5.6,
  not a runtime test.

## 8. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/nostromo-interaction_arch-design.md` | Yes — §4 "no new engine entry points" (now `EngineGetRoute`); §7.4 `RouteField` (curve/enable pending); §7.3 `SubjectId` order vs §7.6 pane order; §7.7 `Binding` union `ParamId`→`ParamRef`; §9 invariant 2 (n_cols is the page total — totality over `(group, col)`); §7.8 modulation-amount accel note; §7.9 `EncEncoding`/`DecodeEnc` surface field. | Task 8.1 |
| `../arch-designs/engine-parameter-surface_arch-design.md` | Yes — §6 `ParamDesc` lacks `accel_max`/`zero_notch`. | Task 8.2 |
| `../arch-designs/synth-routing_arch-design.md` | No — `ModRoute` (source/dst/amount) is unchanged; the layer reads it, doesn't alter it. `EngineGetRoute` is a read mirror of `EngineSetRoute`, not a semantic change. | None. |
| `../arch-designs/spike_arch-design.md` | No — `MarkDirty`/`DynSlot`/damage ownership is unchanged; the layer is a caller. | None. |
| `../design-studies/2026-09-12_panel-interaction-model_design-study.md` | No — design-study records reasoning; superseded conclusions are already reflected in the arch-design. | None. |
| `../../random/panel-ui-design-state.md` | Yes — its four-module panel-layout description (the layout phase 6 migrates away from) is superseded; the `DrawChrome`/`grat[]` failure-mode reference in the arch-design §5 survives as history. | Fold a note into task 8.1. |

## 9. Cleanup

No diagnostic instrumentation is added. The §10 interaction-cost logger ($S(\tau)$) is a
measurement feature, not debug instrumentation; if a minimal version lands with the host wiring,
it ships deliberately (not as a cleanup item). The §13 tunables (acceleration, hues, $E$) are
runtime `FeelProfile`/`geom` values measured during execution, not temporary scaffolding.
