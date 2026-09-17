---
title: Output-View Settings and Visual Refinements -- Implementation Plan
status: approved
date: 2026-09-17
author: Dizan Vasquez
arch-design: ../arch-designs/nostromo-interaction_arch-design.md
---

# Output-View Settings and Visual Refinements -- Implementation Plan

## 1. Implementation Status

Follow-up to the embedded-output-plot re-design (parent plan:
`2026-09-17_embedded-output-plot_plan.md`). Two additions, both against the
settled interaction layer: **separate the two embedded plot halves** (corner
brackets) and **indicate the output mode** (title, mode label, and — the
substance — implementing the TIMEBASE and CYCLES settings so the axis labels
have real values to show). The design is settled by a third-party review; this
plan sequences it and records the decisions.

**Phases:**

1. **Mode indication** — `ScopeModeName` + title substitution + mode label. (depends on nothing)
2. **Output settings** — TIMEBASE and CYCLES end-to-end, plus the axis labels. (depends on nothing from phase 1; kept separate for reviewable hashes)
3. **Plot brackets** — corner brackets on each plot region. (depends on nothing)
4. **Documentation** — arch-design reconciliation. (depends on phase 2)

| # | Task | Status |
|---|---|---|
| 1.1 | `nostromo/panel.cc` — add file-local `ScopeModeName(ScopeMode)` → "SCOPE"/"CYCLE"/"SPECTRUM" | Pending |
| 1.2 | `nostromo/panel.cc` — `DrawEditChrome`: substitute `ScopeModeName(nav.scope_mode)` for `page.long_name` when `nav.mode == kOutView` | Pending |
| 1.3 | `nostromo/panel.cc` — mode-name label at the output region's top-left (`kSecondaryFont`, `kDim`, `x+6, y+4`) | Pending |
| 1.4 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build`; re-bake hashes (B1 changes every plot page) | Pending |
| 2.1 | `nostromo/interaction.h` — `struct OutputSettings { std::uint32_t timebase_ms; std::uint8_t cycles; }` + `DefaultOut()` (341, 3); `OutputSettings out` member beside `feel` | Pending |
| 2.2 | `nostromo/interaction.cc` — `TurnOut`/`RevertOut` (1-2-5 timebase steps, 1..8 cycles); dispatch from the `kViewCtl` case | Pending |
| 2.3 | `nostromo/pages.cc` — reclassify SCALE/TRIGGER/HOLD/ALIGN/RANGE/AVERAGE/WINDOW as `kPending` (TIMEBASE/CYCLES stay `kViewCtl`) | Pending |
| 2.4 | `nostromo/panel.cc` — `DrawScopePlot` window from `timebase_ms` (stride = `window_samples / w`, clamped to `[w/f_s, kCapacity/f_s]`); `DrawCyclePlot` `n_eff = min(cycles, kCycleBufSize/period)` with a dim out-of-range line at `n_eff == 0` | Pending |
| 2.5 | `nostromo/panel.cc` — per-mode graticule rows + axis labels (vertical on lines; horizontal `0ms`/window, `0`/`n_eff`, `20Hz`/`20kHz`) | Pending |
| 2.6 | `tests/test_bindings.cc` + `tests/test_interaction.cc` — kPending resolution cases; `TurnOut`/`RevertOut` dispatch cases | Pending |
| 2.7 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build`; re-bake hashes; smoke-render scope/cycle/spectrum | Pending |
| 3.1 | `nostromo/geom.h` — `kPlotBracketLeg = 12`, `kPlotBracketTh = 2`; `nostromo/panel.cc` — generalize `Cursor()` into a leg-parameterized bracket helper; call from each plot hook at `kDim` | Pending |
| 3.2 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build`; re-bake hashes | Pending |
| 4.1 | `../arch-designs/nostromo-interaction_arch-design.md` — add `OutputSettings` beside `FeelProfile` (§7.10); correct §7.4/§7.6: TIMEBASE/CYCLES are display state, the other seven are `kPending` (supersedes the "kPending until ParamId grows" note) | Pending |
| 4.2 | Verify: `git status` clean | Pending |

## 2. Architecture

### 2.1. Directory Layout

| File | Change |
|---|---|
| `nostromo/interaction.h` | Add `OutputSettings`, `DefaultOut()`, `Interaction::out` |
| `nostromo/interaction.cc` | Add `TurnOut`/`RevertOut`; dispatch |
| `nostromo/pages.cc` | kPending reclassification |
| `nostromo/panel.cc` | `ScopeModeName`, title, mode label, window, clamp, graticule, labels, bracket helper |
| `nostromo/geom.h` | `kPlotBracketLeg`, `kPlotBracketTh` |
| `tests/test_bindings.cc` | kPending resolution cases |
| `tests/test_interaction.cc` | `TurnOut`/`RevertOut` cases |
| `tests/test_panel.cc`, `tests/test_panel_pages.cc` | hash re-bakes |
| `docs/workflow/arch-designs/nostromo-interaction_arch-design.md` | OutputSettings + kPending correction |

No new files; no `CMakeLists.txt` change; no engine change.

### 2.2. Dependency Graph

`OutputSettings` lives on `Interaction` (the single ViewCtl backing store, beside
`FeelProfile`): the interaction layer edits it, the panel reads it via
`p->interaction->out.*`. No inter-package edges change.

## 3. Interface Changes

### `nostromo/interaction.h`

```cpp
/// Output-view display settings, edited from the kOutView columns. Display
/// state, not engine params — the two settings that change what the plot
/// shows, not what it sounds like.
struct OutputSettings {
  std::uint32_t timebase_ms;  ///< total scope window (size-invariant)
  std::uint8_t  cycles;       ///< single-cycle count
};

