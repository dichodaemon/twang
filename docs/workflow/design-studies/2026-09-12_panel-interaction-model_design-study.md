---
title: Panel Interaction Model
status: review
date: 2026-09-12
author: Dizan Vasquez
---

# Panel Interaction Model

## 1. Problem Statement

Noisetromo's panel has a settled visual system and three screens rendered at 1:1, but no
interaction model. `panel-ui-design-state.md` §10 states this directly: every interaction
detail in the existing screens was decided locally, to make one screen look complete, with
no principle connecting it to the next. The clearest symptom is the encoder legend — a row
of text on every screen saying what the four encoders currently do, which exists because
nothing else tells you.

The absence is not a gap to be filled later. Three of its consequences are already
load-bearing: `PUSH` means three different things on three screens with no rule behind it,
nothing drives the nav bar, and the control surface itself is unspecified — how many
encoders exist, where they sit, whether they push, whether there are dedicated buttons is
recorded nowhere in the repo. Spatial correspondence cannot be designed without that, and
several of the layouts in §9 will move once it is.

This study resolves the interaction model and the control surface **together**, because
each constrains the other. Its output is a set of decisions that are simultaneously
software (what a screen does with input) and hardware (how many holes are drilled in the
panel, and at what pitch). The hardware half is irreversible in a way the software half is
not, which sets the standard of confidence required.

The principles the study cuts with are stated up front in §2, each with the concrete design
it rules out and the dimensions it decided.

**In scope:** parameter addressing; the navigation model; the control complement (encoders,
buttons, and their allocation); screen-to-control correspondence; the gesture vocabulary;
overflow policy; modulation route creation, editing and overview; patch browser navigation;
part switching semantics; and the panel geometry that follows from all of the above.

**Out of scope, by decision:**

1. **Patch storage** — flat pool versus partitioned, fixed count versus flash-bounded, what
   an import does. This is a question about function, not interaction. The browser
   interaction resolved in §4.10 is invariant under every storage model considered, which is
   itself an argument for it; recording storage as an open question would wrongly imply the
   interaction model waits on it.
2. **Performance mode** — deferred by decision (§4.12). Its content depends on which
   parameters prove to be reached for in practice, which requires using the default mode
   first.
3. **The visual system** — palette, emphasis ladder, component vocabulary, primitive costs.
   Governed by `panel-ui-design-state.md` §1–§6 and treated here as constraint, not
   question.
4. **Touch input.** The panel ships with an optional touch layer and `PollTouch` exists, but
   the model below is complete without it. Touch as a secondary modality is additive and is
   not designed here.
5. **MIDI-CC mapping of panel controls** — the addressing in §4.1 leaves room for it.

## 2. Design Principles

These are normative and were used to cut. Two tests were applied to each: it must be
**falsifiable** — nameable as a concrete design it rules out — and it must have been
**decisive somewhere in this study**. A principle that decided nothing is decoration, and
will later be cited to justify whatever is wanted. Measured facts are not principles; they
are constraints and live in §3.

`stated` marks a principle carried in from the brief. `derived` marks one that emerged during
the analysis; both derived principles turned out to cut more than most of the stated set, and
they are labelled so a reader can tell which came from where.

| | Principle | Origin | Rules out | Decisive in |
|---|---|---|---|---|
| **P1** | **Function and constraint dictate form.** The visual and physical vocabulary follows from flash, compute and access-pattern limits. Consistency is expected to emerge from the constraints rather than be imposed over them. | stated | Node-and-edge graph (diagonals); rotated pane labels (bitmap atlas); vertical rules as ornament | 4.4, 4.5, 4.7, 4.9 |
| **P2** | **The instrument explains itself.** Any mapping the user must memorise is a defect, not a documentation problem. | stated | The encoder legend row | 4.3, 4.5, 4.6 |
| **P3** | **No invisible mode.** Any latched state carries a physical indicator or a persistent on-screen presence. Momentary is preferred to latched; latched-and-lit to latched-and-hidden. | derived | The depth encoder; per-part cursor memory; a general SHIFT | 4.1, 4.2, 4.6, 4.11, 4.12 |
| **P4** | **The panel is one object; position carries meaning.** Screen, encoders and buttons are a single spatial system, not three components that happen to be adjacent. | stated | Navigation encoders in the parameter row; any screen exempt from 1:1 | 4.2, 4.3, 4.4 |
| **P5** | **Positional constancy.** A given thing lives in a given place. Layout does not reorder itself under the user. | derived | Filtering the modulation view; a fixed cursor with scrolling content | 4.7, 4.9, 4.10, 4.11 |
| **P6** | **The panel builds intuition about the engine.** Structure visible in the interface mirrors structure in the signal path. | stated | A menu tree unrelated to signal flow; shrinking the plots to gain density | 4.1, 4.8, 4.9 |
| **P7** | **Shifts and double functions are rare and strategic.** Where one is unavoidable it is momentary, self-clearing, and carries a meaning of its own. | stated | A general-purpose SHIFT; a held modifier for performance mode | 4.6, 4.8, 4.12 |

**Deliberately absent: "fun and immediate."** It was in the brief and it is not falsifiable —
every dimension here could claim to serve it, which by the test above makes it decoration.
It is better understood as an *emergent* property of the others: it is fun to see resonance
move in the spectrum (P6), and immediate to match a control to a concept without reading a
legend (P2, P4). Its testable residue is P7 and criterion C1.

**Redundancy is tolerated.** P3 is arguably a consequence of P2, and P5 of P4. They are kept
separate because each cuts independently — P3 alone decides four dimensions — and a principle
that has to be derived from another before it can be applied will not be applied.

