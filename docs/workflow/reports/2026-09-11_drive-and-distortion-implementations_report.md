---
title: Drive and Distortion Implementations
date: 2026-09-11
author: Dizan Vasquez
---

# Drive and Distortion Implementations

## 1. Summary

Survey of how three open-source synthesizers implement the output non-linearity as two functions — *protection* (the rail: preventing full-scale overflow) and *musicality* (drive as timbre): Ambika (pichenettes/ambika, MIT), DelugeFirmware (SynthstromAudible/DelugeFirmware, GPL3), and Surge XT (surge-synthesizer/surge, GPL3). On the musical side the three converge on one primitive — soft saturation via a `tanh`-shaped curve — and diverge only in how configurable that curve is and where it sits: Surge generalizes the curve into a 50-type lookup-table waveshaper library (per-filter drive plus a dedicated oversampled distortion effect); Deluge uses a fixed tanh everywhere (`getTanHUnknown` for filter drive/feedback, the anti-aliased `getTanHAntialiased` for post-FX saturation); Ambika uses a single fixed `tanh(6x)` fuzz table plus wavefold/XOR/bitcrush mix ops and an analog ladder whose overdrive is physical. On the protection side they diverge: Surge has an explicit configurable hard clip (`HARDCLIP_TO_18DBFS` default), Deluge a saturating output shift, and Ambika nothing beyond its bounded 8-bit domain. Protection and musicality are separate stages everywhere — never the same non-linearity.

## 2. Scope

| In scope | Out of scope |
|---|---|
| The drive/distortion non-linearity: curve, placement, anti-aliasing | Oscillator, envelope, and filter *topology* beyond the drive stage |
| Protection (the rail): output clamps, saturating gain, fixed-point bounds | Reverb, delay, chorus, and other time-based effects |
| Filter drive, post-FX saturation, and mix-stage fuzz/distortion | Patch storage and UI layout |
| Lo-fi effects (bitcrush, sample-rate reduction, decimation) where they are the distortion mechanism | Synthesis-voice DSP not on the distortion path |
| Waveshaper/wavefolder primitives as they serve distortion | — |

Source snapshots: Ambika at commit `2c4a690` (2019-11-18); DelugeFirmware at `e14e8ef2` (2026-09-07); Surge XT at `f05b0b90e` (2026-09-07).

## 3. Sources

1. Ambika (MIT) — `/workspace/ambika`, commit `2c4a690`.
   Key files: `voicecard/resources/waveforms.py`, `voicecard/voice.cc`, `voicecard/voice.h`, `voicecard/oscillator.cc`, `voicecard/voicecard.cc`.
2. DelugeFirmware (GPL3) — `/workspace/DelugeFirmware/src/deluge`, commit `e14e8ef2`.
   Key files: `dsp/filter/lpladder.cpp`, `dsp/filter/hpladder.cpp`, `dsp/filter/svf.cpp`, `dsp/filter/ladder_components.h`, `model/global_effectable/global_effectable_for_clip.{h,cpp}`, `model/mod_controllable/mod_controllable_audio.cpp`, `dsp/compressor/rms_feedback.cpp`, `dsp/delay/delay.cpp`, `modulation/params/param.cpp`, `processing/engines/audio_engine.cpp`, `util/functions.h`.
3. Surge XT (GPL3) — `/workspace/surge/src`, commit `f05b0b90e`.
   Key files: `common/dsp/QuadFilterChain.cpp`, `common/dsp/SurgeVoice.cpp`, `common/dsp/effects/DistortionEffect.cpp`, `common/SurgeStorage.cpp`, `common/SurgeStorage.h`, `common/SurgeSynthesizer.cpp`, `common/Parameter.cpp`, plus the `sst-waveshapers` submodule (`sst/waveshapers/WaveshaperConfiguration.h`).

## 4. Findings

### 4.1. Analytical Framework: Two Functions

A non-linearity on the output path serves one of two functions, and the three references implement them as *separate* stages rather than as a single "distortion" knob:

- **Protection** — the rail: guarantee the output never exceeds full scale. A hard clip, a saturating gain stage, or a bounded fixed-point domain.
- **Musicality** — drive as timbre: a user-facing non-linearity that shapes the sound (filter drive, post-FX saturation, mix-stage fuzz).

