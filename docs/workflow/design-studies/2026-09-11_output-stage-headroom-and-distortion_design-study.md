---
title: Output Stage Headroom and Distortion
status: draft
date: 2026-09-11
author: Dizan Vasquez
---

# Output Stage Headroom and Distortion

## 1. Problem Statement

The 4-voice headroom (a ×0.25 scale) lives in `kAmp`, a part-level parameter that defaults to 0.25, reads "25%" in the UI, and is editable to 100%. Where it previously sat in a compile-time constant (`kVoiceHeadroom`) — and briefly in the velocity→amp route amount — it is now user-defeatable, so a part at unity plus full polyphony hard-clips the output bus.

This study resolves **two separate functions** the output stage must serve — not a single placement choice:

- **Protection** — the rail: guaranteeing the output never exceeds full scale, and managing how polyphony stacks into the bus.
- **Musicality** — drive as timbre: a user-facing non-linearity that shapes the sound.

These are not mutually exclusive: a finished instrument has both. But they are *separate*, and placement is dictated by the function, not chosen freely — protection must see the final sum (it lives on the bus), while musical drive belongs per-voice (a bus-level drive generates cross-voice intermodulation, §5.5). They are closely related and share at least two dimensions — the *curve* and the *anti-aliasing* — which is why they belong in one study: those shared axes are decided *together*, even though the answer may differ per function (§3.3).

The measurement in §5 shows the premise of "prevent clipping by constraining level" is wrong: there is a ~28 dB spread between the quietest and loudest legitimate cases, so no static gain covers it. Clipping is a certainty in ordinary polyphonic playing, not an edge case to prevent. The decision is therefore not *whether* the bus saturates, but *how* — and, separately, whether the rail engages often enough to be part of the sound, which is exactly the protection/musicality boundary (§3.1.2). The evidence base for the curve dimension (§3.3.1) is the companion report [Drive and Distortion Implementations](../reports/2026-09-11_drive-and-distortion-implementations_report.md), which surveys how Ambika, DelugeFirmware, and Surge XT each implement drive/distortion.

**In scope:** the mono output bus's protection stage (gain staging, rail curve, metering); the per-voice musical waveshaper stage (curve, `kDrive` integration); the shared curve and anti-aliasing dimensions.

**Out of scope:** effect DSP (reverb/delay); the phase-4 multi-bus routing (this study covers the single `g_buses[0]` mono path that exists today, and the conclusions transfer); per-filter (rather than per-voice) shaping; sample-rate reduction (a stateful decimation, not a memoryless waveshaping curve). Wavefolding, rectification, and quantizing curves are *in scope* as waveshaping shapes (§3.3.1) — the topic is waveshaping, and the curve dimension covers the full family.

## 2. Current State

`RenderBlock` (`engine/engine.cc`) sums all active voices into `g_buses[0].L[]`, then applies a hard clamp per sample:

```cpp
for (int i = 0; i < frames; ++i) out[i] = Clamp(g_buses[0].L[i]);   // hard clip at ±1.0
```

The amp chain, after the phase-1 matrix cutover, is `amp_eff = kAmp × (velocity factor) × (envelope factor)`, with `kAmp` default 0.25 carrying the headroom. The bus is mono (`kNumBuses = 1`); there is no separate gain element between the voice sum and the clamp, and no metering.

The bare `Clamp` is the only non-linearity, and it serves both functions badly: as protection it is a hard chop at ±1.0, and as musicality it has no drive control and the worst aliasing of any curve (§5.2). With the proposed `kAmp = 1.0`, the clamp engages at 8 voices (§5.1) — it is part of the sound in ordinary polyphony, which is precisely the conflation the two functions must separate.

## 3. Dimensions

Two functions, each with its own dimensions, plus the dimensions they share. Placement is not a dimension: it defines the functions (protection is bus, musicality is per-voice).

### 3.1. Protection (the bus rail)

The rail: how the bus manages the voice sum so the output never exceeds full scale. Function-specific dimensions are where the headroom lives (§3.1.1) and how aggressively it protects (§3.1.2); the curve it uses is shared (§3.3.1).

#### 3.1.1. Headroom placement

Where does the ×0.25 headroom scale live?

##### Option 1: `kAmp` (part level, default 0.25) — current

