---
title: Output Stage Headroom and Distortion
status: draft
date: 2026-09-11
author: Dizan Vasquez
---

# Output Stage Headroom and Distortion

## 1. Problem Statement

The 4-voice headroom (a ×0.25 scale) now lives in `kAmp`, a part-level parameter that defaults to 0.25, reads "25%" in the UI, and is editable to 100%. Where it previously sat in a compile-time constant (`kVoiceHeadroom`) — and briefly in the velocity→amp route amount — it is now user-defeatable, so a part at unity plus full polyphony hard-clips the output bus.

This study resolves **where the headroom lives** and **what shape the distortion takes** when the bus does exceed full scale. The measurement in §5 shows the premise of "prevent clipping by constraining level" is wrong: there is a ~28 dB spread between the quietest and loudest legitimate cases, so no static gain covers it. Clipping is a certainty in ordinary polyphonic playing, not an edge case to prevent. The decision is therefore not *whether* the bus saturates, but *how*. The evidence base for the distortion-shape dimension (§3.2) is the companion report [Drive and Distortion Implementations](../reports/2026-09-11_drive-and-distortion-implementations_report.md), which surveys how Ambika, DelugeFirmware, and Surge XT each implement drive/distortion.

**In scope:** the mono output bus's gain staging and the non-linearity applied when the voice sum exceeds ±1.0; the `kAmp`/level parameter's default and range; a metering signal for the output stage.

**Out of scope:** per-voice or per-filter drive/saturation (a separate concern, phase 2+); effect DSP (reverb/delay); the phase-4 multi-bus routing (this study covers the single `g_buses[0]` mono path that exists today, and the conclusions transfer).

## 2. Current State

`RenderBlock` (`engine/engine.cc`) sums all active voices into `g_buses[0].L[]`, then applies a hard clamp per sample:

```cpp
for (int i = 0; i < frames; ++i) out[i] = Clamp(g_buses[0].L[i]);   // hard clip at ±1.0
```

The amp chain, after the phase-1 matrix cutover, is `amp_eff = kAmp × (velocity factor) × (envelope factor)`, with `kAmp` default 0.25 carrying the headroom. The bus is mono (`kNumBuses = 1`); there is no separate gain element between the voice sum and the clamp, and no metering.

## 3. Dimensions

### 3.1. Headroom placement

Where does the ×0.25 headroom scale live?

#### Option 1: `kAmp` (part level, default 0.25) — current

- *Properties:* The scale is a per-part parameter (`params[kAmp]`), default 0.25, range 0..1. It is the only per-voice gain path to the bus besides velocity and envelope.
- *Pros:* No extra state; one fewer multiply in the render loop.
- *Cons:* Conflates "part level" with "global headroom". A user raising the level to make one part louder simultaneously defeats the headroom for that part's polyphony — two concerns in one knob. The headroom is also user-visible as "25%", which misstates what it is.

#### Option 2: Fixed bus gain (`bus_gain`, default 0.25)

- *Properties:* A single scalar on `g_buses[0]`, applied once per sample after the voice sum. `kAmp` returns to a pure level (default 1.0, range 0..1, no ceiling).
- *Pros:* Headroom is stated as headroom, at the one place in the signal chain that is actually the headroom boundary (the bus). `kAmp` regains a clean "part level" meaning. The scale is a bus constant the user does not reason about as a level.
- *Cons:* One additional multiply per sample (negligible; the bus is written once per sample, not per voice). The headroom is no longer editable per part (accepted — see §3.2).

#### Option 3: Clamp `kAmp` (ceiling)

- *Properties:* Cap the part-level range below unity so the headroom cannot be defeated.
- *Pros:* Preserves the "no clip" guarantee if a ceiling could cover the worst case.
- *Cons:* **Rejected by measurement (§5.1).** The worst case (24-voice unison) needs 0.04, which would leave a single voice 28 dB below full scale. Any ceiling is an arbitrary point on a 28 dB range; it makes the instrument quiet everywhere else without preventing clipping at high polyphony.

**Conclusion:** Option 2. The headroom is a bus property, not a part property. `kAmp` is a level and should default to unity with no ceiling.

### 3.2. Distortion shape

When the voice sum exceeds ±1.0, what non-linearity does the bus apply?

#### Option 1: Hard clamp — current

- *Properties:* `Clamp(x) = min(max(x, -1), 1)` — a hard chop at the rail.
- *Pros:* Zero cost; deterministic; is already the output guarantee.
- *Cons:* Harsh odd-harmonic distortion, and the *most* aliasing of any soft curve (§5.2). Uncontrolled: no drive shaping, the ceiling is the only behavior.

#### Option 2: Soft-clip (tanh), no oversampling

- *Properties:* Replace `Clamp` with a smooth saturation curve (`tanh`-shaped), still evaluated once per sample.
- *Pros:* Even-harmonic character; bounded; a single function change.
- *Cons:* The aliasing improvement over hard clamp is only 2–3 dB (§5.2). By itself it does not change the character enough to justify shipping it as "the" distortion answer.

#### Option 3: Soft-clip + 2× oversampling

- *Properties:* A soft saturation curve evaluated at 2× the sample rate, with a half-band up/down filter pair around it, on the bus only (not per voice).
- *Pros:* ~10 dB aliasing reduction over the 1× soft-clip (§5.2), which is what turns overdrive into saturation rather than harshness. Cost is bounded: two biquads up, two down, and a 2× inner loop — ~96k saturator evaluations/s plus filtering on the M85, affordable.
- *Cons:* Most complex of the three; more state and code.

**Conclusion:** Option 3, gated on the open question in §6. The oversampling is the only option that changes the *character* of the distortion rather than nudging it; its cost is confined to the bus.

## 4. Design Options (composed)

