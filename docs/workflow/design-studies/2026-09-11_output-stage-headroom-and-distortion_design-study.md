---
title: Output Stage Headroom and Distortion
status: resolved
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
- *Cons:* The trade is ~6 dB of output level — nothing more. `kAmp` is already the per-patch level (Surge's scene volume), so there is no new burden on the musician; the rail simply sits 6 dB lower, buying a rail that engages 0.10% of samples instead of 9.29% (§5.7).

##### Option 3: Normalize per-voice (unison/voice-count scaling)

- *Properties:* Scale each voice (or the unison set) so the sum stays bounded — e.g. `1/N` or `1/√N` gain compensation — leaving the bus rail rare and single voices at full level.
- *Pros:* Keeps the rail rare *and* keeps single voices loud; no user burden and no constant-engagement saturator.
- *Cons:* **Rejected by the scaling law (§5.1).** Peaks scale as roughly `N^0.73` — between incoherent (`√N`) and coherent (`N`). `1/N` leaves 24 spread at 0.44 (over-attenuated), `1/√N` leaves it at 2.16 (still 6.7 dB into the rail). Neither law works; there is no normalization exponent that tracks the measured peak.

##### Option 4: Bus dynamics (compressor/limiter) — rejected (§5.7)

- *Properties:* A level detector and gain computer on the bus, reducing gain as the sum approaches the rail (attack/release envelope), ahead of the saturator. Deluge is the reference: its RMS compressor (`dsp/compressor/rms_feedback.cpp`) already runs the anti-aliased tanh at its output (`rms_feedback.cpp:106`).
- *Pros:* The one option that actually resolves the 28 dB spread: it keeps the rail rare (so protection needs no anti-aliasing, §3.3.2), keeps single voices loud, changes no per-voice semantics, and responds to the *signal* rather than the voice count — so it handles spread and unison alike, unlike normalization (§5.1).
- *Cons:* Envelope state and attack/release tuning; program-dependent gain modulation (pumping on sustained material). The deeper cost is to velocity's *audible* mapping: the compressor never touches velocity (per-voice, upstream of the bus), but above threshold it pulls the whole sum back, so a harder hit in a dense chord is not proportionally louder, and a note's loudness depends on what else is sounding rather than its own velocity. That is inherent — resolving the 28 dB spread means leveling off the sum. Whether the instrument self-levels or leaves level to the musician (the reference model, Option 2) is the product question.

**Conclusion:** Option 2, with `bus_gain` **0.125** (−18 dB, matching Surge's −18 dBFS). Measured real playing (§5.7) engages the rail 0.10% of samples at 0.125 versus 9.29% at 0.25 — one 6 dB step turns the rail from constantly engaged to essentially never. Option 1 (automatic 0.25) is IM-bound (§5.5); Option 3 is rejected by the scaling law (§5.1); Option 4 is rejected as unnecessary — 0.125 already gives a rare rail — and its velocity cost is real.

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

**Conclusion (per function):** Protection takes Option 2 (fixed tanh) — the rail is a cheap safety net, not a character control. Musicality ships a fixed curve (soft saturation) now with the configurable dispatch reserved (§6.2) — Option 3's *mechanism* without its memory cost today. The two answers differ *on purpose*: a configurable rail would expose protection as a timbre control, which none of the references does.

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

- *Properties:* First-order ADAA — output is `(F(x[n]) − F(x[n−1])) / (x[n] − x[n−1])` with `F` the antiderivative of the curve. Three implementations: closed-form (`F = log(cosh(x))`); a 1D `F`-table (~1 KB/curve, with a `|Δx| < ε` divide fallback); or a 2D table pre-computing the whole quotient to avoid the divide (Deluge's `getTanHAntialiased` — a `lastWorkingValue` state word plus a `tanH2d[x][x_prev]` table, `util/functions.h:295`; that this is specifically ADAA is *inferred* from the shape, not named in the source).
- *Pros:* Matches or beats 2× oversampling for a fraction of the cost (reviewer-measured: ×3 → −29.5 dB vs −31.2 dB for 2×; ×10 → −23.9 dB vs −22.6 dB). No filter design, no group delay, no ringing. The 1D `F`-table is bit-indistinguishable from closed-form at ~1 KB/curve and ~2.9× the cost of a plain tanh (§5.7).
- *Cons:* The closed-form variant needs log1p/exp — ~10× a plain tanh, *worse than oversampling* (§5.7). The 1D table needs the `|Δx| < ε` divide fallback, and `ε` is a *precision* guard, not just a divide-by-zero guard — the quotient cancels as `x[n] → x[n−1]` (worst at low frequencies), so a larger `ε` is better (§7). The 2D table avoids the divide but costs 64× the memory (~64 KB vs ~1 KB). The above numbers are reviewer-measured on the same 7 kHz sine and tanh as §5.2/§5.4 (directly comparable) — still verify against a polyBLEP saw.

**Conclusion (per function):** Protection takes Option 1 (none) — `bus_gain` 0.125 keeps the rail rare (§5.7), so it needs no anti-aliasing. Musicality takes Option 2 or 3, and the discriminator is CPU, not flash: flash is not tight (768 KiB; the engine is ~1% of it), but at 24 voices oversampling costs ~+141% versus +69% for tabulated ADAA — and closed-form ADAA is the expensive path at +236% (§5.7). The two mechanisms are not interchangeable across curves either — oversampling is curve-agnostic, ADAA needs a per-shape antiderivative — so the AA choice is gated by the curve choice (§6.2) and the CPU headroom measurement (§7).

## 4. Design Options (composed)

The recommendation composes the per-function conclusions into two stages:

**Protection (bus):**
- `kAmp` → a part *level*, no ceiling (§3.1.1).
- **Bus gain element** → the headroom scale (**0.125**, −18 dB) on the bus, before the saturator (§3.1.2, §6.1).
- **Saturator** → a fixed soft curve (tanh) replacing the bare `Clamp` (§3.3.1), with no anti-aliasing — the rail rarely engages (§6.1).
- **Hard clamp after** → retained as the DAC guarantee; it should never engage.
- **Metering tap** → a peak signal reporting how far into the rail the bus is.

**Musicality (per-voice):**
- **Drive control** → `kDrive`, a per-part `ParamId` into the per-voice shaper, decoupled from output level (§3.2.1; decoupling settled in §6.4).
- **Shaper** → one fixed curve (soft saturation) now, with the curve dispatch mechanism reserved for the eventual set (§6.2). It runs only when `kDrive > 0`, so drive-off patches pay nothing — the §7 CPU bar is 24 *driven* voices, not the engine's 24-voice baseline.
- **Anti-aliasing** → ADAA with a 1D antiderivative table on the per-voice stage (§6.3).

The *values* — the `bus_gain` default, the curve, the anti-aliasing mechanism — are parameter choices, not design decisions; §5 uses them only as evidence for the architectural choice. They are set in the implementation, but the study recommends starting values:

- `kAmp` default **1.0** (no ceiling).
- `bus_gain` default **0.125** (−18 dB) — §6.1.
- Protection curve: **fixed tanh**.
- Musicality curve: **one fixed curve (soft saturation) + dispatch mechanism** — §6.2.
- Musicality anti-aliasing: **ADAA, 1D antiderivative table** — §6.3.
- ADAA `ε` fallback: **1e-3** (a precision guard, not just a divide-by-zero guard; re-derive in Q31) — §7.

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

A `bus_gain` of 0.125 puts a single voice 18 dB below the rail — the same headroom Surge hard-clips at (−18 dBFS, §A.1) — and 24 spread at ×1.32, so the rail engages only 0.10% of samples in measured real playing (§5.7). The headroom constant lives on the bus, where it is the headroom boundary, rather than inside a part parameter that reads as "part volume".

The move from `kAmp` to `bus_gain` is output-preserving at the same value: today `kAmp 0.25 × 1.0` (headroom in the level), proposed `kAmp 1.0 × bus_gain 0.25`. Wherever `0.25 × raw < 1.0` the output is identical; only where `0.25 × raw ≥ 1.0` — where the current path hard-clips — does the proposal differ, saturating instead of chopping. Measured: a single voice at velocity 127 peaks at 0.257671 in both `kAmp 0.25` and `kAmp 1.0 × bus_gain 0.25` — ratio 1.0000, exactly output-preserving below the rail.

The recommended `bus_gain` of **0.125** is a *separate* decision from the migration: it is 6 dB quieter than today's 0.25 effective level, and that 6 dB is the explicit cost of a rail that essentially never engages (§5.7) — stated plainly rather than folded into the migration argument.

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

**Recommendation:** per function — a bus-level gain element (0.125) + fixed soft saturator + hard clamp + meter (protection); a `kDrive` control + per-voice shaper (one fixed curve + dispatch) with ADAA (1D antiderivative table) (musicality). The curve and oversampling-factor values are tuning, set in implementation.

### 5.7. Real-playing peaks and CPU cost (reviewer-measured)

The §5.1 worst case is sustained full polyphony; real playing does not sustain it. Measured with staggered chords, varied velocity, and envelopes at different stages:

| Passage | 0.25 peak | over rail | 0.125 peak | over rail |
|---|---:|---:|---:|---:|
| Solo, vel 60–110 | 0.28 | 0.00% | 0.14 | 0.00% |
| 5-note chords | 1.87 | 1.80% | 0.93 | 0.00% |
| 8-note chords, vel 100–127 | 2.83 | 9.29% | 1.41 | 0.10% |

One 6 dB step (`0.25 → 0.125`) takes the rail from constantly engaged to essentially never — the reference model without a compressor, at the same −18 dB headroom Surge hard-clips at (§A.1).

Per-evaluation cost, host x86 (the M85 also has an FPU, so the ratios should roughly transfer, but the log1p/exp penalty is libm-dependent and may differ on the M85 toolchain):

| Per evaluation | ns | ×plain |
|---|---:|---:|
| plain rational tanh | 1.44 | 1.0 |
| ADAA, 1D F-table (256, interp) | 4.20 | 2.9 |
| ADAA, closed-form (log1p/exp) | 14.45 | 10.0 |

Projected at 24 voices against a 147 ns/sample engine:

| Stage | vs current engine |
|---|---:|
| per-voice tanh (×24) | +24% |
| per-voice ADAA, 1D F-table | +69% |
| per-voice ADAA, closed-form (log1p/exp) | +236% |
| per-voice 2× oversampling | ~+141% (modelled) |

The tabulated/closed-form split matters more than ADAA-vs-oversampling: closed-form ADAA is *worse* than oversampling, because log1p/exp costs ~10× a tanh. ADAA (1D table) holds a ~2× advantage over oversampling — still decisive, but narrower than the earlier +51% guess. (The ~+141% oversampling figure is modelled — derived as ×6 for two saturator evaluations plus four biquads — not measured like the other rows.) This is the discriminator for §3.3.2, and it gates §6.3 on the CPU headroom measurement (§7).

## 6. Open Questions

The per-function architecture is settled by §5. §6.1 (headroom strategy), §6.2 (curve), and §6.3 (anti-aliasing) are resolved in that order — the curve choice determines the anti-aliasing mechanism — with §6.3 gated on the CPU headroom measurement (§7).

1. **Headroom strategy.** ~~(implicit: automatic `bus_gain` 0.25)~~ Resolved: `bus_gain` **0.125** (−18 dB, matching Surge's −18 dBFS). Measured real playing engages the rail 0.10% of samples at 0.125 vs 9.29% at 0.25 (§5.7). Per-voice normalization is rejected by the scaling law (§5.1); bus dynamics is rejected as unnecessary and velocity-costly. A rare rail needs no anti-aliasing (§3.3.2).

2. **Musicality curve.** ~~Fixed tanh vs configurable waveshaper.~~ Resolved: **ship one curve (soft saturation), build the curve dispatch now** — the same reserve-the-parameter argument as `kDrive` (§3.2.1). The eventual set is soft saturation, asymmetric/fuzz, and wavefold — all continuous, all ADAA-tractable. Quantize is lo-fi, not a drive shape (a discontinuous curve is not ADAA-tractable, §6.3). Expanding the set is gated by UI and CPU, not memory: a 1D antiderivative table is ~1 KB per curve (§5.7), so four shapes is ~4 KB — 0.5% of the 768 KiB partition.

3. **Anti-aliasing mechanism (per-voice).** Determined by §6.2 and CPU headroom. With one fixed curve (§6.2), **ADAA with a 1D antiderivative table** is the answer — a 1 KB `F`-table is bit-indistinguishable from closed-form (§5.7), and the `|Δx| < ε` fallback handles the divide (Deluge's 2D table avoids the divide at 64× the memory; it buys divide-avoidance, not memory efficiency). Closed-form is the *expensive* path — log1p/exp costs ~10× a tanh, worse than oversampling (§5.7) — so table the antiderivative in every case. The choice re-opens only if the curve set grows: a small continuous set → one 1D `F`-table per shape; a set including quantize → oversampling (ADAA cannot serve a discontinuous curve). Gated on the CPU headroom measurement (§7): at ~20% headroom either mechanism fits, at ~60% oversampling is out regardless of aliasing. Resolved for one curve, pending §7.

4. **Drive/level decoupling.** ~~Whether the drive is coupled to the bus gain or decoupled.~~ Resolved: **decouple** — `kDrive` into the per-voice shaper, with an output level after it (§4), so quiet-and-dirty and loud-and-clean are both reachable.

The §5.2/§5.4 figures use a 7 kHz sine chosen to fold badly; a polyBLEP saw at typical pitches folds less, so treat the numbers as an upper bound and a reliable *ranking*.

## 7. Deliverables

- [x] Arch-design: `docs/workflow/arch-designs/output-stage_arch-design.md` (written) — the two-stage output architecture: bus-level protection (gain 0.125, fixed soft saturator, hard clamp, meter) and per-voice musicality (`kDrive`, one fixed curve + dispatch, ADAA with a 1D antiderivative table).
- [ ] Implementation: `engine/engine.{h,cc}` — a bus gain stage (0.125), a fixed soft saturator replacing the bare `Clamp`, a hard clamp after it, and a peak metering signal (protection); a `kDrive` parameter and per-voice shaper with a curve dispatch and ADAA (1D antiderivative table) (musicality). Parameter values (curve, anti-aliasing table) are set here, not in this study.
- [ ] Measurement: the engine's CPU budget on the M85 — `bench`'s ns/sample/voice at 24 voices (the host figure is 9.1 ns/sample/voice via `tools/bench.cc`, not a target) expressed as a fraction of the 48 kHz frame budget, *plus* a per-evaluation microbenchmark of the three AA variants — the §5.7 ordering (tabulated vs closed-form vs oversampling) differs by 10× and is what §6.3 rests on. This gates §6.3: at ~20% headroom either mechanism fits; at ~60% oversampling is out regardless of aliasing.
- [ ] Test: `tests/test_engine.cc` — pre-clamp bus peak is observable; saturator is bounded; per-voice shaper anti-aliasing DC gain is unity and group delay is bounded and known (both mechanisms — oversampling *and* ADAA, which has ~half-sample group delay). Two ADAA-specific hazards: (1) the `|Δx| < ε` fallback is a *precision* guard, not just a divide-by-zero guard — measure 30–110 Hz SNR against a float64 reference and choose `ε` to hold ≥ ~90 dB (≈1e-3 beats 1e-6 by ~20 dB at 30 Hz; re-derive in Q31); (2) reset the ADAA state (`x[n−1]`, `F(x[n−1])`) on note-on — a voice steal must not carry the previous note's trailing sample into the first sample of the new note (a stale state word clicks).

## Appendix A: Comparison with the Reference Implementations

The companion report [Drive and Distortion Implementations](../reports/2026-09-11_drive-and-distortion-implementations_report.md) surveys Ambika, DelugeFirmware, and Surge XT. Read as two functions: the report's finding is that every reference keeps protection (the rail) cheap and unoversampled, and spends its anti-aliasing on the musical drive stage (§4.4 of the report).

### A.1. Side-by-side

| Dimension | Ambika | Deluge | Surge | Proposal |
|---|---|---|---|---|
| Protection (rail) | none (8-bit domain bounds signal) | saturating output shift | configurable hard clip (−18 dBFS default) | fixed tanh + hard clamp + meter |
| Musicality placement | mix-stage fuzz + analog filter | filter drive + post-FX saturation | per-filter drive + FX insert | per-voice drive |
| Curve (musicality) | fixed `tanh(6x)` | fixed tanh | configurable (50+ waveshapers) | one fixed curve + dispatch (§6.2) |
| Anti-aliasing | none | anti-aliased tanh (ADAA, inferred) | 4× oversampling | ADAA, 1D F-table (§6.3) |
| Drive/level | decoupled | decoupled | decoupled | decoupled |
| Curve as a control | no (amount only) | no (amount only) | yes (shape + depth) | reserved (dispatch; §6.2) |
| Non-monotonic / quantize shaping | `OP_FOLD` (fold), `OP_BITS` (bitcrush) | wavefold + bitcrush (and stateful SRR) | fold/rectify/cheby/digital shapes | fold/quantize as curve shapes (§3.3.1); SRR out (stateful) |

### A.2. Where it aligns

- **Protection is a separate stage from musicality** — matches all three: Surge's hard clip runs after the waveshaper, Deluge's saturating shift after the tanh stages, Ambika's byte domain under the fuzz. Never the same non-linearity.
- **Anti-aliasing follows the drive, not the rail** — matches all three: the rail is unoversampled everywhere; Deluge's `getTanHAntialiased` (ADAA) and Surge's 4× oversampling are spent on the musical stage.
- **Decoupled drive/level** — matches all three (Ambika's `MIX_FUZZ` vs VCA, Deluge's `clippingAmount` vs volume, Surge's `Drive` vs `Gain`).
- **Shape/depth orthogonal** — matches Surge; Ambika and Deluge expose only the depth (drive amount) of a fixed shape.

### A.3. Where it diverges

1. **Per-voice musical drive — where no reference is a pure match.** Surge and Deluge also distort inside the filter (per-filter drive, feedback saturation); the proposal's musicality is per-voice only, per-filter deferred. Deluge's post-FX saturation is a *light post-sum* musical stage — closer to the proposal's bus stage than to its per-voice shaper, and a reminder that light bus saturation is a valid (low-IM) third thing.

2. **One fixed curve with the dispatch reserved — between Surge and the rest.** Surge's 50-shape library is a range; Ambika/Deluge fix one curve. The proposal ships one fixed curve (Ambika/Deluge) but reserves the shape/depth dispatch (Surge's split) for the eventual set (§6.2) — Surge's generality without its memory cost today.

3. **ADAA as the choice — matching Deluge, with a 1D table instead of its 2D.** Deluge's `getTanHAntialiased` is lookup-table ADAA (inferred from its API, §3.3.2), but its 2D table buys divide-avoidance at 64× the memory; the proposal uses a 1D antiderivative table plus the `|Δx| < ε` fallback (§6.3), pending the CPU measurement (§7).

4. **Non-monotonic shaping as curve shapes, not separate lo-fi controls.** Ambika and Deluge expose fold/bitcrush as separate controls; Surge folds them into the waveshaper library. The proposal follows Surge — fold/rectifier/quantize are shapes in the curve selector (§3.3.1), with a heavier anti-aliasing cost — and omits sample-rate reduction, which is stateful, not a memoryless curve.

### A.4. Verdict

Two stages where the references confirm each: a cheap fixed soft rail that rarely engages (protection), and a per-voice drive with the anti-aliasing budget (musicality). The fixed-tanh rail matches Ambika/Deluge's single-curve simplicity and lands at the same −18 dB headroom Surge hard-clips at; the per-voice shaper ships one fixed curve with the dispatch reserved (§6.2); the anti-aliasing choice is ADAA with a 1D antiderivative table, matching Deluge's lookup-table approach at 1/64th the memory (§6.3). The one thing the references did that the proposal has now adopted is keep the rail rare — `bus_gain` 0.125 (§6.1).