---

## 3. Current State

### 3.1. What exists

| Artifact | State |
|---|---|
| `spike/descriptor.{h,cc}` | The §5.2 descriptor interpreter (RECT/HLINE/VLINE/TEXT/CALL/DYN/END). |
| `nostromo/screens.{h,cc}` | The four screens as static-chrome descriptors + the shared DYN hooks. |
| `nostromo/panel.{h,cc}` | Signal-flow screen, live and stateful; its chrome is descriptor-driven. |
| `tools/mockup_screens.cc` | All four screens: descriptor chrome + canned DYN content, rendered to PNG. |
| `assets/fonts/ter-u{20,14}n.bdf` | Two Terminus atlases, 10×20 and 8×14. |

The chrome/DYN split is already in force (§5.1 of `engine-recommendations.md`): the
descriptor says *where* the static chrome sits, and the DYN slots are C hooks that draw
*what* is live. The interaction model this study settles governs those DYN regions — what a
screen does with input, which columns are live, and how they redraw — not the static chrome.
The two are compatible by construction: a new layout is authored as a descriptor, a new
behaviour as a DYN hook.

Layout constants in force:

```
frame        1024 x 600
title bar    x16 y16, 992 x 26
modules      y84, h340, w242, at x = {16, 266, 516, 766}
plot area    +6,+58 within a module, 230 x 232
readout      y = kModY + 306, two lines at 22 px pitch
keyboard     y440
nav bar      y512
```

### 3.2. Engine scope this must address

From the routing study, treated here as fixed input:

- 4 parts, ~4 oscillators per voice, 3 envelopes and 3 LFOs per part
- Modulation slots per part: **32 is a floor, not a ceiling** — modulation is a headline
  feature and the count is bounded by compute, not by design
- Source and destination sets are expected to grow; both are software axes

A first-order parameter count, for sizing rather than specification: roughly 130 per part
across oscillators, filter, envelopes, LFOs, amp, modulation slots and part settings, so
**order 500 addressable parameters** plus globals.

### 3.3. Interaction facts already established

These are constraints, not open questions:

- **Focus and state are distinct visuals** (emphasis ladder, §4 of the panel doc). This is
  already an interaction fact — it exists because an endless encoder moves a cursor over
  items that are not the current selection.
- **Input never draws.** Input changes a parameter; the parameter layer sets a dirty flag;
  the region redraws.
- **Access pattern is a design input.** Horizontal runs are one burst; vertical rules are
  one write per row; diagonals are worse. Any affordance needing long vertical rules or
  per-frame full redraws is expensive.

### 3.4. Display, measured

The target panel is an ER-TFT070-6: 7-inch, 1024×600, active area **154.2144 × 85.92 mm**,
module outline 164.5 mm wide (datasheet Rev1.0, outline drawing).

Two consequences follow immediately.

**Pixel pitch is anisotropic.**

$$p_x = \frac{154.2144}{1024} = 0.15060\ \text{mm}, \qquad p_y = \frac{85.92}{600} = 0.14320\ \text{mm}$$

$$p_x / p_y = 1.0517$$

Pixels are 5.2% wider than tall. The existing 230×232 plot renders as 34.6 × 33.2 mm, and
anything drawn as geometrically square appears visibly wide. This affects the graticule, the
envelope plot's time-versus-level proportions, and any circle. It is cheap to correct in the
layout constants and expensive to discover after the panel is cut.

**Panel width bounds the control count.** Under 1:1 correspondence (§4.3), encoder pitch
equals screen column pitch, so the number of parameter controls is fixed by the display.
This is quantified in §4.4.

## 4. Dimensions

### 4.1. Parameter Addressing

*Governed by: P3, P6.*

**Definition.** How a parameter is named and reached. This is the root dimension: it
determines what the navigation controls do, and therefore how many are needed.

#### Option 1: Hierarchical path with a tree cursor

- *Properties.* Parameters form a tree, `part → class → instance → parameter`. Two encoders:
  one selects which level of the path is active, the other cycles siblings at that level.
- *Pros.* Two controls cover arbitrary depth. Scales to any hierarchy without new hardware.
- *Cons.* The active level is latched state carried between screens and invisible unless
  chrome is added to show it. Depth extent is ragged — `part → OSC → 1` is three levels,
  `part → MOD → slot → field` is four — so the level selector's scale changes underneath
  the user. Requires a breadcrumb to be legible at all.

#### Option 2: Product addressing with independent selectors

- *Properties.* A parameter is a tuple $a = (p, m, i, k)$ over part, module class, instance
  and parameter index. Each axis gets its own control; no axis is reached through another.
- *Pros.* No latched mode. Movement along any axis costs the same regardless of which axis.
- *Cons.* Costs one control per axis. Four axes against a control budget already constrained
  by panel width (§4.4).

#### Option 3: Flattened subject list with a parameter row

- *Properties.* Class and instance collapse into one axis — a flat list of *subjects*
  (`OSC1, OSC2, OSC3, OSC4, MIX, FILT, AMP, ENV1–3, LFO1–3, MOD, FX`, plus part-level and
  global entries). Part is a separate selector. Parameters of the selected subject occupy a
  row of columns.
- *Properties.* Depth is 2: subject, then parameter. Cardinality of the subject axis is ~16
  for the per-part path plus globals.
- *Pros.* One flat encoder for the subject axis, whose position is meaningful and displayable
  absolutely. No latched navigation state. The subject list is short enough to display in
  full, so the navigator is also its own breadcrumb.
- *Cons.* Traversal cost is linear in the subject list rather than logarithmic. Assumes the
  subject list stays short enough to display entirely.

