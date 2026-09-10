---
title: Modulation Matrix and Routing
status: review
date: 2026-09-10
author: Dizan Vasquez
---

# Modulation Matrix and Routing

## 1. Problem Statement

Twang's engine currently hardcodes its modulation — velocity→amp, envelope→cutoff (`filter_env_amount`), and envelope→amp are baked into the render path as fields and branches, and there is no LFO, no modulation matrix, and no audio-routing layer. The final instrument requires all three, and the brief's own guidance is to build foundational abstractions early rather than retrofit them later ("Build the part abstraction from day one. Retrofitting means threading a part ID and parameter-bank pointer through every line of the engine.").

This study resolves the architecture for **modulation routing and the modulation matrix** together with **audio/output routing**, targeting the final capability scope rather than the short-term POC. The evidence base is the companion report [Modulation Matrix Implementations](../reports/2026-09-10_modulation-matrix-implementations_report.md), which surveys how Ambika, DelugeFirmware, and Surge XT each build these systems.

**Locked capability target** (agreed out of band; the study does not re-litigate these numbers):

- **3 LFOs per part** — 2 per-voice + 1 global-per-part, 0.01–100 Hz, tempo-sync optional, shapes triangle/saw/square/sample-and-hold.
- **3 envelopes per part** — amp, filter, one free modulation envelope.
- **16 mod slots per part.**
- **Sources**: velocity, note→keytrack, gate, the 3 LFOs, the 3 envelopes, modwheel, channel aftertouch, pitchbend, expression, random, constant.
- **Destinations**: osc pitch (coarse/fine), osc wave/shape, filter cutoff, resonance, filter-env amount, amp, pan, LFO rates, envelope A/D/S/R.
- **Chaining**: modulate-a-modulator is first-class; depth-of-depth is a deferred-but-encodable extension.

**Out of scope** (deferred by decision, recorded so they are not lost): polyphonic aftertouch (per-note/MPE), the analog "routable part bus" (future extension; the digital/analog filter interaction is noted, not designed), per-part 8-channel output (gated on TDM, which the platform does not expose), effect DSP algorithms (reverb/delay), and MIDI-CC route editing (UI-only initially, with the flexibility reserved).

## 2. Current State

The engine lives in `engine/engine.{h,cc}` and is split across two cores on the EK-RA8D2: the M33 (control) runs the allocator, parameter writes, and MIDI; the M85 (audio) runs `Render` and the DSP. 4 parts, 24 voices, 48 kHz, control decimation `kControlDecimation = 16` (3 kHz).

- **Parameters** — `Part` holds 7 float params (cutoff, resonance, filter_env_amount, attack, decay, sustain, release) in a flat double-buffered `ParamBlock`, change-driven, snapshotted into `g_parts[]` each block. Addressed by a `ParamId` enum + descriptor table.
- **Events** — `Event` {type, part, voice, velocity, freq} over a lock-free SPSC `EventRing`. Note-on/off and (as of the velocity work) note velocity ride this ring.
- **Voice state** — `Voice` holds per-note DSP state (oscillator phase/inc, filter integrators, envelope, and now `gain`).
- **Hardcoded modulation** — velocity→gain (`Voice.gain = kVoiceHeadroom * v/127`), env→cutoff (`filter_env_amount` applied in `UpdateFilterCoeffs`), env→amp (envelope multiplies the output). No LFOs, no matrix, no pan, no effect sends; output is mono and summed.
- **Target hardware** — the M85 has a hardware single-precision FPU (verified: `CONFIG_FPU=y`, `-mfloat-abi=hard -mfpu=auto`), no hardware double precision, and Helium/MVE currently off (`-mcpu=cortex-m85.nomve`).

The hardcoded routes are the concrete manifestation of the problem: velocity→amp was added *this session*, and it is already the kind of special case a matrix would have to absorb.

## 3. Dimensions

### 3.1. Destination model

**Definition.** What can a modulation route target — a closed enumeration of destinations, or the open space of parameter ids. This determines whether "modulate a modulator" is free and whether special cases accumulate.

#### Option 1: Closed destination enum

