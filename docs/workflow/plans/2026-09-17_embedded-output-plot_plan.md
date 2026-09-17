---
title: Embedded Output Plot -- Implementation Plan
status: issued
date: 2026-09-17
author: Dizan Vasquez
arch-design: ../arch-designs/nostromo-interaction_arch-design.md
brief: ../briefs/2026-09-16_embedded-output-plot_brief.md
---

# Embedded Output Plot -- Implementation Plan

## 1. Implementation Status

The re-design retires the three OUT subjects and replaces them with an embedded
output plot plus a latched full-screen `kOutView` mode. The scope is the
interaction layer and the panel; it does not touch the engine or the audio tap
point (see [the brief](../briefs/2026-09-16_embedded-output-plot_brief.md) §2).
The arch-design is the contract source; this plan sequences it and never
duplicates its types, invariants, or acceptance criteria.

Two review findings reshape the sequencing and are folded in as first-class
tasks: the embedded half **crops the scope signal** (a fixed `kStride` derived
from `kPlotW`), and the embedded output would make **every plot page repaint at
trace rate**. The plan fixes the stride, adds a runtime-tunable refresh ceiling,
and renders the embedded output through its **own slot** so the page's plot and
the output invalidate independently.

**Phases:**

1. **Foundations** — fix the scope stride and add the output-refresh rate-limit.
   Independent of the re-design; lands on today's full-band scope. (depends on nothing)
2. **Retire the OUT subjects** — collapse the three subjects into a global
   `scope_mode`, output still full-band. The buildable bisect point. (depends on phase 1)
3. **Embedded split + `kOutView`** — two active slots, the per-mode `kOutColumns`
   tables, full-screen `kOutView`, and the `kOff`-entry fix. (depends on phase 2)
4. **Documentation** — record the frame-budget contingency, archive the superseded
   plan. (depends on phase 3)

