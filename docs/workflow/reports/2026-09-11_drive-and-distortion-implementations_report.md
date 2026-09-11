---
title: Drive and Distortion Implementations
date: 2026-09-11
author: Dizan Vasquez
---

# Drive and Distortion Implementations

## 1. Summary

Survey of how three open-source synthesizers implement drive and distortion: Ambika (pichenettes/ambika, MIT), DelugeFirmware (SynthstromAudible/DelugeFirmware, GPL3), and Surge XT (surge-synthesizer/surge, GPL3). The three converge on one primitive — soft saturation via a `tanh`-shaped curve — and diverge only in how configurable that curve is and where in the chain it sits. Surge generalizes the curve into a 50-type lookup-table waveshaper library used both as a per-filter drive and a dedicated oversampled distortion effect; Deluge uses a fixed tanh everywhere, with a plain `getTanHUnknown` for filter drive/feedback and an explicitly anti-aliased `getTanHAntialiased` for its post-FX "saturation"; Ambika uses a single fixed `tanh(6x)` waveform table as a "fuzz" mix stage, plus wavefold/XOR/bitcrush mix ops and an analog ladder filter whose overdrive is physical. None of the three hard-clips as its drive — hard digital clipping appears only as a safety guarantee, never as the musical non-linearity.

## 2. Scope

| In scope | Out of scope |
|---|---|
| The drive/distortion non-linearity: curve, placement, anti-aliasing | Oscillator, envelope, and filter *topology* beyond the drive stage |
| Filter drive, post-FX saturation, and mix-stage fuzz/distortion | Reverb, delay, chorus, and other time-based effects |
| Lo-fi effects (bitcrush, sample-rate reduction, decimation) where they are the distortion mechanism | Patch storage and UI layout |
| Waveshaper/wavefolder primitives as they serve distortion | Synthesis-voice DSP not on the distortion path |

Source snapshots: Ambika at commit `2c4a690` (2019-11-18); DelugeFirmware at `e14e8ef2` (2026-09-07); Surge XT at `f05b0b90e` (2026-09-07).

## 3. Sources

1. Ambika (MIT) — `/workspace/ambika`, commit `2c4a690`.
   Key files: `voicecard/resources/waveforms.py`, `voicecard/voice.cc`, `voicecard/voice.h`, `voicecard/oscillator.cc`, `voicecard/voicecard.cc`.
2. DelugeFirmware (GPL3) — `/workspace/DelugeFirmware/src/deluge`, commit `e14e8ef2`.
   Key files: `dsp/filter/lpladder.cpp`, `dsp/filter/hpladder.cpp`, `dsp/filter/svf.cpp`, `dsp/filter/ladder_components.h`, `model/global_effectable/global_effectable_for_clip.{h,cpp}`, `model/mod_controllable/mod_controllable_audio.cpp`, `dsp/compressor/rms_feedback.cpp`, `dsp/delay/delay.cpp`, `modulation/params/param.cpp`.
3. Surge XT (GPL3) — `/workspace/surge/src`, commit `f05b0b90e`.
   Key files: `common/dsp/QuadFilterChain.cpp`, `common/dsp/SurgeVoice.cpp`, `common/dsp/effects/DistortionEffect.cpp`, `common/SurgeStorage.cpp`, `common/Parameter.cpp`, plus the `sst-waveshapers` submodule (`sst/waveshapers/WaveshaperConfiguration.h`).

## 4. Findings

### 4.1. Analytical Dimensions

The survey evaluates each implementation along five dimensions:

- **Primitive** — the non-linearity function: a fixed `tanh`, a configurable waveshaper lookup table, or a hard clip.
- **Placement** — where the non-linearity sits: per-filter drive, a post-FX saturation stage, a pre-filter mix/fuzz stage, or an analog filter's physical overdrive.
- **Curve flexibility** — whether the curve is fixed or user-selectable.
- **Anti-aliasing** — how the non-linearity controls fold-back (oversampling, an anti-aliased tanh, or none).
- **Lo-fi effects** — bitcrush, sample-rate reduction/decimation, and wavefold as separate distortion mechanisms.