- *Properties:* A fixed `MOD_DST_*` list (Ambika uses 19). Destinations are an explicit, small set.
- *Pros:* Minimal footprint; destination set is self-documenting; no interpretation of arbitrary ids.
- *Cons:* Every route that falls outside the enum becomes a hardcoded special case (Ambika's VCA multiplicative branch, filter-env bypass, wheel-scaled depth). "Modulate a modulator" does not fit — an LFO rate is a parameter, not an enum member.

#### Option 2: Open parameter-id space

- *Properties:* Destinations are the modulatable parameters themselves (Deluge/Surge). Twang's `ParamDesc` table already enumerates parameters, so destinations become "the set of modulatable params," extended to LFO rates and envelope A/D/S/R.
- *Pros:* Chained modulation falls out for free (an LFO rate or envelope time is just another destination). No special-case accumulation; one mechanism for every route.
- *Cons:* Requires a per-destination *combination class* to handle additive vs multiplicative vs exponential targets (see below); slightly larger destination metadata.

**Observation.** The survey identifies this as the single most consequential choice: it is the difference between Ambika's special-cased matrix and Deluge/Surge's general one. It also determines whether the locked "modulate-a-modulator" capability is free or bolted on.

**Conclusion.** Open parameter-id space, plus a per-destination **combination class** — `additive` (cutoff, resonance, wave/shape index, pan), `multiplicative` (amp/VCA, level, send amounts), `exponential` (pitch/semitones, LFO rate, envelope times). This absorbs the existing hardcoded routes directly: velocity→amp is a multiplicative amp route, env→cutoff an additive cutoff route.

### 3.2. Source placement

**Definition.** Where each modulation source is computed, and how it crosses the M33↔M85 boundary. This is entangled with the transport the engine already has, making it the expensive-to-retrofit dimension.

#### Option 1: Global LFO computed on the M33, streamed

- *Properties:* Ambika's model — the global LFO is rendered on the controller and pushed to the voice core per control step.
- *Pros:* Keeps LFO computation off the audio core.
- *Cons:* Requires a *continuous* per-step IPC stream — the one transport pattern the engine deliberately lacks (params are change-driven, events are discrete). Ambika pays for it with SPI flooding and a half-rate throttle on LFO 2/3.

#### Option 2: All LFOs and envelopes computed on the M85

- *Properties:* Per-voice LFOs and envelopes render inside the audio core's control-step loop; the global per-part LFO is per-part state advanced once per part per step and read by that part's voices. The M33 only *configures* rate/shape/depth.
- *Pros:* No new IPC channel — the event ring and param block remain the only two transport mechanisms. Zero stream latency; no bandwidth throttle.
- *Cons:* LFO state lives on the audio core (a small per-part phase accumulator); configuration still crosses the boundary (change-driven).

**Conclusion.** Option 2. Per-note sources (velocity, note, gate) ride the existing event ring; per-part sources (modwheel, channel aftertouch, pitchbend, expression) and LFO config ride the double-buffered param block; per-voice sources (the 2 per-voice LFOs, 3 envelopes) never leave the M85. All three LFOs live on the M85; the global one is per-part state, not streamed.

### 3.3. Value domain

**Definition.** How source values and route amounts are represented and combined — fixed-point vs float, and the unipolar/bipolar encoding.

#### Option 1: Fixed-point (byte or Q31)

- *Properties:* Ambika (byte domain) and Deluge (Q31) use integer arithmetic end to end.
- *Pros:* No FPU dependency; deterministic; the references prove it works on weak cores.
- *Cons:* Forces the AC-coupled/bipolar-neutral hacks Ambika carries (`source + 128`); less readable; unnecessary on a core with hardware FP.

#### Option 2: Float, unipolar/bipolar split

- *Properties:* Unipolar sources (velocity, envelope, gate, random, constant) ∈ [0, 1]; bipolar sources (LFOs, pitchbend, note→keytrack) ∈ [−1, +1], neutral at 0. Amount is a signed float. Combination is `base + Σ(amount × source)` for additive, `base × Π(...)` for multiplicative, log-domain for exponential.
- *Pros:* Hardware single-precision FPU on the M85 (verified §2) makes float multiply-adds free; the bipolar sources are already centered at 0, collapsing Ambika's AC-coupled branch; one multiply-add per route.
- *Cons:* Float rounding in long accumulations (bounded at 16 slots — negligible).

**Conclusion.** Option 2. Source values normalized to [0,1]/[−1,1]; amounts normalized, with the destination's range applied at evaluation (so a route is meaningful independent of its target). Natural units appear only at the destination's display mapping, consistent with how `ParamDesc` already separates norm from display.

### 3.4. Evaluation strategy

**Definition.** Where and at what rate the matrix runs, and how much work it culls.

#### Option 1: Per-row dirtiness (Deluge)

- *Properties:* A bitmask over *routes* skips destinations whose contributing sources did not change.
- *Pros:* Skips unchanged destinations.
- *Cons:* Couples slot count to a bitmask width — Deluge's hard 32-cable ceiling comes from exactly this (`kMaxNumPatchCables = kNumUnsignedIntegersToRepPatchCables * 32`). Adds state and ordering bugs for a saving that is negligible at 16 slots.

#### Option 2: Source-level gating only

- *Properties:* A per-part bitmask over *sources* ("is this source routed to anything") gates source generation; the 16-row loop runs unconditionally.
- *Pros:* The one big win — an unrouted LFO or envelope is not computed at all. Slots stay uncapped (the route array size is a free-standing constant). One AND instruction per source.
- *Cons:* Couples *source count* to a bitmask width (12 sources vs 32 bits — nowhere near the ceiling, widenable to 64 mechanically).

**Conclusion.** Option 2, at control rate (3 kHz) in the same control-step loop that advances envelopes and recomputes filter coefficients — not the per-sample inner loop. The amp and filter envelopes are always rendered (they drive the VCA and filter even without a route); the third envelope and both LFOs are gated. The culling bitmask keys off route *existence* (`source != kNone`), not amount — a zero-amount route is still "live" and keeps its source computed, so dragging an amount through zero never toggles source computation; only add/delete (changing `source` to/from `kNone`) changes the mask. The `kNone` sentinel is what makes "empty slot" a distinct state from "present-but-silent route" in a fixed table.

### 3.5. Chaining depth

**Definition.** How far modulation-of-modulation goes: a route targeting a modulator's parameter, versus a route targeting *another route's amount*.

#### Option 1: Modulate-a-modulator only

- *Properties:* A route's destination can be an LFO rate or an envelope time. Delivered for free by the open destination model (3.1).
- *Pros:* Covers the locked chaining capability; no ordering solver.
- *Cons:* Cannot express "envelope scales LFO→cutoff depth" as one route.

#### Option 2: Plus depth-of-depth

- *Properties:* A route whose destination is another route's amount (Deluge's `depthControlledBy`), requiring a two-phase evaluation (depth sources before amounts) and cycle concerns.
- *Pros:* Richest routing expressiveness.
- *Cons:* Highest complexity, lowest use — Deluge commits only one level, Surge lacks it, Ambika has only a hardcoded wheel→depth special case. Two-phase evaluation and ordering.