- *Properties:* The scale is a per-part parameter (`params[kAmp]`), default 0.25, range 0..1. It is the only per-voice gain path to the bus besides velocity and envelope.
- *Pros:* No extra state; one fewer multiply in the render loop.
- *Cons:* Conflates "part level" with "global headroom". A user raising the level to make one part louder simultaneously defeats the headroom for that part's polyphony — two concerns in one knob. The headroom is also user-visible as "25%", which misstates what it is.

##### Option 2: Fixed bus gain (`bus_gain`, default 0.25)

- *Properties:* A single scalar on `g_buses[0]`, applied once per sample after the voice sum. `kAmp` returns to a pure level (default 1.0, range 0..1, no ceiling).
- *Pros:* Headroom is stated as headroom, at the one place in the signal chain that is actually the headroom boundary (the bus). `kAmp` regains a clean "part level" meaning. The scale is a bus constant the user does not reason about as a level.
- *Cons:* One additional multiply per sample (negligible; the bus is written once per sample, not per voice). The headroom is no longer editable per part (accepted).

##### Option 3: Clamp `kAmp` (ceiling)

- *Properties:* Cap the part-level range below unity so the headroom cannot be defeated.
- *Pros:* Preserves the "no clip" guarantee if a ceiling could cover the worst case.
- *Cons:* **Rejected by measurement (§5.1).** The worst case (24-voice unison) needs 0.04, which would leave a single voice 28 dB below full scale. Any ceiling is an arbitrary point on a 28 dB range; it makes the instrument quiet everywhere else without preventing clipping at high polyphony.

**Conclusion:** Option 2. The headroom is a bus property, not a part property. `kAmp` is a level and should default to unity with no ceiling.

#### 3.1.2. Headroom strategy

How aggressively the rail protects — equivalently, who manages the polyphony level. This is the dimension the references resolve by handing level to the musician; the study must state its choice rather than reach for it implicitly.

##### Option 1: Automatic headroom (rail engages in ordinary polyphony)

- *Properties:* A fixed `bus_gain` (e.g. 0.25) that keeps the *typical* case below the rail but lets the rail engage at high polyphony (8 voices → ×1.19 at 0.25, §5.3).
- *Pros:* No user burden; the engine manages polyphony stacking for the player. Matches the current proposal.
- *Cons:* The rail engages in ordinary playing, so it is an output saturator — it shapes the sound, and its dominant artefact at bus placement is cross-voice intermodulation, which no anti-aliasing reduces (§5.5). This makes the "protection" stage a de-facto musicality stage, collapsing the two functions the study separates. None of the three references does this — they keep the rail rare and make level a user responsibility (§A.1).

##### Option 2: Manual level (rail rarely engages) — the reference model

- *Properties:* A low `bus_gain` (or none) with part levels set by the patch/preset; the rail is a net underneath that catches only overs.
- *Pros:* The rail is true protection — inaudible in ordinary playing, engaging only on user error. Matches all three references (Surge clips at −18 dBFS with user-managed level, §A.1).
- *Cons:* Hands the 28 dB problem to the musician/preset designer. A single voice is quiet unless levels are raised; the burden moves from the engine to the content.

##### Option 3: Normalize per-voice (unison/voice-count scaling)

- *Properties:* Scale each voice (or the unison set) so the sum stays bounded — e.g. `1/N` or `1/√N` gain compensation — leaving the bus rail rare and single voices at full level.
- *Pros:* Keeps the rail rare *and* keeps single voices loud; no user burden and no constant-engagement saturator.
- *Cons:* **Rejected by the scaling law (§5.1).** Peaks scale as roughly `N^0.73` — between incoherent (`√N`) and coherent (`N`). `1/N` leaves 24 spread at 0.44 (over-attenuated), `1/√N` leaves it at 2.16 (still 6.7 dB into the rail). Neither law works; there is no normalization exponent that tracks the measured peak.

##### Option 4: Bus dynamics (compressor/limiter)