### 4.2. Ambika (MIT)

A two-core AVR design: a digital voicecard (oscillators, mix, envelopes) driving a hardware analog filter board. Distortion lives in the digital mix stage and the analog filter.

**Fuzz/distortion — a fixed `tanh` table.** The "distortion" waveform is a 256-entry table generated in `voicecard/resources/waveforms.py`:

```python
signal_range = ((numpy.arange(0, 256) / 128.0 - 1.0))
fuzz = numpy.tanh(6.0 * signal_range) * 128.0 + 128.0
waveforms.append(('distortion', Scale(fuzz, dither=0)))
```

It is a single fixed `tanh(6x)` soft-clip, scaled to the 0..255 byte domain. The amount is the `MIX_FUZZ` destination (`voice.cc:248`), applied after the oscillator mix (`voice.cc:514-517`: `wet_gain = U14ShiftRight6(dst_[MOD_DST_MIX_FUZZ])`, then "apply distortion" over the mixed buffer).

**Mix ops.** `voice.cc:440-500` dispatches `patch_.mix_op`:

- `OP_XOR` — ring-mod XOR of the two oscillators.
- `OP_FOLD` — wavefold (`mix + 128` around the midpoint).
- `OP_BITS` — bitcrush: `wet_gain >>= 5; wet_gain = 255 - ((1 << wet_gain) - 1); ... & wet_gain` — a bit-depth mask.

`MIX_CRUSH` (`voice.cc:249`) is a separate destination feeding the crush amount.

**Analog filter overdrive.** The SMR-4 (and the other voicecards) are hardware filter boards. The digital voicecard writes only `cutoff`, `resonance`, and `mode` to them over DAC/PWM (`voicecard/voicecard.cc:141-143`). There is no digital drive parameter — overdrive is the physical ladder's input-level saturation, not a computed curve.

**Anti-aliasing.** None. The fuzz table is applied per-sample in the mix; there is no oversampling and no anti-aliased tanh.

### 4.3. DelugeFirmware (GPL3)

A single ARM core. Distortion is tanh everywhere, in two flavors — a plain `getTanHUnknown(x, N)` (N-iteration tanh approximation) for filter drive and feedback, and an explicitly anti-aliased `getTanHAntialiased(x, workingValue, amount)` for the post-FX saturation. Lo-fi (bitcrush + decimation) is a separate stage.

**Filter drive.** The drive ladder is a distinct filter mode, `FilterMode::TRANSISTOR_24DB_DRIVE` (`dsp/filter/lpladder.cpp`). It runs a driven ladder (`doDriveLPFOnSample`) and saturates the output:

```cpp
q31_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, l);
*currentSample = getTanHUnknown(outputSampleToKeep, 4);   // 4-iteration tanh
```

with optional oversampling when resonance is high.

**Filter feedback saturation.** The same tanh appears in the ladder's feedback (`lpladder.cpp:397`: `feedbacksSum = getTanHUnknown(feedbacksSum, 7)`), the high-pass ladder (`hpladder.cpp:106`: `getTanHUnknown(a, 2)`), and the SVF (`svf.cpp:100`: `band = getTanHUnknown(band, 3)`). The SVF and ladder saturate their resonance feedback rather than the whole signal.

**Post-FX saturation.** The "Saturation" control is `clippingAmount` (0..15, `menu_item/fx/clipping.h:55`), applied after the voice sum in `global_effectable_for_clip.{h,cpp}`:

```cpp
q31_t saturate(q31_t data, uint32_t* workingValue, int32_t shiftAmount) {
    return getTanHAntialiased(data, workingValue, 3 + clippingAmount) << shiftAmount;
}
```