| # | Task | Status |
|---|---|---|
| 1.1 | `nostromo/panel.cc` — `DrawScopePlot`: derive the stride from the drawn width (`const int kStride = ScopeRing::kCapacity / w`), replacing `kCapacity / geom::kPlotW`. `DrawCyclePlot`/`DrawSpectrumPlot` are already width-correct (verified: cycle reads a full period at stride 1 and resamples by `period/w`; spectrum is FFT-width-independent) — no change | Pending |
| 1.2 | `nostromo/feel.h` + `feel.cc` — add `std::uint32_t scope_interval_ms` to `FeelProfile` (doc-comment flags it as the one display-timing field, see Design Decision A); `DefaultFeel()` returns 33 | Pending |
| 1.3 | `nostromo/pages.h` + `pages.cc` + `interaction.cc` — add `ViewCtl::kScopeRefresh`; append a REFRESH column to `kColsConf`; handle `kScopeRefresh` in `TurnFeel`/`RevertFeel` | Pending |
| 1.4 | `nostromo/panel.cc` — add `Panel` members `std::uint32_t last_scope_ms` and `bool scope_pending`; replace the `scope_dirty` drain in `PanelDraw` with the interval-checked latch (§4.2) | Pending |
| 1.5 | `tests/test_panel_pages.cc` — re-bake the CONF hash (the new REFRESH column) | Pending |
| 1.6 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` — 20 green | Pending |
| 2.1 | `nostromo/geom.h` — `kSubjectCount` 20→17; `kPaneRowsN` 11→10, `kPaneStripsN` 4→3 (drop OUT from both pane comments); add `kEmbedGap`/`kEmbedW`/`kEmbedX(int)`; replace the "OUT view strip" static_assert with `kEmbedW*2 + kEmbedGap == kPlotW` | Pending |
| 2.2 | `nostromo/interaction.h` — `SubjectId`: delete `kOutScope`/`kOutCycle`/`kOutSpec` (→17); add `enum class ScopeMode {kOff,kScope,kCycle,kSpectrum}`; `ViewMode`: add `kOutView`; delete `NavPos`; `NavState`: delete `prev`, add `ScopeMode scope_mode`; update `Control::kOut` comment | Pending |
| 2.3 | `nostromo/panel.h` — delete `enum class ScopeMode` (superseded by `interaction.h`'s `ScopeMode`) | Pending |
| 2.4 | `nostromo/pages.cc` — delete the three `k_pages` entries and the old `kPending` `kColsOutScope/Cycle/Spec` tables; `ResolveBinding`: gate `kOut`→`kNone` on `dyn_slot==-1`, gate `kMod`→`kNone` via `HasModulatableColumn`; add `HasModulatableColumn`; `#include "params.h"` | Pending |
| 2.5 | `nostromo/interaction.cc` — delete `IsOut`; add `NextScopeMode`; rewrite the `kOutToggle` dispatcher: tap cycles `scope_mode` (hold is phase 3); `Init`: power-on `subject kFilt` + `scope_mode kScope`, drop `prev`; remove all `nav.prev` uses | Pending |
| 2.6 | `nostromo/panel.cc` — delete `ScopeModeOf` + `DrawViewStrip` (+ its call); `DrawOutPlot` switches on `nav.scope_mode`; `IsGlobalSubject` → `>= kFx`; `ActivePlotSlot`: `scope_mode != kOff` → `kSlotOut`, else the page's `dyn_slot` (output full-band) | Pending |
| 2.7 | `tests/test_bindings.cc` — subject sweep over 17; OUT → `kNone` on a no-plot page; MOD → `kNone` on a non-modulatable page (MOD page) | Pending |
| 2.8 | `tests/test_interaction.cc` — power-on navigation (subject is now `kFilt`); OUT tap cycles `scope_mode` off→scope→cycle→spectrum→off | Pending |
| 2.9 | `tests/test_panel.cc` + `test_panel_pages.cc` — re-bake hashes (intermediate: power-on `kFilt` + `scope_mode=kScope` shows the output full-band) | Pending |
| 2.10 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` — 20 green | Pending |
| 3.1 | `nostromo/pages.h` — `ViewCtl`: add `kTimebase,kCycles,kRange,kScale,kTrigger,kAlign,kAverage,kHold,kWindow` | Pending |
| 3.2 | `nostromo/pages.cc` — rewrite `kColsOutScope/Cycle/Spec` as `kViewCtl` tables (drop SOURCE, four cols each per §7.4); add `kOutColumns[4]`; `ResolveBinding`: resolve `kOutView` encoders to `kOutColumns[scope_mode][n]` (n<4 → `kViewCtl`, n≥4 → `kNone`) | Pending |
| 3.3 | `nostromo/interaction.cc` — `kOutToggle` hold: toggle `kOutView`; on entering `kOutView` from `kOff`, force `scope_mode = kScope` (Design Decision 2) | Pending |
| 3.4 | `nostromo/panel.cc` — two active slots (§4.9): `ActivePlotSlot` → `ActivePlotSlots{page,out}`; set each slot's rect from the mode; draw loop paints both active slots; `scope_dirty` drain marks `kSlotOut`; `DrawColumns` shows `kOutColumns[scope_mode]` in `kOutView`; `kOutView` mode prefix | Pending |
| 3.5 | `tests/test_bindings.cc` — `kOutView` encoder → `kOutColumns[scope_mode][n]`; `Enc(4)` → `kNone` | Pending |
| 3.6 | `tests/test_interaction.cc` — OUT hold toggles `kOutView`; hold from `kOff` forces `kScope` | Pending |
| 3.7 | `tests/test_panel.cc` + `test_panel_pages.cc` — re-bake hashes (final: embedded split on plot pages) | Pending |
| 3.8 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build`; smoke-render the power-on page (embedded scope) and an OUT hold (`kOutView` full-screen) | Pending |
| 4.1 | `../arch-designs/nostromo-interaction_arch-design.md` — add one line (§7.6 keystone paragraph or §5): the embedded default is contingent on an unmeasured frame budget; `kOff` + `kOutView` is the fallback | Pending |
| 4.2 | Archive `docs/workflow/plans/2026-09-14_nostromo-interaction_plan.md` → `docs/archive/workflow/plans/` | Pending |
| 4.3 | Verify: `git status` shows only the intended changes | Pending |

## 2. Architecture