**Observations.**

The tree in Option 1 is not a tree. Modulation routes are cross-edges from a source instance
to a destination parameter, so the structure is a graph, and it fails to be a tree at exactly
the point navigation is hardest.

Traversal cost, subject axis, with wrap-around over $n$ entries: worst case
$\lceil n/2 \rceil$. At $n = 16$ that is 8 detents, on a control whose meaning never changes.
Option 1 reaches the same target in roughly 4–6 actions but leaves latched state behind.
Detents get cheaper with acceleration and wrap; invisible latched state does not get cheaper
with anything.

Option 3 subsumes Option 2's benefit at lower control cost, because the instance axis is
absorbed into the subject list rather than given its own encoder.

**Conclusion.** Option 3. Subject list on one encoder, part on dedicated buttons (§4.2),
parameters on the column row. Depth 2, no latched navigation state.

### 4.2. Navigation Control Allocation

*Governed by: P3, P4.*

**Definition.** Which physical controls drive navigation, and where they sit.

#### Option 1: Two navigation encoders in the parameter row

- *Properties.* Navigation encoders occupy two of the positions under the screen.
- *Pros.* One uniform row of controls; simplest panel.
- *Cons.* Breaks the rule that a control under the screen edits the column above it, on two
  of the positions. Consumes two parameter columns.

#### Option 2: Navigation cluster left of the screen

- *Properties.* NAV1, NAV2 and four part buttons form a cluster to the left of the display,
  adjacent to the on-screen navigation pane they drive. The row under the screen is
  parameters only.
- *Pros.* The rule "under the screen is a parameter, left of the screen is navigation" holds
  without exception. Applies spatial correspondence to navigation as well as parameters.
  Frees the whole column row.
- *Cons.* Panel is wider than the display. Two distinct control regions rather than one.

#### Option 3: Split — one navigation encoder in the row, one beside it

- *Properties.* NAV1 under the pane, NAV2 to the side.
- *Pros.* NAV1 sits directly under the pane it drives.
- *Cons.* The two navigation encoders are a pair and this makes them asymmetric. The
  under-screen rule becomes three-quarters true, which is worse than either extreme.

**Observations.**

Part is a persistent, latching, global mode. It must be readable at a glance and settable in
one action, which is a button's job rather than an encoder's. Four latching buttons with LEDs
also give the mode a physical indicator, which is the mitigation for latched state used
throughout this study.

NAV2's meaning varies by page — the item cursor on a list page, idle on most class pages.
This is acceptable, and distinct from the latched depth level rejected in §4.1, because the
page is on screen while its meaning varies.

**Conclusion.** Option 2. Left cluster: NAV1 (subject pane), NAV2 (item within page), PART×4
(latching, LED). Parameter row is parameters only.

### 4.3. Screen–Control Correspondence

*Governed by: P2, P4.*

**Definition.** How the user learns which control changes which parameter.

#### Option 1: Encoder legend row

- *Properties.* A text row names the current function of each encoder. Present on every
  screen today.
- *Pros.* Works for any layout; no geometric constraint.
- *Cons.* Costs a row on every screen. The mapping must be read rather than seen. Existing
  legends were written to fill the row — plausible, not derived, with nothing traded off to
  reach them.

#### Option 2: 1:1 spatial correspondence

- *Properties.* Encoder $n$ sits directly beneath column $n$. The column header names the
  parameter and therefore labels the encoder.
- *Pros.* Legend row is eliminated on every screen. The mapping is seen, not memorised. One
  rule covers all screens.
- *Cons.* Column pitch becomes a mechanical constant. Every screen must be authored to the
  same column count. Screens whose content is not $E$ parameters need to be reshaped
  (§4.7, §4.9, §4.10).

**Observations.**

This dimension is where the study's hardware and software halves meet. Adopting Option 2
converts "how many encoders" from a preference into a constraint binding both the display
choice and every future screen.

**Conclusion.** Option 2, universally, with no exceptions permitted. A rule with one
exception is a rule users stop trusting, and the exception would fall on the densest screens
— exactly where the affordance matters most. Screens that do not naturally decompose into
$E$ parameters are reshaped until they do; §4.9 and §4.10 do this for modulation and patches.

### 4.4. Column Count and Panel Geometry

*Governed by: P1, P4.*

**Definition.** The number of parameter columns $E$, and the mechanical pitch that follows.

Given a navigation pane of width $P$ px and 1:1 correspondence, parameter pitch is

$$\text{pitch}_{\text{mm}} = \frac{1024 - P}{E} \cdot p_x, \qquad p_x = 0.15060\ \text{mm}$$

Ergonomic floor: a knob of diameter $d$ needs $d + 5$ mm of pitch for finger clearance. A
20 mm knob needs 25 mm; an LED ring assembly is ~30 mm outer diameter.

| $E$ | pitch, $P=170$ | pitch, $P=92$ | 20 mm knob | LED ring |
|---:|---:|---:|:--:|:--:|
| 4 | 32.1 mm | 35.1 mm | yes | yes |
| 5 | 25.7 mm | 28.1 mm | yes | no |
| 6 | 21.4 mm | 23.4 mm | 16 mm knob only | no |

#### Option 1: $E = 4$

- *Properties.* Matches the existing four-module layout and the four-encoder mockups.
- *Pros.* Generous pitch. LED rings fit.
- *Cons.* Filter (cutoff, resonance, env amount, keytrack, drive), oscillator (wave, coarse,
  fine, level, shape) and LFO (rate, shape, depth, sync, fade) all want five. Four forces a
  second column group on the majority of pages.

#### Option 2: $E = 5$

