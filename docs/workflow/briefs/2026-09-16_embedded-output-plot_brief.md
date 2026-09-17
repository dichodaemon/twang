---
title: Embedded Output Plot
date: 2026-09-16
author: Dizan Vasquez
---

# Embedded Output Plot

## 1. Objective

Frame the re-design that makes the output view (scope, single-cycle, spectrum) visible while editing: an embedded plot on every plot-bearing page, plus a full-screen configuration mode. The output view stops being a navigation subject entirely — the three OUT subjects are retired, not collapsed — and configuration moves into a mode. This brief records the settled decisions and the documents they touch, so the work can be executed against the arch-design without a preamble conversation.

## 2. Scope

| In scope | Out of scope |
|---|---|
| Embedded output plot on plot pages (scope/cycle/spectrum, `scope_mode` field) | Engine / output-stage DSP (the tap point is a note, not new signal code) |
| Latched full-screen `kOutView` mode (OUT hold) with per-mode settings columns | Code implementation — this brief frames the arch-design reconciliation |
| Retire the three OUT subjects (`kCount` 20 → 17) | Patch storage, the 32-slot scroll window, mod-view per-route cursor |
| Three per-mode column tables (no SOURCE) | The spike renderer primitives |
| Button applicability (OUT on plot pages, MOD on modulatable pages) | — |
| Reconcile `nostromo-interaction_arch-design.md`; one tap-point note in `output-stage_arch-design.md` | — |

## 3. Sources

1. `docs/workflow/arch-designs/nostromo-interaction_arch-design.md` — the subject/page/geometry/contract surface (§5, §6, §7.1–§7.9, §8, §9, §11, §12).
2. `docs/workflow/arch-designs/output-stage_arch-design.md` — the audio chain where the output view taps (§5).
3. `docs/random/engine-recommendations.md` — §5.5/§5.7/§6.2 (column-update traces, per-slot invalidation, trace cost ≈ 10⁴ writes/s), §7.2 (measure one plot draw).
4. Code: `nostromo/pages.cc` (`kColsOutScope/Cycle/Spec`, `g_pages`), `nostromo/panel.cc` (`DrawOutPlot`, `TraceState`, `scope_dirty`), `nostromo/interaction.cc` (OUT dispatch, `NavState`), `nostromo/geom.h`.

## 4. Approach

### 4.1 The problem

You cannot see what a change does while making it. The scope, cycle and spectrum views lived on their own pages, so watching the output meant leaving the page being edited. The fix splits each plot page so the output view is always visible, and adds a full-screen mode for configuring it.

### 4.2 Settled decisions

- **The output view is not a subject.** The three OUT subjects (`kOutScope`/`kOutCycle`/`kOutSpec`) are retired outright — the pane loses OUT, `kCount` goes 20 → 17. The view is exposed two ways instead:

  | Surface | How reached | What it is |
  |---|---|---|
  | Embedded plot (440 × 404) | `scope_mode ≠ off` on any plot page | Read-only glance at the current part's output; no columns. |
  | Full-screen `kOutView` mode | OUT **hold** (latched) | Output at 900 × 404, column band shows the settings — the configuration home. |

- **Scope-mode switching is the OUT button.** Tap (short press) cycles `scope_mode` off → scope → cycle → spectrum → off, globally. Hold (long press) toggles `kOutView`. One control, two gestures, mode visible in the plot itself — no LED, no hidden state.
- **No SOURCE, no master.** The output view always monitors the current part (`nav.part`); the tap point is post-amp (post-VCA, before the bus sum). There is no master/part selector anywhere.
- **Per-mode columns, not a superset.** `kOutView`'s settings are three distinct `ColumnSpec` tables, selected by `scope_mode` (`kOutColumns[4]`, with `off` sharing scope's table):

  | col | scope | cycle | spectrum |
  |---|---|---|---|
  | 1 | TIMEBASE | CYCLES | RANGE |
  | 2 | SCALE | SCALE | SCALE |
  | 3 | TRIGGER | ALIGN | AVERAGE |
  | 4 | HOLD | HOLD | WINDOW |

- **Button applicability.** A control acts only where its effect is defined: OUT resolves to `kNone` on pages with no plot, MOD on pages with no modulatable column (which is what keeps arming inert on the MOD page itself).
- **Power-on** is `kFilt` with `scope_mode = kScope` — filter curve plus embedded scope, the "shows the output" state without an output subject.
- **Full-screen is latched**, and its indicator is self-evident (the full-screen output + settings columns), so it needs no LED.
- **Split geometry**: vertical, two ~440 × 404 halves; page plot left (half 0), output view right (half 1).

### 4.3 Two ideas withdrawn

- Using the pane's SC/CY/SP cells as the mode selector — the OUT button's cycle subsumes it.
- The claim that this introduces timer-driven repaint — `scope_dirty` is already set by the audio path and drained through `MarkDirty`; invariant 1 is untouched.

### 4.4 Arch-design sections touched

`nostromo-interaction_arch-design.md`: §5 (design decisions — reverse "three subjects" to "not a subject", with the change-of-circumstance rationale), §6 (power-on, `kOutView` transitions), §7.1 (geometry split, pane rows), §7.2 (`kOut` comment), §7.3 (`SubjectId` → 17, `ViewMode` + `kOutView`, `ScopeMode`, drop `NavPos`/`prev`), §7.4 (`ViewCtl` + 9 output settings, three tables + `kOutColumns`, drop the superset encoding), §7.6 (page table, keystone paragraph), §7.7 (applicability), §8 (OUT/MOD contracts, resolution table), §9 (invariants 4/5/14/15), §11 (acceptance), §12 (code pointer), §13 (empty of this work).

`output-stage_arch-design.md`: one note in §5 recording the post-amp tap point.

## 5. Constraints

- **Hardware is delayed — measurement is validation, not a design gate.** The scope trace is a column-update polyline (`engine-recommendations.md` §5.5/§6.2: a few hundred lit pixels per frame, ~10⁴ writes/s at 30 fps), and the embedded half is narrower than the old 900-wide scope, so it is cheaper per frame, not more expensive. §7.2 (does one plot draw fit the frame budget) is run once on silicon and tunes the half-width constant, which is a single `geom.h` value, not a redesign.
- **Invariant 1 is untouched** — no timer; the repaint model stays "invalidate per slot, drain through `MarkDirty`".
- **The 16-slot matrix is unaffected** — this re-design does not touch `kModSlots`.
