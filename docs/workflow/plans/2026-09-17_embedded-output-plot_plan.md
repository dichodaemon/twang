---
title: Embedded Output Plot -- Implementation Plan
status: draft
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

**Phases:**

1. **Core** — geometry, vocabulary, page table, resolution, dispatch, and panel
   rendering. One atomic change: the `SubjectId`/`ScopeMode` removals force all
   three consumers (`pages.cc`, `interaction.cc`, `panel.cc`) to move together,
   so there is no buildable half-way point. (depends on nothing)
2. **Tests** — rewrite the OUT round-trip, add applicability/`kOutView` coverage,
   re-bake the golden hashes. (depends on phase 1)
3. **Documentation** — archive the superseded interaction plan. (depends on
   phase 1)

| # | Task | Status |
|---|---|---|
| 1.1 | `nostromo/geom.h` — `kSubjectCount` 20→17; `kPaneRowsN` 11→10, `kPaneStripsN` 4→3 (drop OUT from both pane comments); add `kEmbedGap`/`kEmbedW`/`kEmbedX(int)`; replace the "OUT view strip" static_assert with `kEmbedW*2 + kEmbedGap == kPlotW` | Pending |
| 1.2 | `nostromo/interaction.h` — `SubjectId`: delete `kOutScope`/`kOutCycle`/`kOutSpec` (→17); `ViewMode`: add `kOutView`; add `enum class ScopeMode {kOff,kScope,kCycle,kSpectrum}`; delete `NavPos`; `NavState`: delete `prev`, add `ScopeMode scope_mode`; update `Control::kOut` comment | Pending |
| 1.3 | `nostromo/panel.h` — delete `enum class ScopeMode` (superseded by `interaction.h`'s `ScopeMode`) | Pending |
| 1.4 | `nostromo/pages.h` — `ViewCtl`: add `kTimebase,kCycles,kRange,kScale,kTrigger,kAlign,kAverage,kHold,kWindow` | Pending |
| 1.5 | `nostromo/pages.cc` — rewrite `kColsOutScope/Cycle/Spec` as `kViewCtl` tables (drop SOURCE, four cols each per §7.4); add `kOutColumns[4]`; delete the three `k_pages` entries; `ResolveBinding`: gate `kOut`→`kNone` on `dyn_slot==-1`, gate `kMod`→`kNone` via `HasModulatableColumn`, resolve `kOutView` encoders to `kOutColumns[scope_mode][n]`; add `HasModulatableColumn`; `#include "params.h"` | Pending |
| 1.6 | `nostromo/interaction.cc` — delete `IsOut`; add `NextScopeMode`; rewrite the `kOutToggle` dispatcher (tap cycles `scope_mode`, hold toggles `kOutView`, both `MarkAll()`); `Init`: power-on `subject kFilt` + `scope_mode kScope`, drop `prev`; remove all `nav.prev` uses | Pending |
| 1.7 | `nostromo/panel.cc` — delete `ScopeModeOf` + `DrawViewStrip` (+ its call); `DrawOutPlot` switches on `nav.scope_mode`; `IsGlobalSubject` → `>= kFx` | Pending |
| 1.8 | `nostromo/panel.cc` — embedded split: `PlotOsc`/`PlotFilter`/`PlotEnv` draw the page plot into half 0 and `DrawOutPlot` into half 1 when `scope_mode != kOff`, else the full band | Pending |
| 1.9 | `nostromo/panel.cc` — `kOutView`: `ActivePlotSlot` returns `kSlotOut` in `kOutView`; `DrawColumns` shows `kOutColumns[scope_mode]`; `kOutView` mode prefix; `scope_dirty` drain marks the active output-bearing slot | Pending |
| 1.10 | Verify: `cmake --build /tmp/twang-build`; `cmake --build /tmp/twang-build --target panel_shot && /tmp/twang-build/panel_shot /tmp/embedded.png` and confirm the power-on page shows the filter plot + embedded scope (`read /tmp/embedded.png?q=...`); drive an OUT hold to `kOutView` in a throwaway render and confirm the full-screen output + settings columns; `ctest --test-dir /tmp/twang-build -E 'panel|interaction'` — 17 pass | Pending |
| 2.1 | `tests/test_interaction.cc` — rewrite the power-on navigation (subject is now `kFilt`) and the OUT round-trip → tap cycles `scope_mode`, hold toggles `kOutView`, no-plot pages inert | Pending |
| 2.2 | `tests/test_bindings.cc` — OUT → `kNone` on a no-plot page; MOD → `kNone` on a non-modulatable page (MOD page); `kOutView` encoder → `kOutColumns[scope_mode][n]`; the E-sweep remains valid at 17 subjects | Pending |
| 2.3 | `tests/test_panel.cc` — re-bake `kExpectedHash`: run the test, read the printed `buffer0 hash`, write it into the constant (power-on is now `kFilt` + embedded scope) | Pending |
| 2.4 | `tests/test_panel_pages.cc` — re-bake `kGolden[17]` (drop the three OUT rows) and `kModViewGolden`/`kModArmGolden`: render each page offscreen, read the hash, update the constant | Pending |
| 2.5 | Verify: `ctest --test-dir /tmp/twang-build` — all 20 tests green | Pending |
| 3.1 | Archive `docs/workflow/plans/2026-09-14_nostromo-interaction_plan.md` → `docs/archive/workflow/plans/` (superseded OUT/NavPos/20-subject design) | Pending |
| 3.2 | Verify: `git status` shows only the intended changes; arch-design/brief need no edit (already reconciled in `d91ccbc`) | Pending |

## 2. Architecture

### 2.1. Directory Layout

| File | Change |
|---|---|
| `nostromo/geom.h` | Modify: subject count, pane rows/strips, embed constants, static_asserts |
| `nostromo/interaction.h` | Modify: `SubjectId` (17), `ViewMode`+`kOutView`, add `ScopeMode`, remove `NavPos`, `NavState` |
| `nostromo/panel.h` | Modify: remove `enum class ScopeMode` |
| `nostromo/pages.h` | Modify: `ViewCtl` +9 entries |
| `nostromo/pages.cc` | Modify: `kColsOut*` tables, `kOutColumns`, `k_pages` −3, `ResolveBinding`, `HasModulatableColumn` |
| `nostromo/interaction.cc` | Modify: `kOutToggle` dispatch, `Init`, remove `IsOut`/`NavPos` uses, add `NextScopeMode` |
| `nostromo/panel.cc` | Modify: `DrawOutPlot`, embedded split, `kOutView` render, remove `ScopeModeOf`/`DrawViewStrip` |
| `tests/test_interaction.cc` | Modify: power-on + OUT round-trip → cycle/hold/applicability |
| `tests/test_bindings.cc` | Modify: applicability + `kOutView` resolution cases |
| `tests/test_panel.cc` | Modify: re-bake golden hash |
| `tests/test_panel_pages.cc` | Modify: re-bake 17 per-page hashes + MOD goldens |
| `docs/archive/workflow/plans/2026-09-14_nostromo-interaction_plan.md` | Move (archive) |

No new files; no `CMakeLists.txt` change (the lib sources and test targets are
unchanged). The one new include — `pages.cc` gains `#include "params.h"` for the
`modulatable` descriptor flag — is within the already-linked `engine` package.

### 2.2. Dependency Graph

No inter-package edges change. The data-flow direction is unchanged: the audio
thread still sets `scope_dirty` → `PanelDraw` drains it; the interaction layer
still calls `MarkDirty` as its sole invalidation channel. What changes is
*which* slot the drain and the OUT dispatcher mark (see §5.9).

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

The `kStripW`/`StripW` helpers and the digit-strip assert stay; only the
OUT-specific strip assert and the pane counts change.

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

// NavPos — removed (OUT hold is a mode toggle, not navigation; no return position).

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
  bool         route_full;           ///< unchanged (route-table-full alert)
};
```

### `panel.h` — remove `enum class ScopeMode`

Deleted outright. `panel.cc` already includes `interaction.h`, so the display
view is now `interaction.h`'s `ScopeMode`; `DrawOutPlot` is only ever called
with `scope_mode ∈ {kScope,kCycle,kSpectrum}` (see Design Decisions §1).

### `pages.h` — `ViewCtl` +9

```cpp
enum class ViewCtl : std::uint8_t {
  kCategory, kSort, kFavourite, kAction,                     // PATCH
  kDetents, kAccelMax, kAccelThresh, kLongPress, kFineDiv,   // CONF
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

// New file-local predicate:
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
    nav.mode = (nav.mode == ViewMode::kOutView) ? ViewMode::kEdit : ViewMode::kOutView;
    MarkAll();
  }
  break;
}