- *Properties.* 28.1 mm pitch at $P = 92$.
- *Pros.* Covers oscillator, filter and LFO with no paging; envelope with a spare column.
  Comfortable pitch with 20 mm knobs.
- *Cons.* No LED rings. The 242 px module geometry in `panel.cc` does not survive; every
  layout constant in §7 of the panel doc moves.

#### Option 3: $E = 6$

- *Properties.* 23.4 mm pitch, requiring ~16 mm knobs.
- *Pros.* Headroom above the five-parameter modules.
- *Cons.* Tight but usable pitch. Column width falls to ~142 px — 14 characters at the 10 px
  advance — which is enough for a header and value but not a unit suffix. No rings.

**Observations.**

Four is the outlier among module parameter counts, not the norm. Paging is needed regardless
for part settings (§4.7), so the argument for a larger $E$ is about *frequency* of paging,
not its elimination.

$E$ cannot be revised after the panel is cut. "Start small and expand later" is available in
software and not in hardware; the exploration strategy must therefore be to prototype with a
superset on the X-Touch and remove, not to start minimal and add.

**Conclusion.** $E = 5$, pane $P = 92$ px. Retaining the existing 16 px margins:

$$92 + 5 \times 180 = 992$$

| | px | mm |
|---|---:|---:|
| Nav pane | 92 | 13.9 |
| Parameter column | 180 | 27.1 |
| Column centres from AA left edge | 198, 378, 558, 738, 918 | 29.8, 56.9, 84.0, 111.1, 138.2 |
| **Encoder pitch** | 180 | **27.1** |

Column width is 18 characters at the 10 px advance. Pane width is 9 characters: bracket,
space, four-character label, 32 px slack. **Column pitch is now a mechanical constant, not a
layout choice.**

### 4.5. Value Feedback Channel

*Governed by: P1, P2.*

**Definition.** How a parameter's absolute value is shown, given endless encoders have no
inherent position.

#### Option 1: LED rings on the parameter encoders

- *Properties.* 11–16 LEDs per encoder, at the finger.
- *Pros.* Readable without looking at the screen. Survives when the screen shows something
  else.
- *Cons.* Does not fit at $E \ge 5$ (§4.4). Resolution is coarse. Cannot distinguish
  unassigned from zero. Adds BOM, GPIO and a driver chain.

#### Option 2: On-screen split wells under 1:1 correspondence

- *Properties.* The existing split-well component, drawn in the column directly above the
  encoder.
- *Pros.* Higher resolution than a ring. Distinguishes three states a ring cannot —
  unassigned, exactly zero, and signed non-zero. Zero BOM, zero GPIO. Already designed and
  rendered.
- *Cons.* ~40 mm from the finger. Unavailable when the screen shows something else.

**Observations.**

Under 1:1 correspondence the two are largely redundant: the well sits directly above the
encoder it describes. The ring's surviving advantage is confined to the case where the eyes
are off the screen, which is performance (§4.12), not sound design.

**Conclusion.** Option 2 for the parameter row, which is also forced by $E = 5$. If ring
feedback proves necessary for performance mode, buy it back with a single ringed encoder in
the navigation cluster rather than five in the parameter row. Deferred with §4.12.

### 4.6. Gesture Vocabulary

*Governed by: P2, P3, P7.*

**Definition.** What each control does beyond turning, and whether a modifier exists.

#### Option 1: A general-purpose SHIFT

- *Properties.* One momentary modifier reused across contexts.
- *Pros.* Absorbs any function that does not fit.
- *Cons.* Has no meaning of its own, so it accumulates whatever did not fit elsewhere. A
  general modifier makes future layout gaps easy to paper over rather than surface.

#### Option 2: No modifier; a fixed push vocabulary

- *Properties.* Three gestures on every parameter encoder, distinguished by duration and by
  whether a detent occurred.

  | Gesture | Meaning | Scope |
  |---|---|---|
  | Hold + turn | fine adjust, ×⅒ | every continuous parameter, unconditional |
  | Long press (>500 ms, no detent) | revert to default | every parameter, unconditional |
  | Short press (<500 ms, no detent) | descend into the parameter | only where interior structure exists |

- *Properties.* Any detent while held commits to fine adjust; the release then does nothing.
- *Properties.* The conditional third gesture advertises itself with one glyph in the column
  header: `WAVE▸` means push acts, bare `WAVE` means it does not.
- *Pros.* The first two are unconditional, so they need no display and cannot surprise. The
  third is spatially bound to its control, costing one character rather than a legend row.
  `PUSH CONFIRM` and `PUSH CLEAR` cease to exist as concepts — modal actions become ordinary
  columns whose value is the action.
- *Cons.* Puts more load on push. Requires the detent-disambiguation rule to avoid the
  accidental-micro-turn failure.

**Observations.**

Removing SHIFT is a design constraint, not an aspiration: if a function cannot be reached
without one, that is evidence the layout is wrong, and the evidence should surface rather
than be absorbed.

Making modal actions ordinary columns also closes an open item in the panel doc — the save
dialogue's destructive action is currently the highlighted default; as a column value it must
be turned to, which is an explicit act.

Navigation encoder pushes follow the same discipline. NAV1 push does nothing: the pane is
flat and turning already commits, and a dead push is discoverable in a second. NAV2 push
enters or leaves whatever NAV2 is currently walking — one rule covering the browser, the
modulation view and modals.

**Conclusion.** Option 2. No SHIFT. Control complement: PART×4 (latching, LED), MOD
(momentary and tap, §4.8), PERF (latching, LED, deferred), column-group (momentary, §4.7).

### 4.7. Overflow Policy

*Governed by: P1, P5.*

