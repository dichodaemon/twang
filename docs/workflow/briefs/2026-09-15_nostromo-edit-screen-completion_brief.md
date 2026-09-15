---
title: Nostromo Edit-Screen Completion
date: 2026-09-15
author: Dizan Vasquez
---

# Nostromo Edit-Screen Completion

## 1. Objective

Frame the work that the nostromo interaction-layer plan deliberately deferred — the full edit-screen chrome, the route-field/view-control dispatch, and the host feedback path — so a decision can be made about whether it warrants a follow-up plan, a spec, or direct implementation.

## 2. Scope

| In scope | Out of scope |
|---|---|
| Full column chrome: modulation band, summary lines, SENDS band (the split well already landed) | The interaction-layer core — SurfaceProfile, GestureRecognizer, NavState, PageTable/BindingResolver, Dispatcher — already implemented |
| Mode overlays: kModArm / kModView / focus-widening | Patch storage and the patch browser's backing data (a synth-routing / engine non-goal) |
| Route-field + view-control dispatch (the Dispatcher's `kRouteField`/`kViewCtl` "later" cases) | The modulation semantics themselves (ModRoute, CombinationClass — owned by synth-routing, already specified) |
| Item-axis list rendering (slots / routes) | The §13 open tunables (acceleration, part hues, `E`) |
| View strip (OUT select) | The spike renderer primitives (emphasis ladder, focus) — already exist |
| Host feedback: LED rings, mode LEDs, the stale `Feedback()` | |
| Route-table-full alert; "zero = present but silent" well state | |
| Untested invariants (12/13, §11 items) | |

## 3. Sources

1. `docs/workflow/arch-designs/nostromo-interaction_arch-design.md` — the full column spec (§5, §7.6, §8, §9, §11)
2. `docs/workflow/plans/2026-09-14_nostromo-interaction_plan.md` — the plan that scoped phase 6 to "headers/values" and deferred the rest ("a plot-geometry migration, not chrome porting", §6)
3. `docs/workflow/arch-designs/synth-routing_arch-design.md` — ModRoute and CombinationClass, the data behind the modulation band and summary lines
4. `docs/workflow/arch-designs/spike_arch-design.md` and `docs/random/panel-ui-design-state.md` — emphasis ladder, focus-vs-state, alert treatment, patch browser
5. Code: `nostromo/panel.cc` (DrawColumns/DrawWell), `nostromo/interaction.cc` (Dispatcher), `host/midi_io.cc` (Feedback)
6. Beads: `twang-0jvx` (MOD-arm overlay, already filed)

## 4. Approach

Organize the gaps into three epics by owner, ordered by value. The P1 items are correctness holes and the user's active focus; P2-and-below is polish that can trail.

### Epic 1 — Edit-screen chrome (`nostromo/panel.cc`)

| Gap | Value / Pri |
|---|---|
| Modulation band (`[lo,hi]` folded extent) | High · P1 |
| Summary lines (inbound `<-ENV2 +48` tags) | High · P1 |
| MOD-arm overlay (route amounts, `--` on non-modulatable) | Med · P1 — already `twang-0jvx` |
| SENDS band (outbound routes) | Med · P2 |
| MOD-view overlay (route lists + focus) | Med · P2 |
| Item-axis lists (slots / routes) | Med · P2 |
| View strip (OUT select) | Low · P3 |
| Focus / widening | Low · P3 |
| "Zero = present but silent" well state | Low · P3 |
| Route-table-full alert | Low · P3 |
| Patch browser | Low · P4 — blocked on patch storage |

### Epic 2 — Dispatch completion (`nostromo/interaction.cc`)

| Gap | Value / Pri |
|---|---|
| Route-field dispatch (MOD source/dest/amount) | High · P1 — the MOD page is inert today |
| View-control dispatch (CONF feel editing) | Med · P2 |
| View-control dispatch (PATCH) | Low · P3 — blocked on patch storage |
| Invariant 12 test (feel never changes shape) | Low · P3 |
| Invariant 13 test (bounded work/event) | Low · P3 |
| §11 OUT round-trip test | Low · P3 |

### Epic 3 — Host feedback (`host/midi_io.cc`)

| Gap | Value / Pri |
|---|---|
| Stale `Feedback()` (still old CC 1–4/10–11 map) | High · P1 — actively wrong now |
| LED rings (drive from resolved binding) | Med · P2 |
| Mode LEDs (kModView/kPerform latched → lit) | Med · P2 |
| §11 surplus-encoder "no events" test | Low · P3 |

The clustering is the finding: eight of ten gaps collapse to `nostromo/panel.cc`, so this is one cohesive "chrome completion" body of work, not ten independent loose ends.

## 5. Constraints

- The plan's §6 decision ("a plot-geometry migration, not chrome porting") is the reason these are unscheduled; the deferral was never handed to an owner, which is why they read as a miss rather than a tracked backlog.
- The modulation band and summary lines need the folded modulation extent: they read `EngineGetRoute` and fold per `CombinationClass` (already specified in synth-routing), so no new engine entry point beyond `EngineGetRoute` is required.
- The patch browser is blocked on patch storage; the rest of the chrome should be scheduled without it.
- `Feedback()` is stale, not just missing: it still drives the old `kXtouchCompact` CC 1–4/10–11 map, contradicting the confirmed surface map.

## 6. Open Questions

- Does Epic 1 warrant a spec, or is a follow-up plan enough once the P1 items land? (Answer: decide after the P1 items.)
- Where should the modulation-extent fold live — a small `nostromo` accessor, or inline in `panel.cc`? (Answer: depends on whether the summary lines consume the same data.)
- Which P2/P3 items are worth doing before the next hardware iteration? (Answer: the user.)