- *Properties:* A level detector and gain computer on the bus, reducing gain as the sum approaches the rail (attack/release envelope), ahead of the saturator. Deluge is the reference: its RMS compressor (`dsp/compressor/rms_feedback.cpp`) already runs the anti-aliased tanh at its output (`rms_feedback.cpp:106`).
- *Pros:* The one option that actually resolves the 28 dB spread: it keeps the rail rare (so protection needs no anti-aliasing, §3.3.2), keeps single voices loud, changes no per-voice semantics, and responds to the *signal* rather than the voice count — so it handles spread and unison alike, unlike normalization (§5.1).
- *Cons:* Envelope state and attack/release tuning; program-dependent gain modulation (pumping on sustained material). The deeper cost is to velocity's *audible* mapping: the compressor never touches velocity (per-voice, upstream of the bus), but above threshold it pulls the whole sum back, so a harder hit in a dense chord is not proportionally louder, and a note's loudness depends on what else is sounding rather than its own velocity. That is inherent — resolving the 28 dB spread means leveling off the sum. Whether the instrument self-levels or leaves level to the musician (the reference model, Option 2) is the product question.

**Conclusion:** Open — §6.1. Option 1 is what the current proposal implicitly reaches for (IM-bound, §5.5); Option 2 is the reference model (hands the spread to the musician); Option 3 is rejected by the scaling law (§5.1); Option 4 (dynamics) resolves the spread but introduces self-leveling. The choice determines the `bus_gain` value, whether the rail needs anti-aliasing (§3.3.2), and whether the bus carries a dynamics stage.

#### Metering

Not a dimension (no real alternatives) but a settled requirement: the protection stage must report how far into the rail the bus is — a peak signal, so overdrive is intentional rather than accidental (§4).

### 3.2. Musicality (the per-voice waveshaper)

Waveshaping as timbre — a memoryless non-linearity the musician drives. Placement is per-voice: a bus-level shaper generates cross-voice intermodulation (measured at −8.7 dB at ×4 drive vs −36 dB per-voice, §5.5), so heavy shaping must stay per-voice to remain consonant. (A *light* post-sum saturation is a valid but different stage — Deluge does it, §A.1 — and is not what this function is.)

#### 3.2.1. Matrix integration

Is drive a playable, modulatable control, or a fixed constant?

##### Option 1: Fixed drive (constant, not a parameter)

- *Properties:* The per-voice shaper runs at a fixed drive; no parameter, no modulation.
- *Pros:* Simplest; no parameter-space or matrix work.
- *Cons:* Drive is not performable — a drive that cannot be modulated is an effect, not an instrument control. Forfeits the core reason drive exists as a function.

##### Option 2: `kDrive` as a `ParamId` + mod-matrix destination

- *Properties:* A `kDrive` parameter per part, wired through the existing `CombinationClass`/mod-matrix so it can be a modulation source and destination (e.g. envelope → drive).
- *Pros:* Drive is playable — velocity/envelope/LFO can push it. Reserving the parameter now costs nothing; retrofitting it after shipping splits a parameter. Matches the references (drive is a user control everywhere, §5.2 of the report).
- *Cons:* One more parameter and matrix wiring.

**Conclusion:** Option 2. Drive is an instrument control, not a constant. The parameter is reserved now; the exact modulation routings are tuning.

### 3.3. Shared dimensions

The axes both functions share. They are decided *together* because the answer to one constrains the other — but the answer may deliberately differ per function.

#### 3.3.1. Curve

Both functions are non-linearities, so both need a curve. The options are the same; the recommended answer differs.

##### Option 1: Hard clamp

- *Properties:* `Clamp(x) = min(max(x, -1), 1)`.
- *Pros:* Zero cost; deterministic.
- *Cons:* Harsh odd harmonics, most aliasing (§5.2). Acceptable only for a rail that never engages.

##### Option 2: Fixed soft curve (tanh)

- *Properties:* A smooth, monotonic, bounded saturation (`tanh`-shaped), fixed.
- *Pros:* Even-harmonic character; bounded; matches Ambika and Deluge (§A.1). Cheap enough for a rail.
- *Cons:* One character only; drive amount is the only "shape" control.

##### Option 3: Configurable waveshaper (Surge-style)

- *Properties:* A shape selector over a family of curves plus a separate depth — shape and depth orthogonal, not a single drive knob.
- *Pros:* Turns distortion into a waveshaping family — monotonic saturations (soft/hard, fuzz, asymmetric) *and* non-monotonic shapes (fold, rectifier, sine, chebyshev) and quantizers (bitcrush-as-curve) — rather than one curve; matches Surge's split of shape from depth (§5.2 of the report). The split is nearly free at a small shape set; the full 50-shape library is not.
- *Cons:* A waveshaper table + shape parameter; more code than a fixed tanh. Non-monotonic and quantizing shapes alias worse and interact with the anti-aliasing dimension (§3.3.2).