### 2.1. Directory Layout

| File | Change |
|---|---|
| `nostromo/geom.h` | Modify: subject count, pane rows/strips, embed constants, static_asserts |
| `nostromo/interaction.h` | Modify: `SubjectId` (17), `ScopeMode`, `ViewMode`+`kOutView`, remove `NavPos`, `NavState` |
| `nostromo/panel.h` | Modify: remove `enum class ScopeMode` |
| `nostromo/pages.h` | Modify: `ViewCtl` +10 (`kScopeRefresh` + 9 output settings) |
| `nostromo/pages.cc` | Modify: `kColsOut*` tables, `kOutColumns`, `k_pages` −3, `ResolveBinding`, `HasModulatableColumn`, CONF column |
| `nostromo/interaction.cc` | Modify: `kOutToggle` dispatch, `Init`, `TurnFeel`/`RevertFeel`, remove `IsOut`/`NavPos` uses, add `NextScopeMode` |
| `nostromo/feel.h`, `feel.cc` | Modify: `scope_interval_ms` field + default |
| `nostromo/panel.cc` | Modify: `DrawScopePlot` stride, `DrawOutPlot`, drain, `ActivePlotSlots`, embedded split, `kOutView` render, remove `ScopeModeOf`/`DrawViewStrip` |
| `tests/test_interaction.cc` | Modify: power-on + OUT cycle/hold/applicability |
| `tests/test_bindings.cc` | Modify: applicability + `kOutView` resolution cases |
| `tests/test_panel.cc` | Modify: re-bake golden hash |
| `tests/test_panel_pages.cc` | Modify: re-bake per-page hashes (twice: phase 2 intermediate, phase 3 final) + MOD goldens |
| `docs/archive/workflow/plans/2026-09-14_nostromo-interaction_plan.md` | Move (archive) |
| `docs/workflow/arch-designs/nostromo-interaction_arch-design.md` | Modify: one contingency line (§4.1) |

No new files; no `CMakeLists.txt` change. The one new include — `pages.cc` gains
`#include "params.h"` for the `modulatable` descriptor flag — is within the
already-linked `engine` package.

### 2.2. Dependency Graph