`getTanHAntialiased` carries a `lastSaturationTanHWorkingValue` state word — this is the anti-aliased tanh (the "working value" tracks the fold-back to cancel aliasing), not the plain `getTanHUnknown`. The same anti-aliased tanh is used by the RMS compressor (`rms_feedback.cpp:106`) and the delay's analog-saturation mode (`delay.cpp:339`: `getTanHUnknown(..., delayWorkingState.analog_saturation)` — note this one is the plain variant).

**Lo-fi.** `processSRRAndBitcrushing()` (`model/mod_controllable/mod_controllable_audio.cpp:279`) applies bitcrush (`sample.l &= mask`, a bit-depth mask derived from `UNPATCHED_BITCRUSHING`) and sample-rate reduction (down-conversion via a `lowSampleRateIncrement` grab, `UNPATCHED_SAMPLE_RATE_REDUCTION`). These are separate from the tanh stages.

**Wavefold.** `LOCAL_FOLD` (a patched param) drives a wavefold in `dsp/util.hpp` (`add_saturate(level, FOLD_MIN)`).

**Curve flexibility.** Fixed tanh — no user-selectable curve. The only "shape" control is the drive amount (`clippingAmount`, filter drive level), which changes how hard the fixed tanh is driven.

### 4.4. Surge XT (GPL3)

A desktop/plugin synth. Distortion is a configurable lookup-table waveshaper, used both as a per-filter drive and a dedicated oversampled effect.

**Waveshaper library.** `sst::waveshapers` (the `sst-waveshapers` submodule) defines 50+ curve types in `WaveshaperType` (`WaveshaperConfiguration.h`): `wst_soft`, `wst_hard`, `wst_asym`, `wst_sine`, `wst_digital`, `wst_fuzz*`, `wst_singlefold`/`wst_dualfold`/`wst_westfold` (wavefolders), `wst_cheby2..5` (Chebyshev harmonics), `wst_fwrectify`/`wst_poswav`/`wst_negwav` (rectifiers), `wst_sinpx`/`wst_2cyc*` (trigonometric), and `wst_add12`+ (additive-harmonic). Each is a SIMD lookup table. `wst_soft` — the tanh-like soft-clip — is the workhorse, called directly in many effects (`lookup_waveshape(wst_soft, ...)` in the BBD ensemble, Combulator, Resonator, and Frequency Shifter).

**Filter drive.** Each filter stage in the `QuadFilterChain` applies the waveshaper with a dB `Drive` parameter:

```cpp
d.Drive = SIMD_MM(add_ps)(d.Drive, d.dDrive);
x = g.WSptr(&d.WSS[0], d.wsLPF, d.Drive);   // waveshaper(x, Drive)
```

`Drive = db_to_linear(wsunit.drive)` (`SurgeVoice.cpp:1417`). The waveshaper type is patch-selectable, so the drive *curve* is configurable, not just the amount.

**Distortion effect.** `DistortionEffect.cpp` is a dedicated insert: Drive (dB) → pre-EQ (peak) → waveshaper (`GetQuadWaveshaper(ws)`) → post-EQ, oversampled (`dist_OS_bits = 2`, i.e. 4×). The waveshaper type is user-selectable from the same 50+ table.

**Asymmetric tanh.** `SurgeStorage.cpp:3118` defines `shafted_tanh(x) = (exp(x) − exp(−1.2x)) / (exp(x) + exp(−x))` — an asymmetric tanh (unequal positive/negative drive), used in the saturation tables. A `fasttanh` (`sst::basic_blocks`) appears in the spring reverb's feedback.

**Curve flexibility.** Maximal — the curve is a first-class selectable parameter (50+ shapes), and the drive amount is separate, so shape and depth are orthogonal.

### 4.5. Comparison Matrix