**Conclusion (per function):** Protection takes Option 2 (fixed tanh) — the rail is a cheap safety net, not a character control. Musicality leans Option 3 (configurable) — the author's "character" interest and Surge's shape/depth split point there; this is open (§6.2). The two answers differ *on purpose*: a configurable rail would expose protection as a timbre control, which none of the references does.

**Observation (curve × anti-aliasing interaction):** the curve choice sets the anti-aliasing budget. Monotonic saturations (tanh, soft/hard, fuzz, asymmetric) fold modestly and are served by 2× oversampling or first-order ADAA (§5.4). Non-monotonic shapes (fold, rectifier, sine, chebyshev) and quantizers (bitcrush-as-curve) generate far more harmonics, so they alias worse — the §5.4 numbers, measured on a tanh-like curve, do not transfer to them, and they need their own measurement (likely more oversampling, or accepting the aliasing as character). Sample-rate reduction is the one mechanism genuinely outside waveshaping: it is stateful, not a memoryless curve. Do not make one knob morph saturation into folding — each shape is a distinct curve with its own anti-aliasing, not a point on a single drive axis.

#### 3.3.2. Anti-aliasing

Both non-linearities fold back energy above Nyquist, but the budget differs: the rail (rarely engaging) needs little or none; the drive (meant to be pushed) pays for it. The references confirm the asymmetry — every one keeps the rail unoversampled and spends its anti-aliasing on the drive stage (§4.4 of the report).

##### Option 1: None

- *Properties:* The non-linearity evaluated once per sample, no anti-aliasing.
- *Pros:* Zero cost; fine for a rail that rarely engages.
- *Cons:* Audible fold-back when pushed (×3 → −21 dB, ×10 → −14 dB, §5.4).

##### Option 2: Oversampling (2×/4×)

- *Properties:* Evaluate at 2× or 4× the sample rate (4× is a cascade of two 2× half-band stages), half-band up/down filters around it.
- *Pros:* ~10 dB aliasing reduction over 1× (§5.4); the factor follows the drive the stage supports.
- *Cons:* Filter state and code; the half-band filters ring (8th order is *worse* than 4th at high drive, §5.4). On the per-voice stage the cost is ×24 instances.

##### Option 3: Antiderivative anti-aliasing (ADAA)