The two functions share dimensions, but the answer to each may differ. This survey reads each implementation along:

- **Curve** — the non-linearity shape: fixed `tanh`, configurable waveshaper lookup table, or hard clip.
- **Placement** — where the non-linearity sits: per-voice/per-filter, post-sum bus, or final output.
- **Drive amount** — whether drive is a separate user control (musicality) or fixed/absent (protection).
- **Anti-aliasing** — how the non-linearity controls fold-back (oversampling, an anti-aliased tanh, or none).
- **Lo-fi effects** — bitcrush, sample-rate reduction/decimation, and wavefold as separate distortion mechanisms.

### 4.2. Protection (the rail)

**Surge is the only one with an explicit, configurable protection stage.** `SurgeStorage.h:1808-1814` defines `HardClipMode` — `HARDCLIP_TO_18DBFS`, `HARDCLIP_TO_0DBFS`, and `BYPASS_HARDCLIP` (scene only) — defaulting to `HARDCLIP_TO_18DBFS` for both the master (`hardclipMode`) and each scene (`sceneHardclipMode`). It is applied as a hard clip at the scene output and again at the master output (`SurgeSynthesizer.cpp:4953-4984, 5145-5157`: `sdsp::hardclip_block*` on `sceneout` and `output`). The 18 dBFS default reserves headroom for a downstream limiter; the clip is a safety net, not a sound.

**Deluge protects implicitly with saturating arithmetic.** The output gain stage multiplies by 2^8 (`AUDIO_OUTPUT_GAIN_DOUBLINGS 8`, `audio_engine.cpp:103`) via `lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>` (`audio_engine.cpp:1188-1189`), which is `signed_saturate<32 - lshift>(val) << lshift` (`util/functions.h:101-104`) — the value is clamped to the signed range *before* the shift, so the ×256 gain cannot wrap into the DAC. This is a numerical safety net against integer overflow, not a user-facing limiter.

**Ambika has no explicit protection — the fixed-point domain bounds the signal.** The digital voicecard processes audio in the 8-bit byte domain (0..255), so a sample can never exceed full scale. The DAC write scales but cannot overflow: `sample_12bits.value = (sample * dac_scale) | 0x9000` with `dac_scale = 16` (`voicecard.cc:50, 78`) — an exact 8→12-bit expansion into the 12-bit DAC.

In all three, protection is *separate from* the musical non-linearity: Surge clips after the waveshaper, Deluge saturates the gain stage after the tanh stages, and Ambika relies on the byte domain. None drives its musical saturation hard enough to double as the rail.

### 4.3. Musicality (drive as timbre)

The user-facing side — the non-linearity as a timbre control. Each implementation below is the *musical* stage; the protection stage (§4.2) runs after it.

#### 4.3.1. Ambika (MIT)

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

#### 4.3.2. DelugeFirmware (GPL3)

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

#### 4.3.3. Surge XT (GPL3)

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

### 4.4. Interaction (how the two functions interact)

**They are stacked stages, not alternatives.** In the two synths that have both, protection runs *last*: Surge's musical waveshaper (per-filter drive, distortion FX) feeds the hard clip at the scene/master output; Deluge's musical tanh stages (filter drive, feedback, post-FX saturation) feed the saturating output shift. Ambika has only the musical side — its protection is the byte domain, not a stage. So protection and musicality do not compete for the same slot; the question is what each stage's curve should be, not which stage to keep.

**The drive amount is where the functions couple.** The musical drive is a user control everywhere (`MIX_FUZZ`, `clippingAmount`, `Drive`), and its setting determines how often the protection rail engages. Ambika and Deluge fix the curve and expose only this amount; Surge separates shape from depth. No reference exposes a protection *threshold* as a musical control — Surge's hardclip mode is a global safety setting, not a per-patch voice parameter.