// Init (was subject kOutScope, prev {kFilt,0,-1}):
nav.subject = SubjectId::kFilt;
nav.scope_mode = ScopeMode::kScope;
// nav.prev removed.
```

### `panel.cc` — rendering

- **Removed**: `ScopeModeOf(SubjectId)` (subject → display mode); `DrawViewStrip`
  and its call in `DrawEditChrome`.
- **Changed**: `DrawOutPlot` switches on `p.interaction->Nav().scope_mode`
  (`kCycle`→cycle, `kSpectrum`→spectrum, `kScope`/`kOff`→scope); `IsGlobalSubject`
  returns `>= SubjectId::kFx` (was `>= kOutScope`).
- **Embedded split** (`PlotOsc`/`PlotFilter`/`PlotEnv`): when
  `scope_mode != kOff`, draw the page's plot into
  `Rect{kEmbedX(0), kPlotY, kEmbedW, kPlotH}` and `DrawOutPlot` into
  `Rect{kEmbedX(1), kPlotY, kEmbedW, kPlotH}`; else the full band as today.
- **`kOutView`**: `ActivePlotSlot` returns `kSlotOut` when
  `nav.mode == kOutView`; `DrawColumns` uses `kOutColumns[scope_mode]` in that
  mode; `ModePrefix` emits the `kOutView` prefix; the `scope_dirty` drain marks
  the active output-bearing slot (see §5.9).

## 4. Solution Breakdown

### 4.1 Geometry split (`nostromo/geom.h`)

Location: `namespace nostromo::geom`. Step: set `kSubjectCount = 17`; reduce the
pane to 10 rows/3 strips with corrected comments; add the three embed constants
and the tiling guard; drop the OUT strip assert.

- **Edge cases**: `kPaneNeedH` must stay ≤ `kPaneH` (526) — 359 satisfies it.
  `kEmbedW = (900−20)/2 = 440` tiles exactly.
- **Dependencies**: produces `kSubjectCount`, `kEmbedW/Gap/X` consumed by
  `pages.h`'s `static_assert` (1.2), `interaction.h` (1.2), and `panel.cc` (1.8).
- **Done**: task 1.10 — build succeeds and `static_assert(SubjectId::kCount ==
  geom::kSubjectCount)` holds at 17.

### 4.2 `ScopeMode` and `NavState` (`nostromo/interaction.h`)

The embedded-output mode is a global value, not a subject and not per-part.
`NavState::scope_mode` is the single source of truth; `prev`/`NavPos` die
because hold is a mode toggle with nothing to restore.

- **Edge cases**: `NavState{}` zero-initialises `scope_mode` to `kOff`; `Init`
  explicitly sets `kScope` (4.6).
- **Dependencies**: produces `ScopeMode`, `kOutView`, `scope_mode` consumed by
  `pages.cc` (4.4), `interaction.cc` (4.5), `panel.cc` (4.7–4.9).
- **Done**: task 1.10.

### 4.3 Page table: three per-mode tables + `kOutColumns` (`nostromo/pages.cc`)

Replace the three `kPending` SOURCE tables with `kViewCtl` tables of four
columns each, and index them by `scope_mode`. No `SOURCE` column: the output is
always the current part. The fifth encoder is `kNone` — the output view has
exactly four settings.

- **Edge cases**: `kOff` shares `kColsOutScope` (`kOutColumns[0]`), matching the
  "off is treated as scope" rule in `kOutView`.
- **Dependencies**: consumes `ViewCtl` (1.4); produces `kOutColumns` consumed by
  `ResolveBinding` (4.4) and `DrawColumns` (4.9).
- **Done**: task 1.10.

### 4.4 `ResolveBinding`: applicability + `kOutView` columns (`nostromo/pages.cc`)

Three additions to the pure resolver. (a) OUT: `kNone` unless the page has a
plot (`dyn_slot != -1`). (b) MOD: `kNone` unless `HasModulatableColumn`
returns true — which is what keeps arming inert on the MOD page (its columns
are route fields) and on PART/AMP/FX/PATCH/CONF (their columns are
`kPending`/`kViewCtl`). (c) `kOutView` column encoders resolve
`kOutColumns[scope_mode][n]` to a `kViewCtl` binding (n < 4; n ≥ 4 → `kNone`).

- **Edge cases**: `HasModulatableColumn` reads the `engine::k_params` descriptor
  table (const), so the resolver stays pure — no `GetParam`, no writes. A page
  with `kParam` columns but none modulatable (none today, but structurally
  possible) gates MOD to `kNone`.
- **Dependencies**: consumes `kOutColumns` (4.3), `ViewCtl` (1.4), `params.h`;
  produces the bindings the dispatcher (4.5) applies.
- **Done**: task 1.10, plus the resolution cases in task 2.2.

### 4.5 OUT dispatch: cycle + hold (`nostromo/interaction.cc`)

`kOutToggle` fires on the up edge (the recognizer emits `kPressShort`/
`kPressLong` there, matching MOD's tap/hold convention). Tap cycles
`scope_mode`; hold toggles `kOutView`. Both `MarkAll()` — the mode is global,
so every plot page's render changes even though only the active slot repaints.

- **Edge cases**: the OUT down edge emits `kNone` and is ignored (no momentary
  OUT state); applicability (§4.4) guarantees the case only runs on plot pages.
  A second hold returns to `kEdit`.
- **Dependencies**: consumes `NextScopeMode`, `ScopeMode`, `kOutView` (1.2).
- **Done**: task 1.10; behavior locked by task 2.1.

### 4.6 Power-on init (`nostromo/interaction.cc`)

`Init` zeroes `NavState` then sets `subject = kFilt`, `scope_mode = kScope`,
`group = 0`, `mode = kEdit`. Power-on shows the filter curve plus the embedded
scope — the output view without an output subject. `MarkAll()` as today.

- **Edge cases**: `prev` no longer exists, so nothing to initialise there.
- **Dependencies**: consumes the new `NavState` layout (1.2).
- **Done**: task 1.10; locked by the `test_interaction` power-on navigation (2.1).

### 4.7 Panel scaffolding removal (`nostromo/panel.cc`)

Delete `ScopeModeOf` (the subject→view map), `DrawViewStrip` and its call (the
OUT "SCOPE/CYCLE/SPEC" selector), and point `DrawOutPlot` at `nav.scope_mode`.
`IsGlobalSubject` moves its boundary from `>= kOutScope` to `>= kFx`, so the
title bar's `GL` vs `P<n>` stays correct after the OUT globals disappear.

- **Edge cases**: `DrawOutPlot`'s `kOff` case falls through to scope (only
  reachable inside `kOutView`).
- **Dependencies**: consumes `SubjectId` (17) and `ScopeMode` (1.2).
- **Done**: task 1.10.

### 4.8 Embedded split (`nostromo/panel.cc`)

The three plot hooks each check `nav.scope_mode`. When `!= kOff`, they fill the
page's own plot into half 0 and call `DrawOutPlot` into half 1; otherwise the
full 900×404 band. The column traces stay `kPlotW`-wide and are indexed
`[0, w)`, so a 440-wide half simply uses the first 440 columns — no resize, no
misalignment.

- **Edge cases**: the hooks already `FillRect` the full band and clip to their
  rect; the split introduces a second fill+draw per hook. `PlotOut` (slot 3) is
  unchanged — it still draws the full band when `kSlotOut` is active (4.9).
- **Dependencies**: consumes `kEmbedX/W` (1.1) and `DrawOutPlot` (4.7).
- **Done**: task 1.10; visual confirmation via the smoke render (1.10) and the
  re-baked hashes (2.3, 2.4).

### 4.9 `kOutView` full-screen + `scope_dirty` drain (`nostromo/panel.cc`)

`ActivePlotSlot` returns `kSlotOut` when `nav.mode == kOutView`, so the output
fills the band and the page's own plot is suppressed. `DrawColumns` swaps its
column source to `kOutColumns[scope_mode]` in that mode, and `ModePrefix`
emits the mode prefix. The `scope_dirty` drain changes target: mark the *active
output-bearing* slot — `kSlotOut` in `kOutView`, else the page's `dyn_slot`
when `scope_mode != kOff`, else nothing.

- **Edge cases**: the audio thread's `PanelAudioTap` is unchanged; only the
  drain's target is computed. With `scope_mode == kOff` and no `kOutView`, the
  flag is drained and discarded (no output is drawn).
- **Dependencies**: consumes `kOutColumns` (4.3), `ActivePlotSlot` (existing).
- **Done**: task 1.10; locked by `test_panel` (2.3) and the embedded-scope
  hashes (2.4).

## 5. Design Decisions

1. **Delete `panel.h`'s `ScopeMode`; reuse `interaction.h`'s.** The panel's
   `enum class ScopeMode {kScope,kCycle,kSpectrum}` collides with the new
   embedded-mode enum in the same namespace once both headers meet in
   `panel.cc`. Considered renaming the panel's to `OutPlotKind`. Chose deletion:
   the display view is `scope_mode` minus `kOff`, and `DrawOutPlot` is never
   called in the `kOff` state, so a second enum is a second source of truth for
   one fact.

2. **`DrawOutPlot`'s `kOff` case falls through to scope.** `kOff` means "no
   output drawn" and is only reachable inside `kOutView`, where the contract
   says "off is treated as scope". A defensive default (not a separate hide
   branch) keeps the draw path total without a `kOff` special case.

3. **The embedded split lives in the page's own plot hook, not a new slot.** The
   four-slot model paints only the active page's plot into the shared band, and
   the embedded output must repaint with it (scope animates). Drawing half 0 and
   half 1 inside `PlotOsc`/`PlotFilter`/`PlotEnv` keeps the single-writer
   invariant and reuses the column-update erase, at the cost of one extra
   fill+draw per split frame.

4. **`scope_dirty` drain targets the active slot, not `kSlotOut`.** When the
   output is embedded it is drawn by the page's hook (slot 0/1/2), not slot 3,
   so marking `kSlotOut` would leave the embedded scope stale. The audio thread
   (`PanelAudioTap`) is unchanged; only the drain computes where the output
   currently lives.

5. **`HasModulatableColumn` reads the `engine::k_params` descriptor table.** The
   MOD applicability gate needs the `modulatable` flag, which is a const
   descriptor field, not engine state. This matches `NextModulatable` in
   `interaction.cc` (which already walks `k_params`); `pages.cc` gains the
   `params.h` include and the resolver stays pure (no `GetParam`, no writes).

6. **OUT hold toggles `kOutView` on release (`kPressLong`).** The recognizer
   emits long-press only on the up edge, the same as MOD's tap/hold. Adding a
   threshold callback for one control is not worth a new mechanism; the
   difference from MOD is only that OUT has no momentary arm state, so its down
   edge emits `kNone` and is ignored.

7. **A `scope_mode` change marks all four slots.** The mode is global, so every
   plot page's render changes. `MarkAll()` is the existing helper; the panel
   only repaints the active slot anyway, so the cost stays bounded (invariant 13).

8. **`kOutView` has four settings; the fifth encoder is `kNone`.** The output
   view carries exactly four controls per mode (`kColsOut*` are four-element
   arrays). The `kOutView` resolution bounds-checks `n < 4` and returns `kNone`
   past the end, mirroring `Column()`'s partial-final-group handling.

## 6. Success Criteria

- [ ] `SubjectId::kCount` is 17 and `static_assert(kCount == kSubjectCount)` holds (1.1, 1.2).
- [ ] `kEmbedW*2 + kEmbedGap == kPlotW` static_assert compiles (1.1).
- [ ] `NavState` has no `prev` and `NavPos` is gone; `scope_mode` is present (1.2).
- [ ] `ResolveBinding(kFilt, kOut)` → `kOutToggle`; `ResolveBinding(kPart, kOut)` → `kNone` (2.2).
- [ ] `ResolveBinding(kMod page, kMod)` → `kNone`; `ResolveBinding(kFilt, kMod)` → `kModeToggle` (2.2).
- [ ] `ResolveBinding(kOutView, Enc(n<4))` → `kViewCtl` with `kOutColumns[scope_mode][n].ctl`; `Enc(4)` → `kNone` (2.2).
- [ ] OUT tap on a plot page cycles `scope_mode` off→scope→cycle→spectrum→off; `subject`/`group`/`focus_col` unchanged (2.1).
- [ ] OUT hold on a plot page toggles `kOutView`; a second hold returns to `kEdit` (2.1).
- [ ] Power-on is `kFilt` + `scope_mode == kScope` (2.1, 2.3).
- [ ] `test_panel` and `test_panel_pages` hashes re-baked and green after the split (2.3, 2.4).
- [ ] Full `ctest` green — 20 tests (2.5).
- [ ] The superseded interaction plan is archived (3.1).

## 7. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/nostromo-interaction_arch-design.md` | No — the plan implements it; reconciled in `d91ccbc`. | None. |
| `../arch-designs/output-stage_arch-design.md` | No — the tap-point note (post-amp) is a future engine concern, out of this plan's scope. | None. |
| `../briefs/2026-09-16_embedded-output-plot_brief.md` | No — the plan's companion framing. | None. |
| `../plans/2026-09-14_nostromo-interaction_plan.md` | Yes — describes `NavPos`/`prev`, 20 subjects, and OUT jump-and-return. | Task 3.1: archive to `docs/archive/workflow/plans/`. |
| `../briefs/2026-09-15_nostromo-edit-screen-completion_brief.md` | Minor — lists "§11 OUT round-trip test" as a P3 item. | None — a write-once framing brief; the reference is historical. |
| `../../random/engine-recommendations.md` | No — perf guidance (§5.5/§5.7/§6.2/§7.2) unchanged; no OUT-subject references. | None. |

## 8. Cleanup

None. This change adds no diagnostic instrumentation, logs, or temporary flags.
The only removals are of obsolete scaffolding (`NavPos`, `IsOut`,
`ScopeModeOf`, `DrawViewStrip`, the three `PageDesc` entries), which is the
clean-cutover intent, not instrumentation.