The recommendation composes 3.1-option 2 with 3.2-option 3:

- **`kAmp`** → a part *level*, no ceiling. The headroom stops being a part parameter.
- **Bus gain element** → a scalar gain stage on the bus, between the voice sum and the non-linearity. This is where headroom lives — a bus property, not a part property.
- **Saturator element** → a soft non-linearity on the bus, replacing the bare `Clamp` as the primary ceiling, with oversampling filters around it to control aliasing. Drive is derived from the bus gain, so the bus gain is the overdrive control.
- **Hard clamp after** → retained as the DAC guarantee; it should never engage.
- **Metering tap** → a peak signal reporting how far into the saturator the bus is (the `OUTPUT` module's `CYCLE`/`SPEC` tabs), so overdrive is intentional rather than accidental.

The *values* — the `kAmp` and `bus_gain` defaults, the saturator curve, the oversampling factor — are parameter choices, not design decisions; §5 uses them only as evidence for the architectural choice. They are set in the implementation, but the study recommends starting values:

- `kAmp` default **1.0** (no ceiling).
- `bus_gain` default **0.25**.
- Saturator curve: **tanh-shaped**.
- Oversampling factor: **2×**.

These are a starting point, not a commitment — re-verify the factor and curve against real program material (a polyBLEP saw folds less than the 7 kHz sine used in §5.2/§5.4).

## 5. Evaluation

Measurements by the author, pre-clamp (reading the bus before `Clamp`).

### 5.1. Pre-clamp bus peaks (kAmp = 1.0)

| Voices | Peak | Headroom needed |
|---|---:|---:|
| 1 | 1.03 | 0.97 |
| 4 (spread) | 3.26 | 0.31 |
| 8 (spread) | 4.76 | 0.21 |
| 24 (spread, full poly) | 10.57 | 0.09 |
| 24 (same pitch, worst) | 24.74 | 0.04 |

There is ~28 dB between the quietest and loudest legitimate cases. No static gain covers it: sizing for the worst case (0.04) leaves a single voice 28 dB below full scale, and the current 0.25 is already ~3× too loud for full polyphony. Clipping is guaranteed in ordinary playing — a certainty to design, not an edge case to prevent. This falsifies any `kAmp` ceiling (3.1-option 3) and any static "no clip" guarantee.

### 5.2. Aliasing by curve (7 kHz sine, folded-back vs harmonic energy)

| Drive | Hard clamp | Cubic | tanh-ish |
|---:|---:|---:|---:|
| ×1 | −31.2 dB | −31.2 dB | −31.1 dB |
| ×3 | −18.0 dB | −19.7 dB | −21.2 dB |
| ×10 | −11.5 dB | −11.7 dB | −14.0 dB |

Swapping hard clamp for tanh buys 2–3 dB — real but second-order. What dominates is drive amount: ×1 → ×10 costs ~17 dB. Soft-clipping alone (3.2-option 2) would have shipped, been labeled intentional, and not fixed much.

### 5.3. Gain staging

A `bus_gain` of 0.25 puts 4 voices at unity and 24 spread at ×2.6 — in the −20 dB aliasing region instead of −11.5 dB. The headroom constant stays 0.25; it simply lives on the bus, where it is the headroom boundary, rather than inside a part parameter that reads as "part volume".

### 5.4. Oversampling

| Drive | 1× (tanh) | 2× oversampled |
|---:|---:|---:|
| ×3 | −21.2 dB | −31.3 dB |
| ×10 | −14.0 dB | −23.6 dB |

~10 dB reduction. Cost: two biquads up, two down, a 2× inner loop — on the bus only, ~96k saturator evaluations/s plus filtering on the M85.

### 5.5. Matrix

| Criterion | 3.1: kAmp | 3.1: bus_gain | 3.1: clamp | 3.2: hard | 3.2: tanh | 3.2: tanh+OS |
|---|---|---|---|---|---|---|
| Non-defeatable headroom | no | yes | yes | — | — | — |
| "amp = level" semantics | no | yes | no | — | — | — |
| Covers 28 dB polyphony spread | no | yes (via saturation) | no | — | — | — |
| Aliasing at ×10 drive | — | — | — | −11.5 dB | −14.0 dB | −23.6 dB |
| Character | — | — | — | harsh | slightly warm | saturation |
| Cost | — | — | — | 0 | 0 | 2 biquads + 2× loop |

**Recommendation:** a bus-level gain element (headroom), a soft saturator with oversampling (distortion), a hard clamp after as the DAC guarantee, and a metering tap. The gain/curve/oversampling values are tuning, set in implementation.

## 6. Open Questions

None blocking. The architectural decision — bus gain element + saturator + oversampling + meter — is settled by §5. The remaining unknowns are parameter values: the saturator curve and the oversampling factor. These are tuning, not design. The §5.2/§5.4 figures use a 7 kHz sine chosen to fold badly; a polyBLEP saw at typical pitches folds less, so treat the numbers as an upper bound and a reliable *ranking*, and re-measure with real program material before committing to the factor and curve.

## 7. Deliverables

- [ ] Arch-design update: `docs/workflow/arch-designs/synth-routing_arch-design.md` — record the output-stage architecture (bus-level gain element for headroom, soft saturator + oversampling for distortion, hard clamp as DAC guarantee, metering tap) as the settled audio-routing semantics.
- [ ] Implementation: `engine/engine.{h,cc}` — a bus gain stage, a saturator (+ oversampling) replacing the bare `Clamp` on the bus, a hard clamp after it, and a peak metering signal. Parameter values (defaults, curve, oversampling factor) are set here, not in this study.
- [ ] Test: `tests/test_engine.cc` — pre-clamp bus peak is observable; saturator is bounded; oversampling does not shift DC/phase.