**Anti-aliasing is a musicality cost, not a protection cost.** The rail (hard clip, saturating shift) needs no anti-aliasing — it is a last resort that should rarely engage. The musical non-linearity, which is meant to be pushed, is what pays the oversampling/anti-aliased-tanh cost (Surge 4×, Deluge's `getTanHAntialiased`). This is the concrete form of the shared-dimension point: curve and anti-aliasing are the same *axis* for both functions, but the *answer* differs — protection wants a cheap fixed soft curve; musicality wants a configurable curve whose aliasing is a deliberate trade.

### 4.5. Comparison Matrix

| Dimension | Ambika | DelugeFirmware | Surge XT |
|---|---|---|---|
| Protection (rail) | None — 8-bit byte domain bounds the signal | Saturating output shift (`lshiftAndSaturate<8>`) | Configurable hard clip (`HARDCLIP_TO_18DBFS` default) at scene + master |
| Musicality placement | Mix-stage fuzz + analog filter overdrive | Filter drive + post-FX saturation + filter feedback | Per-filter drive + dedicated oversampled FX |
| Curve (musicality) | Fixed `tanh(6x)` table | Fixed tanh (`getTanHUnknown` / `getTanHAntialiased`) | Configurable waveshaper (50+ lookup tables) |
| Curve flexibility | Fixed | Fixed (drive amount only) | User-selectable shape |
| Anti-aliasing | None | Anti-aliased tanh for saturation; oversampling for driven ladder | 4× oversampling on the distortion FX |
| Lo-fi | `OP_BITS` bitcrush, `OP_XOR` ring-mod, `OP_FOLD` wavefold | SRR + bitcrush stage, `LOCAL_FOLD` wavefold | Wavefolder and rectifier shapes in the same library |

## 5. Conclusions

### 5.1. Protection Is Implicit Everywhere Except Surge

Only Surge treats the rail as a designed stage — a configurable hard clip (`HARDCLIP_TO_18DBFS` default) at the scene and master output. Deluge's saturating output shift and Ambika's 8-bit byte domain are *implicit* protection: numerical safety nets that bound the signal without being a feature. None exposes the rail to the musician, and none makes it part of the sound — the hard clip is always a last resort, never a timbre (§4.2).

### 5.2. Musicality: The Curve Is the Product

Soft saturation is universal as the *musical* non-linearity — `tanh` in Ambika and Deluge, a tanh-like `wst_soft` among others in Surge. Ambika and Deluge fix the curve and expose only a drive *amount*; Surge separates shape from depth (50+ waveshaper types, drive as a separate dB value). That one split is what turns Surge's "distortion" into a family (saturators, fuzzes, wavefolders, rectifiers, harmonic adders) rather than a single knob.

Placement splits three ways on the musical side: Ambika puts the fuzz in the *mix* (pre-filter) and lets the analog ladder overdrive physically; Deluge puts tanh at the *filter* (drive + feedback) and the *post-FX* stage; Surge puts a waveshaper at the *per-filter* drive and a *dedicated FX insert*. The common thread is that musical distortion is a *stage*, not a global output shaper.

Anti-aliasing tracks the curve's flexibility: Ambika's static tanh has none; Deluge anti-aliases its post-FX saturation and oversamples only the high-resonance ladder; Surge oversamples its distortion FX 4×. Lo-fi (bitcrush, decimation, wavefold) is orthogonal to level drive everywhere — a separate mechanism, never a point on the drive axis. Arithmetic tracks the hardware, not the function: Ambika and Deluge are integer-domain (byte table, Q31 approximation), Surge is float with SIMD tables (§4.3).

### 5.3. Interaction: Separate Stages, Shared Axes

Protection and musicality are stacked, never merged — the rail runs *after* the drive stage in both synths that have both (§4.4). They share the *curve* and *anti-aliasing* axes, but with different answers: protection wants a cheap fixed soft curve with no anti-aliasing cost; musicality wants a configurable curve whose aliasing is a deliberate, payable trade. The drive amount is the coupling point — a musical control whose setting determines how often the rail engages. The references resolve the shared axes per function rather than once: Surge reuses one curve library for drive but a separate hard clip for the rail; Ambika and Deluge meet both functions with a single fixed curve and a fixed bound.

This report is the evidence base for the companion design study [Output Stage Headroom and Distortion](../design-studies/2026-09-11_output-stage-headroom-and-distortion_design-study.md), which evaluates each function — protection (the bus rail) and musicality (per-voice drive) — and the dimensions they share.