**Definition.** What happens when content exceeds the space allotted, in each of the three
places it can happen.

#### Option 1: Free scrolling

- *Properties.* Content moves continuously under a fixed viewport.
- *Pros.* Uniform mechanism, unbounded capacity.
- *Cons.* Destroys positional constancy. A fixed cursor with scrolling content repaints the
  whole region on every detent. Long lists exhaust attention.

#### Option 2: Pagination

- *Properties.* Fixed groups of $\le E$, cycled by a momentary button, with a `1/2`
  indicator in the header.
- *Pros.* Column positions stay constant, so the encoder-to-column mapping stays learnable.
  Self-clearing; cannot strand the user.
- *Cons.* Needs a button. Only suits axes with a natural small group count.

#### Option 3: Moving cursor with a wrapping viewport

- *Properties.* Cursor moves freely; content scrolls by one when the cursor would leave the
  viewport; the viewport may straddle the modulo boundary.
- *Pros.* No homing policy, no relocation jump. Repaint cost is two small regions on most
  detents, with a full-region repaint only on edge crossings — roughly $1$ in $R$ for a
  viewport of $R$ rows, an order of magnitude cheaper than a fixed cursor.
- *Cons.* A seam exists where the wrap falls; needs one rule at a different weight to mark it.

#### Option 4: Newspaper reflow

- *Properties.* A one-dimensional list laid out in multiple columns, read in reading order,
  walked by a single encoder.
- *Pros.* Multiplies visible capacity without a second cursor axis. Position carries no
  semantics, so no axis ambiguity.
- *Cons.* Only applies where the content is genuinely 1D. Not applicable to a semantic 2D
  space.

**Observations.**

The distinguishing test between Option 4 and a semantic grid: if the cursor needs two
encoders, the space is 2D and the affordance problem returns; if one encoder suffices, it is
reflow.

Edge-crossing detents arrive in bursts when spinning, so a burst must not become a burst of
full-region repaints. No new mechanism is needed: the invalidation model
(`engine-recommendations.md` §5.7) already coalesces to one repaint per frame, and input
accumulates exactly regardless, so the cursor lands where the detents put it. An explicit
settle window would be coarser than the frame period and would only withhold the feedback
that tells the user when to stop. The residual question is cost, not timing — whether one
list-region repaint fits in a frame (§7.2).

**Conclusion.** Per site:

| Site | Policy |
|---|---|
| Parameters exceeding $E$ | Option 2, pagination, via the column-group button |
| Long item lists (MOD page slots) | Option 3, moving cursor with wrapping viewport |
| Patch list within a category (§4.10) | Option 3. Category selection bounds most lists; `All` on a large library overflows by construction |
| Route lists in a focused column | Option 4, reflow across the widened column (§4.8) |

### 4.8. Modulation Route Creation and Local Display

*Governed by: P6, P7.*

**Definition.** How a route is created, and where an existing route is seen and edited.

#### Option 1: Routes exist only on a dedicated modulation screen

- *Properties.* Creation and editing require navigating to that screen.
- *Pros.* One place; simple mental model.
- *Cons.* At 32+ routes per part, every routing decision costs a page change out and back in
  the middle of sound design. This is the deep-menu cost the model is built to avoid, applied
  to a headline feature.

#### Option 2: Held-modifier creation gesture, plus per-page display

- *Properties.* **MOD held** — the pane becomes the source list, NAV2 selects the source, the
  parameter columns switch from values to route amounts, and turning encoder $n$ writes a
  route from the armed source to that column's parameter. Reverts on release.
- *Properties.* **MOD tapped** — latches a modulation view of the current page. Everything
  below the column headers is replaced by that column's routes, inbound prefixed `←` and
  outbound `→`. Short press on encoder $n$ focuses its route list; the column widens to the
  full content width and reflows (§4.7); NAV2 walks the list; encoder $n$ sets the amount;
  press again to release. Tap MOD again to leave.
- *Pros.* Creation costs one gesture on the page already in view. No new addressing: each
  parameter encoder is already bound to a destination, and its column header already names
  it. Two hands, both reachable — MOD and NAV2 in the left cluster, parameter encoder on the
  right. The armed source is never hidden, because the source list is visible whenever the
  gesture is active. Entry and exit are the same button.
- *Cons.* While MOD is held, five parameter encoders change meaning simultaneously — the
  largest meaning-swap in the design. Mitigated by being momentary and by the columns
  visibly changing what they display.

**Observations.**

Column headers persist in the modulation view because they are the registration anchor: they
name the destinations, so they mean the same thing in both states. Keeping one line of
current value under each header is a cheap refinement — a route amount is easier to judge
against the value it displaces — at 20 px against ~400 px of route space.

Per-source-button arming is rejected: sources are a software axis that must be free to grow,
and dedicating panel area to them would freeze it.

**Conclusion.** Option 2.

```
│  MIX     │   CUTOFF     RESO      ENVAMT    KEYTRK    DRIVE     │
│ [FILT]   │                                                      │
│  AMP     │    2.4k      0.31       +48       50%       1.2      │
│          │   ▓▓▓▓░░    ▓▓░░░░     ░░▓▓▓░    ▓▓▓░░░    ▓▓░░░░    │
│          │   ←ENV2 +48                                          │
│          │   ←LFO1 -12            ←VEL  +20                     │
│          │   ←VEL  +08                                          │
```

Outbound routing from a modulator is a module-level parameter on that modulator's page — a
column headed `SENDS ▸` whose value is the count, descending into the full destination list.

### 4.9. Modulation Overview Representation

*Governed by: P1, P5, P6.*