No inter-package edges change. The data-flow direction is unchanged: the audio
thread still sets `scope_dirty` → `PanelDraw` drains it; the interaction layer
still calls `MarkDirty` as its sole invalidation channel. What changes is *which
slots* the panel paints (two, not one) and *how often* it services the output
(phase 1's interval) — see §4.2 and §4.9.

## 3. Interface Changes

### `geom.h` — geometry split

```cpp
// Changed (was 20):
inline constexpr int kSubjectCount = 17;

// Changed (was 11 rows / 4 strips, with OUT in both comments):
inline constexpr int kPaneRowsN   = 10;  // PART FILT AMP MOD OSC ENV LFO FX PATCH CONF
inline constexpr int kPaneStripsN = 3;   // OSC ENV LFO (OUT removed entirely)
// kPaneNeedH re-derives to 359 (10*26 + 3*(22+8) + 3 + 6).

// Added:
inline constexpr int kEmbedGap = 20;
inline constexpr int kEmbedW   = (kPlotW - kEmbedGap) / 2;             // 440
inline constexpr int kEmbedX(int half) {
  return kPlotX + half * (kEmbedW + kEmbedGap);                        // 108 / 568
}

// Added guard (replaces the "OUT view strip" assert):
static_assert(kEmbedW * 2 + kEmbedGap == kPlotW,
              "embedded plot halves must tile the plot width exactly");
```

### `feel.h` — the refresh interval

```cpp
struct FeelProfile {
  // ... existing five input-feel fields ...
  std::uint32_t scope_interval_ms;   ///< min ms between output-view redraws.
                                     ///< The one display-timing field here
                                     ///< (Design Decision A): a refresh
                                     ///< throttle, not an input feel.
};
```

`DefaultFeel()` sets `scope_interval_ms = 33`.

### `interaction.h` — vocabulary

```cpp
// Control::kOut comment (was "momentary, jump to kOutScope and back"):
  kOut,   ///< momentary: tap cycles scope_mode, hold toggles kOutView

// SubjectId — three globals removed; kCount is now 17.
  kMod,
  kFx, kPatch, kConf,
  kCount,        ///< 17; asserted == geom::kSubjectCount in pages.h

// ViewMode — one latched mode added:
enum class ViewMode : std::uint8_t {
  kEdit = 0, kModArm, kModView,
  kOutView,    ///< OUT held — latched, full-screen output + settings
  kPerform,
};

// ScopeMode — new (the embedded-output mode, NOT the panel's old display enum):
enum class ScopeMode : std::uint8_t {
  kOff = 0, kScope, kCycle, kSpectrum,
};

// NavPos — removed (OUT hold is a mode toggle, not navigation).

// NavState — `prev` removed, `scope_mode` added:
struct NavState {
  std::uint8_t part;
  SubjectId    subject;
  std::uint8_t group;
  std::uint8_t item[static_cast<int>(SubjectId::kCount)];
  std::int8_t  focus_col;
  ViewMode     mode;
  ScopeMode    scope_mode;           ///< global, not per-subject
  engine::ModSourceId armed_source;
  bool         route_full;           ///< unchanged
};
```

### `panel.h` — remove `enum class ScopeMode`

Deleted outright. `panel.cc` already includes `interaction.h`, so the display
view is `interaction.h`'s `ScopeMode`; the output view always lives in `kSlotOut`
(§4.9), so `DrawOutPlot` is only called with `scope_mode ∈ {kScope,kCycle,kSpectrum}`
after the `kOff`→`kScope` fix (Design Decision 2).

### `pages.h` — `ViewCtl` +10

```cpp
enum class ViewCtl : std::uint8_t {
  kCategory, kSort, kFavourite, kAction,                     // PATCH
  kDetents, kAccelMax, kAccelThresh, kLongPress, kFineDiv,   // CONF
  kScopeRefresh,                                             // CONF (rate-limit)
  kTimebase, kCycles, kRange,                                // kOutView col 1
  kScale,                                                     // kOutView col 2 (shared)
  kTrigger, kAlign, kAverage,                                // kOutView col 3
  kHold, kWindow,                                            // kOutView col 4
};
```

### `pages.cc` — tables, `kOutColumns`, `k_pages`, `ResolveBinding`

```cpp
// kColsOutScope/Cycle/Spec: kPending → kViewCtl, SOURCE column removed.
constexpr ColumnSpec kColsOutScope[] = {
    {ColumnKind::kViewCtl, "TIMEBASE", {.ctl = ViewCtl::kTimebase}},
    {ColumnKind::kViewCtl, "SCALE",    {.ctl = ViewCtl::kScale}},
    {ColumnKind::kViewCtl, "TRIGGER",  {.ctl = ViewCtl::kTrigger}},
    {ColumnKind::kViewCtl, "HOLD",     {.ctl = ViewCtl::kHold}},
};
// … kColsOutCycle {CYCLES, SCALE, ALIGN, HOLD}
// … kColsOutSpec  {RANGE,  SCALE, AVERAGE, WINDOW}

constexpr const ColumnSpec *kOutColumns[4] = {
  kColsOutScope, kColsOutScope, kColsOutCycle, kColsOutSpec,
};

// k_pages: the three kOutScope/kOutCycle/kOutSpec PageDesc entries are deleted.

bool HasModulatableColumn(SubjectId s);  // walks k_pages[s].cols; true iff any
                                         // kParam col has k_params[].modulatable
```

`ResolveBinding` changes (in `pages.cc`):

- `case Control::kOut`: `b.kind = (dyn_slot >= 0) ? kOutToggle : kNone`.
- `case Control::kMod`: `b.kind = HasModulatableColumn(subject) ? kModeToggle : kNone`.
- Column encoders, before the `switch (col.kind)`: a `kOutView` branch resolves
  `kOutColumns[scope_mode][n]` (n < 4 → `kViewCtl`; n ≥ 4 → `kNone`).

### `interaction.cc` — dispatch and lifecycle

```cpp
// Removed: bool IsOut(SubjectId).
// Added (file-local):
ScopeMode NextScopeMode(ScopeMode m);  // kOff→kScope→kCycle→kSpectrum→kOff

// kOutToggle dispatcher (replaces jump-and-return):
case BindKind::kOutToggle: {
  if (g == Gesture::kPressShort) { nav.scope_mode = NextScopeMode(nav.scope_mode); MarkAll(); }
  else if (g == Gesture::kPressLong) {
    if (nav.mode == ViewMode::kOutView) {
      nav.mode = ViewMode::kEdit;
    } else {
      nav.mode = ViewMode::kOutView;
      if (nav.scope_mode == ScopeMode::kOff) nav.scope_mode = ScopeMode::kScope;
    }
    MarkAll();
  }
  break;
}

// Init (was subject kOutScope, prev {kFilt,0,-1}):
nav.subject = SubjectId::kFilt;
nav.scope_mode = ScopeMode::kScope;
// nav.prev removed.
```

### `panel.cc` — stride, drain, two slots

- **Stride** (`DrawScopePlot`): `kStride = ScopeRing::kCapacity / w` (runtime).
- **Drain** (`PanelDraw`): interval-checked latch (§4.2), still `MarkDirty(kSlotOut)`.
- **Removed**: `ScopeModeOf(SubjectId)`; `DrawViewStrip` and its call.
- **Changed**: `DrawOutPlot` switches on `nav.scope_mode`; `IsGlobalSubject`
  returns `>= SubjectId::kFx`.
- **Two slots** (§4.9): `ActivePlotSlot` → `ActivePlotSlots{page,out}`; per-mode
  rects; `DrawColumns` uses `kOutColumns[scope_mode]` in `kOutView`.

## 4. Solution Breakdown

### 4.1 Scope stride from width (`nostromo/panel.cc`)

`DrawScopePlot`'s `kStride = kCapacity / kPlotW` (18) is only correct at full
band. At `w = 440` the ring is read at stride 18, spanning `440·18 = 7920` of
16384 samples — a 48%-of-window crop, not a magnify. The cycle and spectrum
paths are already width-correct, so the fix is scope-only: compute the stride
from `w` at runtime.

- **Edge cases**: `kCapacity / w` at `w = 440` is 37, `440·37 = 16280 ≤ 16384`
  (full window); at `w = 900` it is 18 — unchanged. `buf[kPlotW]` stays sized
  to the max width.
- **Dependencies**: none (lands on today's full-band scope).
- **Done**: task 1.6 (build + tests) and the phase-3 smoke render at half width.

### 4.2 Output-refresh rate-limit (`feel.h`, `feel.cc`, `panel.cc`)

`PanelAudioTap` stays exactly as it is — one relaxed store. The change is on the
drain side only. The tap sets a `scope_pending` latch; `PanelDraw` services it
when `NowMs() - last_scope_ms >= feel.scope_interval_ms`. The separate latch
matters: `exchange` clears the atomic, so without it a tap arriving inside the
interval window is dropped and the scope stalls until the next tap.

- **Edge cases**: a tap inside the interval is remembered, not lost. The default
  interval (33 ms ≈ 30 Hz) is shorter than the test frame gaps, so existing
  render tests pass unchanged.
- **Dependencies**: produces the ceiling that phase 3's embedded split inherits;
  consumes `NowMs()` (already in `panel.cc`) and `p->interaction->feel`.
- **Done**: task 1.6.

### 4.3 Geometry split (`nostromo/geom.h`)

Set `kSubjectCount = 17`, reduce the pane to 10 rows / 3 strips, add the three
embed constants and the tiling guard, drop the OUT strip assert.

- **Edge cases**: `kPaneNeedH` (359) stays ≤ `kPaneH` (526). `kEmbedW = 440`
  tiles `kPlotW` exactly.
- **Dependencies**: produces `kSubjectCount` (asserted by `pages.h`), `kEmbedW/Gap/X`
  consumed by `panel.cc` (phase 3).
- **Done**: task 2.10.

### 4.4 `ScopeMode` and `NavState` (`nostromo/interaction.h`)

The embedded-output mode is a global value, not a subject and not per-part.
`scope_mode` is the single source of truth; `prev`/`NavPos` die because hold is a
mode toggle with nothing to restore.

- **Edge cases**: `NavState{}` zero-inits `scope_mode` to `kOff`; `Init` sets `kScope`.
- **Dependencies**: consumed by `pages.cc` (4.6), `interaction.cc` (4.7), `panel.cc` (4.8–4.9).
- **Done**: task 2.10.

### 4.5 Page-table retirement (`nostromo/pages.cc`)

Delete the three OUT `PageDesc` entries and the old `kPending` SOURCE tables.
The per-mode `kViewCtl` tables return in phase 3 as `kOutColumns`.

- **Edge cases**: nothing references the deleted tables after the entries go;
  `k_pages` is still indexed by the 17-entry `SubjectId`.
- **Dependencies**: consumes the 17-entry `SubjectId` (4.4).
- **Done**: task 2.10.

### 4.6 `ResolveBinding`: applicability (`nostromo/pages.cc`)

Two gates on the pure resolver. OUT → `kNone` unless the page has a plot; MOD →
`kNone` unless `HasModulatableColumn` returns true — which keeps arming inert on
the MOD page (route-field columns) and on PART/AMP/FX/PATCH/CONF. The predicate
reads the `engine::k_params` descriptor table (const), so the resolver stays pure.

- **Edge cases**: a page with `kParam` columns but none modulatable (none today,
  but structurally possible) gates MOD to `kNone`.
- **Dependencies**: consumes `params.h` (new include) and `k_pages` (4.5).
- **Done**: task 2.10, plus the resolution cases in task 2.7.

### 4.7 OUT dispatch: cycle (`nostromo/interaction.cc`)

`kOutToggle` fires on the up edge (the recognizer emits `kPressShort`/`kPressLong`
there, matching MOD's tap/hold). Tap cycles `scope_mode`; hold is phase 3. Both
`MarkAll()` — the mode is global, so every plot page's render changes even though
only the active slot repaints.

- **Edge cases**: OUT down edge emits `kNone` and is ignored; applicability (§4.6)
  guarantees the case runs only on plot pages.
- **Dependencies**: consumes `NextScopeMode`, `ScopeMode` (4.4).
- **Done**: task 2.10; behavior locked by task 2.8.

### 4.8 Panel scaffolding removal (`nostromo/panel.cc`)

Delete `ScopeModeOf` (subject→view map), `DrawViewStrip` and its call, and point
`DrawOutPlot` at `nav.scope_mode`. `IsGlobalSubject` moves its boundary to
`>= kFx`. `ActivePlotSlot` becomes: `scope_mode != kOff` → `kSlotOut` (output
full-band), else the page's `dyn_slot`. This is the intermediate bisect state —
the output is still full-band, just driven by `scope_mode` instead of a subject.

- **Edge cases**: power-on `kFilt` + `kScope` shows the output full-band until the
  phase-3 split restores the page plot alongside it.
- **Dependencies**: consumes `SubjectId` (17) and `ScopeMode` (4.4).
- **Done**: task 2.10; locked by the phase-2 hash re-bake (2.9).

### 4.9 `kOutView` tables (`pages.h`, `pages.cc`)

Three four-column `kViewCtl` tables indexed by `scope_mode`; no `SOURCE` column.
The fifth encoder is `kNone` — the output view has exactly four settings.

- **Edge cases**: `kOff` shares `kColsOutScope` (`kOutColumns[0]`), though after
  the `kOff`→`kScope` entry fix `kOff` never coexists with `kOutView`.
- **Dependencies**: consumes `ViewCtl` (3.1); produces `kOutColumns` for
  `ResolveBinding` (3.2) and `DrawColumns` (4.10).
- **Done**: task 3.8.

### 4.10 Two active slots, embedded split, `kOutView` (`interaction.cc`, `panel.cc`)

`ActivePlotSlot` becomes `ActivePlotSlots` returning `{page, out}` (each `std::int8_t`,
`-1` = none): `kEdit`+`kOff` → `{page.dyn_slot, -1}`; `kEdit`+`!=kOff` →
`{page.dyn_slot, kSlotOut}`; `kOutView` → `{-1, kSlotOut}`. Each active slot's
rect is set from the mode (page: full band or `kEmbedX(0)` half; out: `kEmbedX(1)`
half or full band), and the draw loop paints both active slots rect-driven. The
two halves are disjoint rects, so the single-writer invariant holds per rect and
`kSlotOut` keeps its own trace buffer (`traces[3]`) — the erase path is unchanged.

This is what makes the output invalidate independently: a parameter change
dirties the page slot only; a trace tick dirties `kSlotOut` only. The filter
curve recomputes when the filter changes, not on every audio callback. It also
collapses the embedded and `kOutView` paths into one rect-driven loop.

`DrawColumns` swaps its column source to `kOutColumns[scope_mode]` in `kOutView`;
`ModePrefix` emits the mode prefix. The `kOutToggle` hold handler forces
`scope_mode = kScope` on `kOutView` entry from `kOff` (Design Decision 2), so the
first tap after entry visibly advances.

- **Edge cases**: `pending[]`/`damage[]` bookkeeping is already per-slot; two
  slots live at once, so each must clear its own `pending` counter (this is the
  region the earlier `DrawDyn` bug lived in — review carefully). `scope_dirty`
  keeps targeting `kSlotOut`; when `scope_mode == kOff` and no `kOutView`, the
  flag is drained and discarded.
- **Dependencies**: consumes `kEmbedX/W` (4.3), `kOutColumns` (4.9).
- **Done**: task 3.8; locked by task 3.7's hash re-bake and the smoke render.

## 5. Design Decisions

1. **Delete `panel.h`'s `ScopeMode`; reuse `interaction.h`'s.** The panel's
   `enum class ScopeMode {kScope,kCycle,kSpectrum}` collides with the new
   embedded-mode enum once both headers meet in `panel.cc`. Considered renaming
   the panel's to `OutPlotKind`. Chose deletion: the output view now always lives
   in `kSlotOut` (§4.10), so the display view is `scope_mode` minus `kOff`, and a
   second enum would be a second source of truth for one fact.

2. **Entering `kOutView` from `kOff` forces `scope_mode = kScope`.** Without it,
   `DrawOutPlot` would draw a scope while `scope_mode` was still `kOff`, and the
   next tap (kOff→kScope) would change nothing visible — a dead first press.
   Forcing the mutation on entry makes the cycle honest and lets `DrawOutPlot`
   never see `kOff`. Considered making the hold inert at `kOff`; rejected because
   a hold that sometimes does nothing is worse than a hold that always enters the
   mode.

3. **The embedded output gets its own slot, not a draw inside the page hook.** The
   four-slot model paints only the active page's plot into the shared band; putting
   `DrawOutPlot` inside `PlotOsc`/`PlotFilter`/`PlotEnv` (the earlier draft) would
   make every trace tick redraw the page's own plot — a full filter-curve recompute
   per callback. Two active slots with disjoint rects keep the single-writer
   invariant per rect and give independent invalidation. This is the reviewer's
   finding 3b, adopted in place rather than as a follow-up because it changes the
   split's shape.

4. **The output refresh is rate-limited, not timer-driven.** `PanelAudioTap` stays
   a single relaxed store; the drain throttles to `scope_interval_ms`. This converts
   an unbounded repaint cost into a chosen ceiling (finding 3a) — the only form of
   control available without target hardware — and also bounds finding 3's waste.
   Invariant 1 ("no timer") is untouched: no audio-thread timing logic is added,
   and the throttle is an existing-`MarkDirty`-path interval, not a scheduler.

5. **`scope_interval_ms` lives in `FeelProfile`, flagged as the display-timing
   exception.** It is the first field that is not an input feel. Considered a
   sibling `DisplayProfile`; rejected for one field. The doc-comment records the
   exception, and invariant 12 is respected: the interval does not change which
   parameter a control drives, how many columns exist, or what is drawn where — it
   only throttles how often the live output redraws, the display analogue of
   `long_press_ms`.

6. **`HasModulatableColumn` reads the `engine::k_params` descriptor table.** The
   MOD applicability gate needs the `modulatable` flag, a const descriptor field,
   not engine state. This matches `NextModulatable`; the resolver stays pure.

7. **OUT hold toggles `kOutView` on release (`kPressLong`).** The recognizer
   emits long-press only on the up edge, like MOD's tap/hold. OUT has no momentary
   arm state, so its down edge emits `kNone` and is ignored; no threshold callback
   is added for one control.

8. **A `scope_mode` change marks all four slots.** The mode is global, so every
   plot page's render changes. `MarkAll()` is the existing helper; the panel only
   repaints active slots, so the cost stays bounded (invariant 13).

9. **`kOutView` has four settings; the fifth encoder is `kNone`.** The `kColsOut*`
   tables are four-element arrays; the `kOutView` resolution bounds-checks `n < 4`,
   mirroring `Column()`'s partial-final-group handling.

## 6. Success Criteria

- [ ] `SubjectId::kCount` is 17 and `static_assert(kCount == kSubjectCount)` holds (2.1, 2.2).
- [ ] `kEmbedW*2 + kEmbedGap == kPlotW` static_assert compiles (2.1).
- [ ] `DrawScopePlot` at `w = 440` reads the full ring window (stride = `kCapacity / w`) (1.1, 3.8 smoke).
- [ ] `scope_interval_ms` defaults to 33 and the drain throttles through the `scope_pending` latch (1.2, 1.4).
- [ ] `NavState` has no `prev` and `NavPos` is gone; `scope_mode` is present (2.2).
- [ ] `ResolveBinding(kFilt, kOut)` → `kOutToggle`; `ResolveBinding(kPart, kOut)` → `kNone` (2.7).
- [ ] `ResolveBinding(kMod page, kMod)` → `kNone`; `ResolveBinding(kFilt, kMod)` → `kModeToggle` (2.7).
- [ ] `ResolveBinding(kOutView, Enc(n<4))` → `kViewCtl` with `kOutColumns[scope_mode][n].ctl`; `Enc(4)` → `kNone` (3.5).
- [ ] OUT tap on a plot page cycles `scope_mode`; `subject`/`group`/`focus_col` unchanged (2.8).
- [ ] OUT hold on a plot page toggles `kOutView`; hold from `kOff` forces `kScope` (3.6).
- [ ] A parameter change dirties the page slot only; a trace tick dirties `kSlotOut` only (3.4).
- [ ] Power-on is `kFilt` + `scope_mode == kScope` (2.8, 2.9).
- [ ] Golden hashes re-baked and green at both the phase-2 intermediate and the phase-3 final (2.9, 3.7).
- [ ] Full `ctest` green — 20 tests (1.6, 2.10, 3.8).
- [ ] The arch-design carries the frame-budget contingency line; the superseded plan is archived (4.1, 4.2).

## 7. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/nostromo-interaction_arch-design.md` | Yes — the embedded default is contingent on an unmeasured frame budget, which is not recorded there. | Task 4.1: add one contingency line (fallback `kOff` + `kOutView`). |
| `../arch-designs/output-stage_arch-design.md` | No — the tap-point note (post-amp) is a future engine concern, out of this plan's scope. | None. |
| `../briefs/2026-09-16_embedded-output-plot_brief.md` | No — the plan's companion framing. | None. |
| `../plans/2026-09-14_nostromo-interaction_plan.md` | Yes — describes `NavPos`/`prev`, 20 subjects, and OUT jump-and-return. | Task 4.2: archive. |
| `../briefs/2026-09-15_nostromo-edit-screen-completion_brief.md` | Minor — lists "§11 OUT round-trip test" as a P3 item. | None — a write-once framing brief; the reference is historical. |
| `../../random/engine-recommendations.md` | No — perf guidance (§5.5/§5.7/§6.2/§7.2) unchanged; no OUT-subject references. | None. |

## 8. Cleanup

None. This change adds no diagnostic instrumentation, logs, or temporary flags.
The only removals are of obsolete scaffolding (`NavPos`, `IsOut`, `ScopeModeOf`,
`DrawViewStrip`, the three `PageDesc` entries), which is the clean-cutover intent,
not instrumentation.