| Dimension | Ambika | DelugeFirmware | Surge XT |
|---|---|---|---|
| Primitive | Fixed `tanh(6x)` table | Fixed tanh (`getTanHUnknown` / `getTanHAntialiased`) | Configurable waveshaper (50+ lookup tables) |
| Placement | Mix-stage fuzz + analog filter overdrive | Filter drive + post-FX saturation + filter feedback | Per-filter drive + dedicated oversampled FX |
| Curve flexibility | Fixed | Fixed (drive amount only) | User-selectable shape |
| Anti-aliasing | None | Anti-aliased tanh for saturation; oversampling for driven ladder | 4× oversampling on the distortion FX |
| Hard clip | None (bitcrush is bit-depth, not level) | None as drive (tanh) | None as drive (waveshaper) |
| Lo-fi | `OP_BITS` bitcrush, `OP_XOR` ring-mod, `OP_FOLD` wavefold | SRR + bitcrush stage, `LOCAL_FOLD` wavefold | Wavefolder and rectifier shapes in the same library |

## 5. Conclusions

### 5.1. Cross-Cutting Patterns

**Soft saturation is universal; hard clipping is absent as a drive.** All three implement drive/distortion as a smooth, monotonic, bounded curve — `tanh` in Ambika and Deluge, a tanh-like `wst_soft` (among others) in Surge. None applies a hard `Clamp` as its musical non-linearity; the hard rail appears only implicitly as a final output guarantee, not as the distortion itself.

**The curve is the product, and Surge is the only one that treats it as such.** Ambika and Deluge fix the curve and expose only a drive *amount* (how hard you push the fixed tanh). Surge separates shape from depth: the waveshaper type is a 50-value parameter, and the drive amount is a separate dB value. That one split is what lets Surge's "distortion" be a family (saturators, fuzzes, wavefolders, rectifiers, harmonic adders) rather than a single knob.

**Anti-aliasing tracks the curve's flexibility.** Ambika's static tanh table has no anti-aliasing. Deluge anti-aliases its post-FX saturation explicitly (`getTanHAntialiased` with a working-value state) and oversamples only the high-resonance driven ladder. Surge oversamples its distortion effect 4×. The two implementations that make drive a deliberate feature (Deluge's saturation, Surge's distortion FX) both pay an anti-aliasing cost; the one that treats it as a fixed waveform (Ambika) does not.

### 5.2. Notable Divergences

**Placement splits three ways.** Ambika puts the fuzz in the *mix* (pre-filter) and lets the analog ladder overdrive physically. Deluge puts tanh at the *filter* (drive + feedback) and at the *post-FX* stage. Surge puts a waveshaper at the *per-filter* drive and at a *dedicated FX insert*. The common thread is that the distortion is a *stage*, not a global output shaper — each synth can distort before, inside, and after the filter independently.

**Fixed-point tanh vs. float waveshaper.** Ambika and Deluge are integer-domain: the tanh is a byte table (`tanh(6x)`) or a Q31 approximation (`getTanHUnknown`, `getTanHAntialiased`). Surge is float, with SIMD lookup tables. The arithmetic tracks the target hardware, not the distortion semantics.

**Lo-fi is orthogonal to level drive everywhere.** Bitcrush/decimation (Ambika's `OP_BITS`, Deluge's SRR+bitcrush) and wavefold (both) are separate mechanisms from the level-based tanh drive — they degrade the signal differently (quantization/aliasing vs. amplitude compression) and are treated as independent controls, not as points on a single "distortion" axis.

### 5.3. Implications

If drive/distortion is a feature to be designed rather than a rail to be prevented, the observed landscape converges on: a bounded soft curve (tanh as the baseline), a separate drive amount, and — when the drive is meant to be pushed — anti-aliasing (oversampling or an anti-aliased tanh). Surge's configurable-waveshaper approach is the generalization that Ambika and Deluge approximate with a single fixed tanh. None of the three treats "clamp the output at full scale" as the distortion answer; the hard clamp is the safety net, not the sound.

This report is the evidence base for the companion design study [Output Stage Headroom and Distortion](../design-studies/2026-09-11_output-stage-headroom-and-distortion_design-study.md), which evaluates where twang's headroom should live and what shape its bus non-linearity should take.