**Definition.** How the complete set of routes is presented for editing and for
comprehension.

#### Option 1: Source × destination grid

- *Properties.* The current mockup: 12 sources × 8 destinations, 2D cursor, detail panel.
- *Pros.* Both axes visible; occupancy readable at a glance.
- *Cons.* A semantic 2D space, so it needs a two-axis cursor and nothing shows which axis a
  turn will move along. Destination count is well past 8 once four oscillator instances are
  counted, so the axis does not fit. Breaks 1:1 correspondence: encoder 1 would drive a
  cursor rather than the column above it. 96 cells against 32+ slots means navigating mostly
  empty space.

#### Option 2: Node-and-edge graph

- *Properties.* Sources and destinations as nodes, routes as edges.
- *Pros.* Topology is directly visible; supports the instrument's stated goal of building
  intuition about the engine.
- *Cons.* Diagonals are the most expensive primitive available. Fixed positions with
  orthogonal routing avoid the cost and a layout algorithm, but at 32+ routes produce a
  bundle of parallel runs crossing a wall of verticals — cheap to draw, unreadable.

#### Option 3: Adjacency list indexed by source

- *Properties.* One row per source, destinations inline: `LFO1 → OSC1.PIT-12 OSC2.PIT+12`.
- *Pros.* Fixed row per source, so a source never moves. Empty rows are informative. No
  diagonals, no crossings, no layout algorithm.
- *Cons.* Overflows in two independent directions — too many sources for the height, too many
  destinations for a line — so it needs variable-height rows or per-row horizontal overflow
  on top of vertical scroll. Structurally the most fragile as the source axis grows.

#### Option 4: Edge list of active slots

- *Properties.* One row per route, five columns matching $E$:

  ```
  │ [MOD]    │   SOURCE    DEST       AMOUNT    CURVE     ENABLE     │
  │          │ 01 LFO2     OSC3.PIT    -030     LIN       ON         │
  │          │[02]ENV3     FILT.CUT    +048     EXP       ON         │
  │          │ 03 VEL      AMP         +100     LIN       ON         │
  │          │ 04 —        —           —        —         —          │
  ```

- *Properties.* NAV2 walks rows. Add by moving to the trailing empty row and turning encoder
  1. Remove by long press on encoder 1 — §4.6's revert-to-default, with "empty" as the slot's
  default.
- *Pros.* 1:1 correspondence holds with no exception. Overflows in one direction only, so
  §4.7's Option 3 handles it. Addresses slots directly rather than hunting among empty
  intersections. No new gestures.
- *Cons.* Loses the cross-cutting view: a route's neighbours in the source or destination
  dimension are not visible.

**Observations.**

Global comprehension is a real need, separate from editing — understanding a patch's topology
is part of what the instrument is for. But it is a *read*, and reads may be dense without
being navigable.

Rejecting the grid removes four dependent problems, not one: the two-axis cursor and its
axis-ambiguity display, the viewport homing policy, the destination-grouping scheme needed to
fit the destination axis, and the repaint-coalescing question (§7.2). That four sub-problems
had to be invented to make the grid work is itself evidence against it.

Capacity is the deciding structural property. The edge list's length tracks route count in
one dimension; every other option overflows in two. Since the route, source and destination
counts are all expected to grow, one-dimensional overflow is the only one that degrades
gracefully.

**Conclusion.** Option 4 as the editor. The local per-page display (§4.8) is the primary
comprehension surface, since it answers "what modulates this" at the moment the parameter is
in view. Option 3 is retained as an optional global read, explicitly permitted to be
incomplete when it overflows. Options 1 and 2 are rejected.

### 4.10. Patch Browser Navigation

*Governed by: P5.*

**Definition.** How a patch is located among many.

#### Option 1: Numeric addressing

- *Properties.* NAV1 selects bank, NAV2 selects slot.
- *Pros.* Constant-time, no scrolling, positionally stable.
- *Cons.* Nobody recalls patches by number. Solves the mechanism and not the task.

#### Option 2: Single scrolling list

- *Properties.* The current mockup: 13 rows with a position bar.
- *Pros.* Names visible; simple.
- *Cons.* Long-list traversal, which is the failure mode this study rejects elsewhere.

#### Option 3: Category then patch

- *Properties.* NAV1 cycles a grouping — categories, with `All` as an entry — and NAV2 cycles
  patches within it. Both lists on screen simultaneously.
- *Pros.* Bounds the second list to something short. Scales to any library size: the
  interaction is identical at 128 patches or 2,000. Two-level, but both levels visible, so
  the hidden-state objection of §4.1 does not apply. Proven on the Micromonsta 2.
- *Cons.* Category assignment must exist and be maintained. Sorting changes positions and
  therefore costs constancy.

**Observations.**

Filtering is acceptable here and rejected in §4.9 for opposite reasons: a patch list has no
positional constancy worth preserving, because its contents change on every save.

Banks are a MIDI artifact — Program Change carries 0–127 and Bank Select supplies the high
bits — so the protocol needs a bank/program mapping but the browser need not navigate by it.
The numeric address is shown in the detail panel, since external sequencers need it, but it
is display, not addressing.

**Conclusion.** Option 3, with the numeric address displayed.

### 4.11. Part Switching Semantics

*Governed by: P3, P5.*

**Definition.** What happens to the pane cursor when a different part is selected.

#### Option 1: Page is global across parts

- *Properties.* The pane cursor does not move. Pressing PART 2 shows part 2's page.
- *Pros.* Pressing a part button changes only the values — headers, columns, layout and
  cursor identical — so the number being compared lands in the same pixels. This is
  flicker-comparison at minimum cost, and cross-part comparison is a defining multitimbral
  gesture. One cursor to track.