- *Properties:* First-order ADAA — output is `(F(x[n]) − F(x[n−1])) / (x[n] − x[n−1])` with `F` the antiderivative of the curve. Two implementations: closed-form (`F = log(cosh(x))`) or a pre-computed 2D lookup table. Deluge's `getTanHAntialiased` is the lookup-table form — it carries a `lastWorkingValue` state word and interpolates a `tanH2d[x][x_prev]` table (`util/functions.h:295`); that this is specifically ADAA is *inferred* from the state-word-plus-2D-table shape, not named in the source.
- *Pros:* Matches or beats 2× oversampling for a fraction of the cost (reviewer-measured: ×3 → −29.5 dB vs −31.2 dB for 2×; ×10 → −23.9 dB vs −22.6 dB). No filter design, no group delay, no ringing. The lookup-table variant is Q31-native (fits twang's M85 fixed-point path) and pre-computes the diagonal, so it has no divide-by-zero.
- *Cons:* The closed-form variant has a `x[n] ≈ x[n−1]` denominator that needs a fallback (a DC input hits it every sample); the lookup-table variant costs 2D table memory. The above numbers are reviewer-measured on the same 7 kHz sine and tanh as §5.2/§5.4 (directly comparable) — still verify against a polyBLEP saw before recommending (§6.3).

**Conclusion (per function):** Protection takes Option 1 (none) or a cheap soft curve if §3.1.2 lands on automatic headroom — but if the rail engages often enough to need anti-aliasing, that is a sign the study has collapsed protection into musicality. Musicality takes Option 2 or 3 — the shaper is the stage that pays the anti-aliasing cost, and the required budget tracks the curve: monotonic saturations are served by ADAA or 2× (§5.4), while non-monotonic shapes need their own measurement (§3.3.1 observation). ADAA is the candidate to measure first (§6.3).

## 4. Design Options (composed)

The recommendation composes the per-function conclusions into two stages:

**Protection (bus):**
- `kAmp` → a part *level*, no ceiling (§3.1.1).
- **Bus gain element** → the headroom scale on the bus, before the saturator (§3.1.1).
- **Saturator** → a fixed soft curve (tanh) replacing the bare `Clamp` (§3.3.1), with no anti-aliasing unless §3.1.2 lands on automatic headroom (§3.3.2).
- **Hard clamp after** → retained as the DAC guarantee; it should never engage.
- **Metering tap** → a peak signal reporting how far into the rail the bus is.

**Musicality (per-voice):**
- **Drive control** → `kDrive`, a per-part `ParamId` into the per-voice shaper, decoupled from output level (§3.2.1; decoupling settled in §6.4).
- **Shaper** → a configurable curve (a small shape set — saturations + fold/rectifier/quantize — plus a separate depth) on each voice (§3.3.1).
- **Anti-aliasing** → oversampling or ADAA on the per-voice stage (§3.3.2).

The *values* — the `kAmp` and `bus_gain` defaults, the saturator curve, the oversampling factor — are parameter choices, not design decisions; §5 uses them only as evidence for the architectural choice. They are set in the implementation, but the study recommends starting values:

- `kAmp` default **1.0** (no ceiling).
- `bus_gain` default **TBD** — depends on §3.1.2 (0.25 is the automatic-headroom starting point).
- Protection curve: **fixed tanh**.
- Musicality curve: **configurable** (a small shape set; default TBD) — §6.2.
- Musicality anti-aliasing: **ADAA or 2×** (measure first — §6.3).

These are a starting point, not a commitment — re-verify the factor and curve against real program material (a polyBLEP saw folds less than the 7 kHz sine used in §5.2/§5.4).

## 5. Evaluation

Measurements by the author, pre-clamp (reading the bus before `Clamp`), unless noted.

### 5.1. Pre-clamp bus peaks (kAmp = 1.0)

| Voices | Peak | Gain to avoid clipping |
|---|---:|---:|
| 1 | 1.03 | 0.97 |
| 4 (spread) | 3.26 | 0.31 |
| 8 (spread) | 4.76 | 0.21 |
| 24 (spread, full poly) | 10.57 | 0.09 |
| 24 (same pitch, worst) | 24.74 | 0.04 |

There is ~28 dB between the quietest and loudest legitimate cases. No static gain covers it: sizing for the worst case (0.04) leaves a single voice 28 dB below full scale, and the current 0.25 is already ~3× too loud for full polyphony. Clipping is guaranteed in ordinary playing — a certainty to design, not an edge case to prevent. This falsifies any `kAmp` ceiling (§3.1.1-option 3) and any static "no clip" guarantee. It is also the evidence for the headroom-strategy fork (§3.1.2): at `bus_gain` 0.25 the rail engages at 8 voices (1.19), so "automatic headroom" and "rail as protection" cannot both hold.

Normalization cannot cover the spread either — the peaks scale as roughly `N^0.73`, between incoherent (`√N`) and coherent (`N`):

| Voices | Peak | ÷N | ÷√N |
|---:|---:|---:|---:|
| 1 | 1.03 | 1.03 | 1.03 |
| 4 | 3.26 | 0.82 | 1.63 |
| 8 | 4.76 | 0.60 | 1.68 |
| 24 (spread) | 10.57 | 0.44 | 2.16 |

`1/N` leaves 24 spread at 0.44 — over-attenuated — while `1/√N` leaves it at 2.16, still 6.7 dB into the rail. No normalization exponent tracks the measured peak, so per-voice normalization is rejected (§3.1.2-option 3) on the same evidence as the static gain.

### 5.2. Aliasing by curve (7 kHz sine, folded-back vs harmonic energy)

| Drive | Hard clamp | Cubic | tanh-ish |
|---:|---:|---:|---:|
| ×1 | −31.2 dB | −31.2 dB | −31.1 dB |
| ×3 | −18.0 dB | −19.7 dB | −21.2 dB |
| ×10 | −11.5 dB | −11.7 dB | −14.0 dB |

Swapping hard clamp for tanh buys 2–3 dB — real but second-order. What dominates is drive amount: ×1 → ×10 costs ~17 dB. Soft-clipping alone (fixed tanh) would have shipped, been labeled intentional, and not fixed much. This motivates the anti-aliasing dimension (§3.3.2) for whichever stage is meant to be pushed.

### 5.3. Gain staging and migration

A `bus_gain` of 0.25 puts 4 voices at unity and 24 spread at ×2.6 — in the −20 dB aliasing region instead of −11.5 dB. The headroom constant stays 0.25; it simply lives on the bus, where it is the headroom boundary, rather than inside a part parameter that reads as "part volume".

The change is output-preserving below the saturator threshold, because the composed gain is unchanged: today `kAmp 0.25 × 1.0` (headroom in the level), proposed `kAmp 1.0 × bus_gain 0.25`. Wherever `0.25 × raw < 1.0` the output is identical; only where `0.25 × raw ≥ 1.0` — where the current path hard-clips — does the proposal differ, saturating instead of chopping. Measured: a single voice at velocity 127 peaks at 0.257671 in both `kAmp 0.25` and `kAmp 1.0 × bus_gain 0.25` — ratio 1.0000, exactly output-preserving below the rail, as the composed gain predicts. The proposal changes behaviour only where the output is already being destroyed — a migration, not a re-voicing, and a far easier sign-off than "we changed the output stage".

### 5.4. Oversampling (factor × filter order)

| | ×3 drive | ×10 drive |
|---|---:|---:|
| 1×, no filter | −21.2 dB | −14.0 dB |
| 2×, 4th order | −31.3 dB | −23.6 dB |
| 2×, 8th order | −31.3 dB | −22.3 dB |
| 4×, 4th order | −30.1 dB | −28.1 dB |
| 4×, 8th order | −31.4 dB | −31.0 dB |

At ×3 drive, 2× saturates the benefit — 4× and steeper filters buy nothing, and −31 dB is the floor. At ×10, 2× plateaus around −23 dB while 4× reaches −31 dB. The oversampling factor therefore follows the drive the stage is meant to support, not a fixed choice. Note also that 8th order is *worse* than 4th at ×10 with 2× — more filter is not monotonically better, because the extra stages ring.

Two caveats apply to these numbers. First, the 7 kHz sine is chosen to fold badly; a polyBLEP saw at typical pitches folds less, so treat the magnitudes as an upper bound and a reliable *ranking*. Second, wavefolding is non-monotonic and aliases far worse than any saturator — the oversampling conclusions here do not transfer to it (§3.3.1 constraint).

Cost: two biquads up, two down, and the oversampling loop — on the bus only, ~96k saturator evaluations/s plus filtering on the M85. On the per-voice musicality stage the same oversampling costs ×24.

### 5.5. Placement and intermodulation

Author-measured intermodulation (two sines, so optimistic — real polyBLEP saws are worse; per-voice is not exactly zero at the same pitch class):

| Drive | Bus (post-sum) | Per-voice |
|---:|---:|---:|
| ×1.5 | −17.8 dB | −35.9 dB |
| ×4.0 | −8.7 dB | −36.1 dB |

A bus-level drive generates cross-voice intermodulation — at ×4 drive, −8.7 dB versus −36 dB per-voice. This is why musical drive is per-voice (§3.2): each voice self-harmonizes cleanly, while the sum through one non-linearity intermodulates. It is also the deeper argument against automatic headroom (§3.1.2-option 1): a rail that engages constantly is a bus-level drive, and no anti-aliasing technique (oversampling *or* ADAA) reduces intermodulation — the dominant artefact at bus placement. The only fixes are to not engage the rail, or to engage per-voice.

### 5.6. Matrix

| Criterion | 3.1.1 kAmp | 3.1.1 bus_gain | 3.1.1 clamp | 3.3.1 hard | 3.3.1 tanh | 3.3.1 config | 3.3.2 OS | 3.3.2 ADAA |
|---|---|---|---|---|---|---|---|---|
| Non-defeatable headroom | no | yes | yes | — | — | — | — | — |
| "amp = level" semantics | no | yes | no | — | — | — | — | — |
| Covers 28 dB polyphony spread | no | yes (via sat) | no | — | — | — | — | — |
| Aliasing at ×10 drive | — | — | — | −11.5 dB | −14.0 dB | ~−14 dB* | −23.6 dB | −23.9 dB† |
| Character range | — | — | — | none | one | small set | — | — |
| Cost | — | — | — | 0 | 0 | table + param | 2 biquads + 2× loop | state + divide (or 2D table) |

\* configurable curve's ×10 aliasing depends on the selected shape; tanh is the reference.
† reviewer-measured, first-order ADAA at ×10 (§3.3.2-option 3), pending verification.

**Recommendation:** per function — a bus-level gain element + fixed soft saturator + hard clamp + meter (protection); a `kDrive` control + configurable per-voice shaper with oversampling or ADAA (musicality). The gain/curve/oversampling values are tuning, set in implementation.

## 6. Open Questions

The per-function architecture is settled by §5; four items remain. The curve and anti-aliasing questions are now per-function (§6.2/§6.3); the headroom-strategy question (§6.1) is new and is the study's real unresolved choice.

1. **Headroom strategy.** ~~(implicit: automatic `bus_gain` 0.25)~~ Open: automatic rail (engages in ordinary polyphony — an output saturator, IM-bound) vs manual level (rare rail — the reference model) vs per-voice normalization (rejected by the scaling law, §5.1) vs bus dynamics (compressor/limiter — the one that resolves the spread, at the cost of self-leveling). This is a product decision, not a technical one; it sets the `bus_gain` value, whether the rail needs anti-aliasing, and whether the bus carries a dynamics stage (§3.1.2, §5.5). Open.

2. **Musicality curve.** ~~Fixed tanh (matching Ambika/Deluge) vs configurable waveshaper (Surge).~~ Relocated to the per-voice function (§3.3.1): fixed tanh is one character; a configurable shape selector plus a separate depth is a range — monotonic saturations (soft/hard, fuzz, asymmetric) and non-monotonic waveshapers (fold, rectifier, chebyshev) — at the cost of a table and a shape parameter. Non-monotonic shapes carry a heavier anti-aliasing cost (§6.3). The author's character interest points at configurable; shape and depth should be orthogonal either way. Open (was §6.3).

3. **Anti-aliasing mechanism (per-voice).** Oversampling (2×/4×) vs first-order ADAA. ADAA is cheaper (no filters, no ringing) and, on reviewer numbers (the same 7 kHz sine and tanh as §5.2/§5.4), matches 2× — but the closed-form variant has a `x[n]≈x[n−1]` hazard the lookup-table variant avoids. Measure ADAA against a polyBLEP saw before recommending; the rail stays unoversampled unless §6.1 lands on automatic headroom. Open.

4. **Drive/level decoupling.** ~~Whether the drive is coupled to the bus gain or decoupled.~~ Resolved: **decouple** — `kDrive` into the per-voice shaper, with an output level after it (§4), so quiet-and-dirty and loud-and-clean are both reachable.

The §5.2/§5.4 figures use a 7 kHz sine chosen to fold badly; a polyBLEP saw at typical pitches folds less, so treat the numbers as an upper bound and a reliable *ranking*.

## 7. Deliverables

- [ ] Arch-design update: `docs/workflow/arch-designs/synth-routing_arch-design.md` — record the two-stage output architecture (bus-level protection: gain element, fixed soft saturator, hard clamp, meter; per-voice musicality: `kDrive` control, configurable shaper, anti-aliasing) as the settled audio-routing semantics.
- [ ] Implementation: `engine/engine.{h,cc}` — a bus gain stage, a fixed soft saturator replacing the bare `Clamp`, a hard clamp after it, and a peak metering signal (protection); a `kDrive` parameter and per-voice shaper with configurable curve and anti-aliasing (musicality). Parameter values (defaults, curve, anti-aliasing mechanism) are set here, not in this study.
- [ ] Test: `tests/test_engine.cc` — pre-clamp bus peak is observable; saturator is bounded; per-voice shaper anti-aliasing DC gain is unity and group delay is bounded and known (both mechanisms — oversampling *and* ADAA, which has ~half-sample group delay); ADAA `x[n]≈x[n−1]` fallback is exercised (DC input).

## Appendix A: Comparison with the Reference Implementations

The companion report [Drive and Distortion Implementations](../reports/2026-09-11_drive-and-distortion-implementations_report.md) surveys Ambika, DelugeFirmware, and Surge XT. Read as two functions: the report's finding is that every reference keeps protection (the rail) cheap and unoversampled, and spends its anti-aliasing on the musical drive stage (§4.4 of the report).

### A.1. Side-by-side

| Dimension | Ambika | Deluge | Surge | Proposal |
|---|---|---|---|---|
| Protection (rail) | none (8-bit domain bounds signal) | saturating output shift | configurable hard clip (−18 dBFS default) | fixed tanh + hard clamp + meter |
| Musicality placement | mix-stage fuzz + analog filter | filter drive + post-FX saturation | per-filter drive + FX insert | per-voice drive |
| Curve (musicality) | fixed `tanh(6x)` | fixed tanh | configurable (50+ waveshapers) | configurable (small shape set; §6.2) |
| Anti-aliasing | none | anti-aliased tanh (ADAA, inferred) | 4× oversampling | ADAA or 2× (§6.3) |
| Drive/level | decoupled | decoupled | decoupled | decoupled |
| Curve as a control | no (amount only) | no (amount only) | yes (shape + depth) | yes (shape + depth; §6.2) |
| Non-monotonic / quantize shaping | `OP_FOLD` (fold), `OP_BITS` (bitcrush) | wavefold + bitcrush (and stateful SRR) | fold/rectify/cheby/digital shapes | fold/quantize as curve shapes (§3.3.1); SRR out (stateful) |

### A.2. Where it aligns

- **Protection is a separate stage from musicality** — matches all three: Surge's hard clip runs after the waveshaper, Deluge's saturating shift after the tanh stages, Ambika's byte domain under the fuzz. Never the same non-linearity.
- **Anti-aliasing follows the drive, not the rail** — matches all three: the rail is unoversampled everywhere; Deluge's `getTanHAntialiased` (ADAA) and Surge's 4× oversampling are spent on the musical stage.
- **Decoupled drive/level** — matches all three (Ambika's `MIX_FUZZ` vs VCA, Deluge's `clippingAmount` vs volume, Surge's `Drive` vs `Gain`).
- **Shape/depth orthogonal** — matches Surge; Ambika and Deluge expose only the depth (drive amount) of a fixed shape.

### A.3. Where it diverges

1. **Per-voice musical drive — where no reference is a pure match.** Surge and Deluge also distort inside the filter (per-filter drive, feedback saturation); the proposal's musicality is per-voice only, per-filter deferred. Deluge's post-FX saturation is a *light post-sum* musical stage — closer to the proposal's bus stage than to its per-voice shaper, and a reminder that light bus saturation is a valid (low-IM) third thing.

2. **Configurable curve at a small shape set — between Surge and the rest.** Surge's 50-shape library is a range; Ambika/Deluge fix one curve. The proposal takes the *shape/depth split* (orthogonal) but not the 50-shape library — a small selector, enough for the character range without Surge's generality.

3. **ADAA as a candidate — matching Deluge, but as a choice rather than a given.** Deluge's `getTanHAntialiased` is lookup-table ADAA (inferred from its API, §3.3.2); the proposal treats ADAA as one option alongside oversampling, to be measured (§6.3).

4. **Non-monotonic shaping as curve shapes, not separate lo-fi controls.** Ambika and Deluge expose fold/bitcrush as separate controls; Surge folds them into the waveshaper library. The proposal follows Surge — fold/rectifier/quantize are shapes in the curve selector (§3.3.1), with a heavier anti-aliasing cost — and omits sample-rate reduction, which is stateful, not a memoryless curve.

### A.4. Verdict

Two stages where the references confirm each: a cheap fixed soft rail that rarely engages (protection), and a per-voice drive with the anti-aliasing budget (musicality). The fixed-tanh rail matches Ambika/Deluge's single-curve simplicity; the per-voice configurable shaper borrows Surge's shape/depth split at a reduced shape count; the anti-aliasing choice (ADAA vs oversampling) matches Deluge and Surge respectively, decided by measurement (§6.3). The one thing the references do that the proposal has not yet chosen is keep the rail *rare* — the headroom-strategy question (§6.1) — which is the study's genuine remaining decision.