OutputSettings DefaultOut();  ///< {341, 3}

// Interaction:
  OutputSettings out = DefaultOut();  ///< beside feel
```

### `nostromo/panel.cc`

```cpp
const char *ScopeModeName(ScopeMode m);  // "SCOPE"/"CYCLE"/"SPECTRUM"
```

`Cursor()` is generalized to a leg-parameterized bracket helper (leg 7 for the
cursor, 12 for plot brackets); the cursor keeps its call sites and 7 px legs.

## 4. Solution Breakdown

### 4.1 Mode indication (`ScopeModeName`, title, label)

`ScopeModeName` maps `ScopeMode → "SCOPE"/"CYCLE"/"SPECTRUM"`. The title uses it
only in `kOutView` (otherwise the page's `long_name`, unchanged). The label is a
single `kSecondaryFont` text at the output region's `(x+6, y+4)` in `kDim`.

- **Edge cases**: `kOff` never coexists with `kOutView` (entry forces `kScope`), so `ScopeModeName` is never called with `kOff`; still return "SCOPE" defensively.
- **Done**: task 1.4.

### 4.2 OutputSettings storage + edit

`OutputSettings` holds the two settings; `DefaultOut()` = {341, 3}. `TurnOut`
steps `timebase_ms` through {20, 50, 100, 200, 341} and `cycles` through 1..8;
`RevertOut` restores the default. Dispatched from `BindKind::kViewCtl` for the
two real settings (the seven `kPending` columns no longer reach it).

- **Edge cases**: long-press reverts; the 1-2-5 sequence clamps at the ends.
- **Done**: task 2.7, plus test_interaction cases (2.6).

### 4.3 kPending reclassification

Seven output settings become `ColumnKind::kPending` (dim header, empty value,
inert) — the "declared-not-yet-built" treatment already used across the page
table. TIMEBASE and CYCLES stay `kViewCtl`.

- **Edge cases**: `ResolveBinding` now returns `kPending` for those columns; the
  `kOutView` encoder test in test_bindings updates accordingly.
- **Done**: task 2.7.

### 4.4 Scope window + cycle clamp

`DrawScopePlot` computes `window_samples = timebase_ms * kSampleRate / 1000`,
clamped to `[w, kCapacity]`, and `stride = window_samples / w` (replaces the
full-ring `kCapacity / w`). `DrawCyclePlot` computes
`n_eff = min(cycles, kCycleBufSize / period)`; `n_eff >= 1` draws that many
cycles, `n_eff == 0` (f < ~11.7 Hz) draws a dim out-of-range line instead of a
blank region. `cycle_buf` stays 4096.

- **Edge cases**: the clamp floor `w/f_s` moves between embedded and full band,
  so it is computed per draw from the current width. The high-frequency blank
  (period < 8) is unchanged.
- **Done**: task 2.7, plus the smoke render.

### 4.5 Graticule + axis labels

Scope/cycle get 4 rows (lines at ±0.5 and 0); spectrum gets 6 rows (15 dB
steps, labelled 0/−30/−60/−90). Vertical labels sit on the lines; horizontal
labels are `0ms` / window, `0` / `n_eff CYCLES`, `20Hz` / `20kHz`.

- **Edge cases**: `n_eff` is shown, not the stored `cycles`; at `n_eff == 0` the
  count label is the out-of-range line.
- **Done**: task 2.7.

### 4.6 Plot brackets

`kPlotBracketLeg = 12`, `kPlotBracketTh = 2`; each plot hook draws its own
bracket at `kDim` inside its rect. Full-band and embedded both come from the
same hook + rect.

- **Edge cases**: no graticule border exists to reconcile against; the curve
  amplitude (±0.42·h) keeps it clear of the corner pixels.
- **Done**: task 3.2.

## 5. Design Decisions

1. **TIMEBASE is a total window in ms, not ms/div.** The graticule's division
   count is width-derived, so ms/div would show a different time span at each
   size — reintroducing finding 1 through the settings layer. A total window is
   size-invariant and needs no division coupling.
2. **CYCLES clamps rather than guards.** `n_eff = min(n, ⌊kCycleBufSize/period⌋)`
   keeps CYCLE working across the musical range and makes the clamp visible in
   the label; `n_eff == 0` draws a dim out-of-range line instead of a blank
   region. `cycle_buf` stays 4096 — the buffer only binds below ~12 Hz, which
   isn't musical content, and SCOPE already owns the long window.
3. **`OutputSettings` lives on `Interaction`, beside `FeelProfile`.** ViewCtl
   already has one backing store there; a second location would give "where does
   a view control live?" two answers. The settings are display state, not engine
   params — superseding the arch-design's "kPending until ParamId grows" note.
4. **Seven settings render `kPending`, not `kViewCtl`.** A header with no value
   and no effect looks functional; `kPending` reads as declared-not-yet-built,
   and "no `kPending` left on the OUT columns" becomes the follow-up's definition
   of done.
5. **Labels go on the graticule lines, and the row count is per-mode.** A
   graticule exists to be read against; scope/cycle get 4 rows (±0.5, 0) and
   spectrum 6 rows (15 dB steps) so the lines land on round, labellable values.
6. **Brackets generalize `Cursor()`.** The corner bracket already exists as
   `Cursor()` (filter pad, envelope handles); A1 is a leg-length parameter (7 →
   12), not a new component.

## 6. Success Criteria

- [ ] `ScopeModeName` returns "SCOPE"/"CYCLE"/"SPECTRUM"; kOutView title is "OUT <mode>" (1.2).
- [ ] Mode label renders in the output region on every plot page (1.3).
- [ ] `OutputSettings` on `Interaction` with defaults {341, 3}; panel reads `p->interaction->out.*` (2.1).
- [ ] `TurnOut`/`RevertOut` edit/revert the two settings (2.2, 2.6).
- [ ] Seven OUT columns resolve `kPending`; TIMEBASE/CYCLES resolve `kViewCtl` (2.3, 2.6).
- [ ] Scope window honours `timebase_ms`, clamped to `[w/f_s, kCapacity/f_s]` (2.4).
- [ ] Cycle shows `n_eff` cycles, label shows `n_eff`; `n_eff == 0` draws the out-of-range line (2.4, 2.5).
- [ ] Per-mode graticule rows + axis labels render (2.5).
- [ ] Each plot region shows corner brackets at `kDim` (3.1).
- [ ] Full `ctest` green — 20 tests (1.4, 2.7, 3.2).
- [ ] Arch-design records `OutputSettings` and the kPending correction (4.1).

## 7. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/nostromo-interaction_arch-design.md` | Yes — §7.4/§7.6 say the output settings render `kPending` until `ParamId` grows; §7.10 defines `FeelProfile` with no `OutputSettings`. | Task 4.1: add `OutputSettings`; correct the kPending framing (TIMEBASE/CYCLES are display state, seven stay `kPending`). |
| `../plans/2026-09-17_embedded-output-plot_plan.md` | No — parent plan; its kOutView tables were the first cut, this plan refines them. | None. |
| `../briefs/2026-09-16_embedded-output-plot_brief.md` | No — framing unchanged. | None. |

## 8. Cleanup

None — no diagnostic instrumentation, logs, or temporary flags.