- *Cons.* Cannot hold position on two different pages in two different parts.

#### Option 2: Per-part cursor memory

- *Properties.* Each part remembers its own pane position.
- *Pros.* Supports working on parts independently.
- *Cons.* Four hidden cursors. Pressing PART 3 lands somewhere unpredictable — the same
  invisible-latched-state objection as §4.1, applied to the most frequently pressed button.

**Conclusion.** Option 1.

### 4.12. Performance Mode

*Governed by: P3, P7.*

**Definition.** A distinct mode optimised for playing rather than editing.

**Observations.** The constraints invert: sound design wants reach across ~500 parameters;
performance wants a small fixed set that never moves, usable while looking elsewhere. That
inversion justifies a real mode rather than a convention.

One property is settled regardless of content: it must be **latching** — both hands are
occupied while playing, so a held modifier is unusable — and latching state requires a
**physical indicator**, a backlit button or a switch whose position is visible. Screen-only
mode state is what makes a panel untrustworthy, and in this mode the screen is showing
something else by definition.

**Conclusion.** Deferred. Its content depends on which parameters prove to be reached for in
practice, which requires the default mode in use first. The PERF button and its LED are
reserved in the control complement (§4.6) so the panel does not need re-cutting.

## 5. Design Options

The dimensions interact, and three coherent compositions are worth stating.

### Option A: Hierarchical navigator with legend

$4.1{\cdot}1$ + $4.2{\cdot}1$ + $4.3{\cdot}1$ + $4.4{\cdot}1$ + $4.5{\cdot}1$.

Depth and breadth encoders, four ringed encoders at 32 mm pitch, encoder legend on every
screen, modulation on a source × destination grid. This is the entry-point proposal and the
closest composition to what exists.

- *Pros.* Minimum change from the current mockups. Ring feedback at the finger. Generous
  pitch.
- *Cons.* Latched navigation level with no physical indicator. Legend row on every screen.
  Column paging on the majority of module pages. The modulation grid breaks whatever
  correspondence rule is adopted.

### Option B: Flat pane, 1:1 columns, five encoders, no rings — *recommended*

$4.1{\cdot}3$ + $4.2{\cdot}2$ + $4.3{\cdot}2$ + $4.4{\cdot}2$ + $4.5{\cdot}2$ + $4.6{\cdot}2$
+ $4.8{\cdot}2$ + $4.9{\cdot}4$ + $4.10{\cdot}3$ + $4.11{\cdot}1$.

```
┌──────────┬────────────────────────────────────────────────────────┐
│ PART 1   │  INIT PATCH                                A007        │
├──────────┼────────────────────────────────────────────────────────┤
│  PART    │   CUTOFF    RESO      ENVAMT    KEYTRK    DRIVE        │
│ ┌ OSC1   │                                                        │
│ │ OSC2   │    2.4k     0.31       +48       50%       1.2         │
│ │ OSC3   │   ▓▓▓▓░░   ▓▓░░░░     ░░▓▓▓░    ▓▓▓░░░    ▓▓░░░░       │
│ └ OSC4   │                                                        │
│ ┌ MIX    │   ┌────────────────────────────────────────────────┐   │
│ │[FILT]  │   │           filter response plot                 │   │
│ └ AMP    │   │                                                │   │
│ ┌ ENV1   │   └────────────────────────────────────────────────┘   │
│ │ ENV2   │                                                        │
│ └ ENV3   │                                                        │
│ ┌ LFO1   │                                                        │
│ │ LFO2   │                                                        │
│ └ LFO3   │                                                        │
│   MOD    │                                                        │
│   FX     │                                                        │
├──────────┤                                                        │
│   PATCH  │                                                        │
│   CONF   │                                                        │
└──────────┴────────────────────────────────────────────────────────┘
   ◉  ◉         ◉         ◉         ◉         ◉         ◉
  N1  N2      ENC1      ENC2      ENC3      ENC4      ENC5
 ▣▣▣▣  ▣ ▣ ▣
 PARTS  MOD PERF GRP
```

Group brackets in the pane are one vline plus two ticks — the corner-bracket language at a
fifth scale. Globals sit below a full-width rule, which is one horizontal burst. Rotated
labels are rejected: Terminus is a bitmap atlas, so rotation requires either a second baked
atlas or per-pixel transposition, the worst available access pattern.

Hue is restricted to a single meaning — part identity, carried by the title-bar indicator and
the four part-button LEDs. The screen otherwise stays monochrome, so the emphasis ladder is
untouched and the terminal language holds.

- *Pros.* No latched navigation state. No legend row on any screen. Five columns cover
  oscillator, filter and LFO without paging. The plot grows to ~800×250 from 230×232.
  Modulation creation costs one gesture from the page in view.
- *Cons.* No ring feedback. Every layout constant in the panel doc §7 moves. Part settings
  require the one paging case.

### Option C: Flat pane, 1:1 columns, four encoders, with rings

As B but $4.4{\cdot}1$ + $4.5{\cdot}1$: four ringed encoders at 35.1 mm pitch.

- *Pros.* Everything in B except column count, plus ring feedback and a more generous pitch.
  Preserves the existing 242 px module geometry more nearly.
- *Cons.* Column paging on oscillator, filter and LFO — a page action on the majority of
  edits, permanently, in exchange for a feedback channel the on-screen wells already provide
  at higher resolution.

## 6. Evaluation

Criteria, stated before the comparison:

| | Criterion |
|---|---|
| C1 | Page or modifier actions in the hot set — the stated objective |
| C2 | Latched states with no physical or persistent visual indicator |
| C3 | Actions to compare the same parameter across two instances |
| C4 | Screen labels its own controls without a legend row |
| C5 | Plot area retained (the engine-intuition goal) |
| C6 | Mechanical uniformity — distinct control types, panel cutout variety |
| C7 | Degradation as parameter, source and route counts grow |

| Criterion | A | B | C |
|---|---|---|---|
| C1 page/modifier actions, hot set | high — legend-driven, plus depth re-aiming | **0** for osc/filter/LFO/env; 1 for part settings | 1 on osc, filter and LFO |
| C2 unindicated latched states | 1 (depth level) | **0** | **0** |
| C3 cross-instance comparison | 4 actions (unwind and re-descend) | **1 detent** | 1 detent |
| C4 self-labelling | no | **yes** | **yes** |
| C5 plot area | 230×232 | **~800×250** | ~800×250 |
| C6 mechanical uniformity | 6 encoders + rings, 2 types | **7 encoders, 1 type; 7 buttons, 1 type** | 6 encoders + rings, 2 types |
| C7 growth | grid fails on destination count; depth ragged | **1D overflow only at every site** | as B |

**Decisive trade-offs.**

C1 and C2 are the objective as stated in the problem: shifts should be rare, and the
instrument should be self-explanatory. Option A loses both, and loses them structurally
rather than by tuning.

Between B and C the trade is explicit and narrow: **ring feedback against one page action on
the majority of edits.** Under 1:1 correspondence the well sits directly above the encoder,
so the ring's marginal information is small and its marginal cost — paid on every filter,
oscillator and LFO edit for the life of the instrument — is not.

**Recommendation: Option B.** $E = 5$, 180 px column pitch, 27.1 mm mechanical pitch, no
rings on the parameter row, navigation cluster left of the display.

## 7. Open Questions

1. **Encoder acceleration for ordinary parameters.** Undecided, and it interacts with
   §4.6's ×⅒ fine adjust: if coarse turns accelerate, the effective ratio between a fast
   coarse turn and a held fine turn reaches ~30×, which may make the fine mode redundant.
   Settled elsewhere: no acceleration on cursor traversal (§4.7 Option 3 — wrap already
   bounds the worst case); capped 3× with a zero-crossing notch on modulation amounts,
   preserving the split well's distinct zero state. *Affects
   §4.6. Resolve by instrumenting the simulator over a fixed task suite, measuring
   $S(\tau)$ — page and modifier actions — separately from $A(\tau)$, total discrete
   actions.*
2. ~~**Repaint coalescing threshold** for edge-crossing scroll.~~ **Withdrawn.** Raised for
   the source × destination grid rejected in §4.9, where a fixed cursor repainted 96 cells
   and two label axes on every detent. Option 3 now applies only to list regions, and the
   invalidation model (`engine-recommendations.md` §5.7) already coalesces detents to one
   repaint per frame; a settle window coarser than the frame period would only remove the
   feedback that tells the user when to stop spinning. What survives is a *cost* question,
   already open as `engine-recommendations.md` §7.2 — whether one list-region repaint
   exceeds a frame period. If it does not, the dirty flag is the whole answer. *Check first
   whether GLCDC base-address scrolling makes the repaint unnecessary; if so the question
   closes outright.*
3. **Route-list overflow beyond a reflowed column.** §4.7's Option 4 gives a widened column
   roughly 100 visible routes. Behaviour past that is undefined; scroll-when-focused is the
   candidate. *Affects §4.8. Low priority — the bound is generous.*
4. **Part hue against the emphasis ladder.** Hue is restricted to the title-bar indicator and
   button LEDs, but the specific hues must remain legible against the four-level brightness
   palette. *Needs the real panel.*
5. **Pane width at 92 px** allows four-character labels with 32 px slack. If a subject label
   needs five characters, the margin gives way, not the column pitch. *Affects §4.4;
   confirm the subject list's final naming.*
6. **Non-square pixel correction.** $p_x/p_y = 1.0517$ (§3.4) affects every layout constant
   authored on the assumption of square pixels. *Affects the existing plot geometry; a
   sweep of `panel.cc` is required.*

## 8. Deliverables

- [ ] **Arch-design (primary):**
  `docs/workflow/arch-designs/nostromo-interaction_arch-design.md` — the settled interaction
  architecture: the addressing tuple and subject list, the control complement and its
  physical layout, the gesture vocabulary, the page taxonomy, the modulation model, and the
  mechanical constants ($E = 5$, 180 px column pitch, 27.1 mm). Written after this study
  reaches `resolved`. Living document, no date prefix. **The principles of §2 carry into it
  verbatim** — they govern screens not yet designed, and re-deriving them per screen is how
  the present inconsistency arose. Its *live regions* are the descriptor's DYN slots: each
  screen's chrome stays a descriptor, and the arch-design names the DYN hooks — what is
  live, what a control drives, and how it invalidates (§3.1).
- [ ] **Update:** `docs/random/panel-ui-design-state.md`. §9's layouts are superseded by the
  arch-design's page taxonomy; §10 becomes a pointer to it. §1–§8 and §11–§13 stand. The
  following §11 opens close as a consequence: the matrix's 2D traversal (grid rejected,
  §4.9), modal entry and exit (§4.6 — modal actions become ordinary columns; NAV2 push
  enters and leaves), the save dialogue's destructive default (same), and encoder
  acceleration (partially — see §7.1).
- [ ] **Simulator instrumentation** for the acceleration measurement in §7.1: an action log
  over a fixed task suite, comparing configurations on $S(\tau)$ and $A(\tau)$.
- Implementation is out of scope; it begins from the arch-design's plan.