**Conclusion.** Option 1 in scope; depth-of-depth deferred but *reserved* in the destination encoding (a route-amount pseudo-destination), so it is additive later. Fixed source evaluation order; a source modulating an LFO rate or envelope time takes effect the next control step (~0.33 ms at 3 kHz), inaudible for rate modulation and avoids a dependency solver.

### 3.6. Route data model and transport

**Definition.** How the 16 routes are represented and how the mod config crosses the M33↔M85 boundary.

#### Option 1: Routes flattened into the parameter space

- *Properties:* Route source/destination/amount become addressable "parameters" (Ambika's stride-3 instance tables).
- *Pros:* One addressing scheme for UI/MIDI/patch.
- *Cons:* Route source/destination are enum-typed (discrete), a type mismatch with the float `ParamBlock`; 48 mixed fields bloat the flat buffer; the slot count becomes entangled with the param count.

#### Option 2: Routes as a separate small table

- *Properties:* `ModRoute[16]` per part (`source_id`, `destination_param_id`, `amount`), a distinct structure from the float params. Lives in the part state, double-buffered and snapshotted at the block boundary alongside the params.
- *Pros:* Keeps routes enum-typed; slot count is a free-standing constant (the thing that must grow to 32/64 later); matches Deluge/Surge, which both keep routes distinct from params.
- *Cons:* Routes need a distinct addressing surface (a mod-matrix page) from params.

**Conclusion.** Option 2. The thing `ParamBlock` transports generalizes from "7 floats" to "a part struct — float params + route table + LFO config"; one double-buffer, one swap, one source of truth. API: `EngineSetParam(part, id, v)` for scalars plus a small `EngineSetRoute(part, slot, src, dst, amount)`. Routes are UI-editable now; the addressing leaves room for MIDI-CC route editing later.

### 3.7. Cutover

**Definition.** How the three existing hardcoded routes migrate when the matrix lands.

#### Option 1: Default routes, delete the hardcoded paths

- *Properties:* velocity→amp, env→cutoff, env→amp become the first 3 of the 16 slots, pre-populated at `EngineInit`; `Voice.gain` and the `filter_env_amount` special case are deleted.
- *Pros:* One modulation mechanism; the matrix is exercised from day one, which is what makes the migration testable; no dual path to keep in sync.
- *Cons:* None material — the matrix must reproduce the default behavior exactly before the fields are deleted.

#### Option 2: Keep the hardcoded paths as fast paths

- *Properties:* The matrix coexists with the special-cased velocity/env routes for a while.
- *Pros:* Lower initial risk.
- *Cons:* Two code paths for the same modulation; the exact accumulation the matrix is meant to eliminate; the migration is never forced.

**Conclusion.** Option 1. Minor baked-in defaults: the random source is per-note (latched at `StartNote`); keytrack is note→cutoff with a per-part depth (0 = off, 1 = full), default off.

### 3.8. Audio/output routing

**Definition.** How voices sum into output buses, how effect sends and pan/level are expressed, and where the analog bus fits.

#### Option 1: Hardcoded main bus

- *Properties:* Voices sum into `main_L/main_R` directly.
- *Pros:* Trivial.
- *Cons:* A rewrite the day per-part outputs or effect sends are needed; the exploration doc already flags this ("Routing indirection to design in from day one").

#### Option 2: Indexed bus array with per-voice routing

- *Properties:* A fixed array of N stereo buses; each voice carries pan + level + send amounts and routes to a bus by index (`add_panned(scratch, bus[v.output_bus], ...)`).
- *Pros:* Per-part outputs and effect sends become a configuration, not a rewrite. The analog "routable part bus" becomes an insertion point (a part routes through an analog filter as its bus stage) rather than a special case.
- *Cons:* A small indirection cost and a bus-count constant.

**Conclusion.** Option 2: N indexed stereo buses (start at 1), 2 shared effect sends (reverb + delay) with per-voice *and* per-part send amounts, per-voice pan + level. Per-part 8-channel output is deferred (gated on TDM, which the platform does not expose). The analog bus is a future extension; the one structural fact recorded is that an analog filter is inherently **per-part paraphonic** (one circuit per part) while the digital SVF is **per-voice polyphonic**, so the bus model treats "filter" as a per-part-routable stage so the two can swap without a rewrite.

## 4. Design Options

The dimensions compose into one coherent shape; the key interactions are (3.1 ↔ 3.6) the open destination model pointing at params while routes live in a separate table, and (3.2 ↔ 3.4 ↔ 3.3) the evaluation loop where per-voice sources are gated, computed in fixed order, and combined in float. The concrete result:

```cpp
// Per-part mod config (double-buffered with the params; snapshot per block).
struct Part {
    // ... existing float params (cutoff, resonance, attack, decay, ...) ...
    float lfo_rate[3];     // LFO rates (destinations too)
    LfoShape lfo_shape[3]; // triangle/saw/square/S&H
    ModRoute routes[kModSlots];  // kModSlots = 16
};

// Modulation sources. kNone is the empty-slot sentinel: zero-initializing
// ModRoute routes[16] = {} marks every slot empty by default.
enum class ModSourceId : uint8_t { kNone = 0, kVelocity, kNote, kGate,
    kLfo0, kLfo1, kLfo2, kEnv0, kEnv1, kEnv2, kModWheel, kAftertouch,
    kPitchBend, kExpression, kRandom, kConstant };

// One route: source → destination param, signed normalized amount.
struct ModRoute {
    ModSourceId source = ModSourceId::kNone;  // kNone == empty slot (3.4)
    ParamId destination;                       // valid only when source != kNone
    float amount = 0.0f;                       // contribution; 0 == "present but silent"
};

// Per-voice mod state (audio core only).
struct Voice {
    // ... existing ...
    float lfo_phase[2];          // 2 per-voice LFOs
    // (the global LFO phase is per-part state, advanced once per part)
};

// Evaluation, once per control step (3 kHz):
//   1. gate sources on "is this source routed" bitmask
//   2. advance per-voice LFOs/envelopes, per-part global LFO
//   3. for each voice, for each of 16 routes:
//        effective[dest] += amount * source_value   (additive)
//        or effective[amp] *= ...                    (multiplicative)
//   4. write effective values into filter coeffs / osc inc / gain
```

## 5. Evaluation

Criteria stated before the results: **generality** (does it enable chained/all destinations without special cases), **cycle cost** (per-voice, control-rate), **transport fit** (does it add an IPC mechanism), **retrofit cost** (late-change cost), **complexity** (state and ordering surface).

| Dimension | Chosen | Generality | Cycle cost | Transport fit | Retrofit cost | Complexity |
|---|---|---|---|---|---|---|
| 3.1 Destination model | Open + class | High (chaining free) | — | — | Low | Low |
| 3.2 Source placement | All on M85 | High | Negligible (per-voice LFO) | No new IPC | **Low** (the expensive one) | Low |
| 3.3 Value domain | Float, normalized | — | HW FPU = free | — | Low | Low |
| 3.4 Evaluation | Source gating | High | ~1.1 M MAC/s total | — | Low (slots uncapped) | Low |
| 3.5 Chaining | Modulate-a-mod only | Adequate (depth deferred) | — | — | Low (depth encodable) | Low |
| 3.6 Route model | Separate table | High | — | One buffer | Low | Low |
| 3.7 Cutover | Default routes | — | — | — | Low | Low |
| 3.8 Audio routing | Indexed buses | High | Negligible | — | Low | Low |

**Recommendation.** The locked set above. The two decisions that carry the most weight — open destinations (3.1) and M85-local sources (3.2) — are exactly the two that are most expensive to retrofit, and both resolve to "general and cheap." The 16-slot matrix is ~1.1 M MAC/s at 24 voices/3 kHz, negligible against the SVF, and desktop-measurable before any silicon decision.

## 6. Open Questions

None blocking the recommendation. The following are deferred by explicit decision and do not change the current architecture:

1. **Depth-of-depth chaining** — deferred; the route-amount pseudo-destination is reserved so it is additive.
2. **Analog bus / digital+analog filter combination** — future extension; only the per-part-paraphonic vs per-voice-polyphonic structural note is captured (3.8).
3. **Per-part 8-channel output** — gated on TDM, which Zephyr/FSP does not expose.
4. **Polyphonic aftertouch** — per-note/MPE; channel aftertouch (per-part) is what the target assumes.
5. **MIDI-CC route editing** — UI-only initially; the route addressing (part, slot, field) leaves room for it.
6. **Tempo-sync clock transport** — the MIDI-clock → tempo-param path on the M33 is a follow-on detail, not a routing decision.
7. **Effect DSP** — reverb/delay algorithms are out of scope; only their send routing is specified.

## 7. Deliverables

- [ ] **Arch-design**: `docs/workflow/arch-designs/synth-routing_arch-design.md` — the settled internal architecture: `Part`/`Voice`/`ModRoute`/`ParamId` types, the evaluation loop, the transport contract, the source/destination/combination-class tables, and the bus/effect-send structure. This is the primary deliverable; it is written after this study reaches `resolved`.
- [ ] **Evidence base**: [Modulation Matrix Implementations](../reports/2026-09-10_modulation-matrix-implementations_report.md) (already written) remains the cited source for the reference-synth findings.
- [ ] Implementation is intentionally **out of scope** for this study — it begins from the arch-design's plan, after Phase-0 and the Phase-3 cycle measurement confirm the per-voice budget.
