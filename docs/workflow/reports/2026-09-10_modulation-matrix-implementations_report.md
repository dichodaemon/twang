---
title: Modulation Matrix Implementations
date: 2026-09-10
author: Dizan Vasquez
---

# Modulation Matrix Implementations

## 1. Summary

Survey of how three open-source synthesizers implement their modulation matrix: Ambika (pichenettes/ambika, MIT), DelugeFirmware (SynthstromAudible/DelugeFirmware, GPL3), and Surge XT (surge-synthesizer/surge, GPL3). The three span the design space: Ambika is a fixed 14-slot byte-domain matrix on an AVR voicecard with controller-side LFOs; Deluge is a fixed 32-"patch cable" Q31 matrix on a single ARM core with bitmask-gated evaluation; Surge is a dynamically sized float matrix with a parameter-id destination space that makes "modulate a modulator" routing free. The decisive architectural divergences are (1) whether destinations are a closed enum or the open parameter-id space, (2) whether chained modulation is a first-class mechanism or an emergent property of the destination model, and (3) where LFOs and the matrix live relative to the control/audio boundary.

## 2. Scope

| In scope | Out of scope |
|---|---|
| Modulation-matrix capability: slot count, sources, destinations, amount format/polarity, chained modulation | Oscillator, filter, and effect DSP topologies |
| LFO and envelope engines in their role as modulation sources/destinations | Patch storage formats beyond how the matrix table is serialized |
| Matrix evaluation strategy, placement, and cost characteristics | MIDI CC mapping beyond matrix-relevant routing |
| Architecture choices affecting the matrix | Non-modulation features (sequencer, arpeggiator, UI layout) |

Source snapshots: Ambika at commit `2c4a690` (2019-11-18); DelugeFirmware at `e14e8ef2` (2026-09-07); Surge XT at `f05b0b90e` (2026-09-07).

## 3. Sources

1. Ambika (MIT) — `/workspace/ambika`, commit `2c4a690`.
   Key files: `common/patch.h`, `voicecard/voice.cc`, `voicecard/voice.h`, `voicecard/voicecard_rx.h`, `controller/part.cc`, `controller/multi.cc`, `controller/parameter.cc`, `common/protocol.h`, `common/lfo.h`, `voicecard/envelope.h`.
2. DelugeFirmware (GPL3) — `/workspace/DelugeFirmware/src/deluge`, commit `e14e8ef2`.
   Key files: `modulation/patch/patch_cable_set.{h,cpp}`, `modulation/patch/patch_cable.h`, `modulation/patch/patcher.{h,cpp}`, `modulation/params/param.h`, `modulation/params/param_descriptor.h`, `modulation/automation/auto_param.h`, `modulation/lfo.h`, `modulation/envelope.h`, `model/voice/voice.cpp`, `processing/sound/sound.cpp` (all under `src/deluge/`), and `src/definitions_cxx.hpp`.
3. Surge XT (GPL3) — `/workspace/surge/src`, commit `f05b0b90e`.
   Key files: `common/ModulationSource.h`, `common/SurgeStorage.h`, `common/SurgeSynthesizer.cpp`, `common/dsp/SurgeVoice.{h,cpp}`, `common/dsp/modulators/LFOModulationSource.{h,cpp}`, `common/dsp/modulators/ADSRModulationSource.h`, `common/Parameter.cpp`.

## 4. Findings

### 4.1. Analytical Dimensions

The survey evaluates each implementation along six dimensions:

- **Slot model** — fixed-size table vs. dynamically sized list; the ceiling and its structural origin.
- **Destination model** — a closed enumeration of modulatable targets vs. the open parameter-id space.
- **Amount representation** — fixed-point (byte / Q31) vs. float, and the polarity/shaping attached to each route.
- **Chaining** — whether one modulator can modulate another (depth-of-depth, LFO-rate/env-time as destinations).
- **Evaluation** — where and at what rate the matrix runs, and whether it is culled/dirtiness-tracked.
- **Placement** — which core/thread computes sources and evaluates the matrix, and how the control/audio boundary is crossed.

### 4.2. Ambika (MIT)

A two-core AVR design: a master controller drives six voicecards over a shared SPI bus. The matrix *data* is a shared patch struct; the matrix *computation* lives entirely on the voicecard.

**Slots.** 14 routes, fixed. `common/patch.h` declares `kNumModulations = 14` and stores `Modulation modulation[14]` inline in the patch at byte offsets 50–91. Each `Modulation` is `{ uint8_t source; uint8_t destination; int8_t amount; }` (3 bytes, not bit-packed). A separate 4-entry `Modifier` op table lives at offsets 92–103.

**Sources — 31 values** (`enum ModulationSource`, `common/patch.h`, `MOD_SRC_LAST = 31`):

| Group | Members |
|---|---|
| Envelopes | `ENV_1`, `ENV_2`, `ENV_3` |
| LFOs | `LFO_1`, `LFO_2`, `LFO_3`, `LFO_4` |
| Modifier outputs | `OP_1`, `OP_2`, `OP_3`, `OP_4` |
| Sequencer/arp | `SEQ_1`, `SEQ_2`, `ARP_STEP` |
| Performance | `VELOCITY`, `AFTERTOUCH`, `PITCH_BEND`, `WHEEL`, `WHEEL_2`, `EXPRESSION` |
| Per-note | `NOTE`, `GATE` |
| Random | `NOISE`, `RANDOM` |
| Constants | `CONSTANT_256`, `CONSTANT_128`, `CONSTANT_64`, `CONSTANT_32`, `CONSTANT_16`, `CONSTANT_8`, `CONSTANT_4` |

Storage is a 31-byte `modulation_sources_[31]` array per voice.

**Destinations — 19 values** (`enum ModulationDestination`, `MOD_DST_LAST = 19`): `PARAMETER_1`, `PARAMETER_2`, `OSC_1`, `OSC_2`, `OSC_1_2_COARSE`, `OSC_1_2_FINE`, `MIX_BALANCE`, `MIX_PARAM`, `MIX_NOISE`, `MIX_SUB_OSC`, `MIX_FUZZ`, `MIX_CRUSH`, `FILTER_CUTOFF`, `FILTER_RESONANCE`, `ATTACK`, `DECAY`, `RELEASE`, `LFO_4`, `VCA`. Note there is no sustain destination, and `LFO_4` is the only LFO-rate destination.

**Amount.** `int8_t`, UI range −63..+63, bipolar. Sources are classified at application time (`voicecard/voice.cc`): LFO 1–4, pitch-bend, and note are "AC-coupled" (128 = no modulation, applied with `S8S8Mul(amount, source + 128)`); all other sources are unipolar (0..255, applied with `S8U8Mul(amount, source)`). Results accumulate into a 14-bit `dst_[]` accumulator clamped by `S16ClipU14`. There is no per-route curve; the only shaping is the AC/unipolar branch keyed on source identity.

**The VCA special case.** VCA is the one *multiplicative* destination (`voicecard/voice.cc`), applied per row in matrix order:

```cpp
if (amount < 0) { amount = -amount; source_value = 255 - source_value; }
if (amount != 63) { source_value = U8Mix(255, source_value, amount << 2); }
modulation_destinations_[MOD_DST_VCA] = U8U8MulShift8(
    modulation_destinations_[MOD_DST_VCA], source_value);
```

The source comment reads "The VCA modulation is multiplicative, not additive. Yet another Special case :(." A second hardcoded case scales the *last* row's amount by the mod wheel (`i == kNumModulations - 1`), and the filter envelope/LFO amounts are applied outside the matrix in `UpdateDestinations()`.

**Chaining.** No general modulator-to-modulator routing, but two restricted mechanisms: (a) 4 modifier ops (`SUM`, `PRODUCT`, `ATTENUATE`, `MAX`, `MIN`, `XOR`, `GE`, `LE`, `QUANTIZE`, `LAG_PROCESSOR`) compose sources into `OP_1..4`, which are themselves legal matrix sources and modifier operands; and (b) `MOD_DST_LFO_4` lets any source modulate the per-voice LFO rate (with one-block latency, since the LFO value is rendered before the matrix writes its rate).

**LFOs.** 4 total: LFO 1–3 are **global per part**, computed on the controller and streamed to the voicecards as differential 1-byte `0x50|n` SPI messages; LFO 4 is **per-voice**, computed on the voicecard. Shapes: triangle, square, sample-and-hold, ramp, plus 16 wavetable shapes. Rates 0..14 are tempo-synced; 15..127 free-run (min 1/16 Hz, max 100 Hz). Key-sync modes: `FREE` / `SLAVE` (reset on retrigger) / `MASTER` (retrigger envelopes on wrap). LFO 2 and 3 refresh at half the control rate to limit SPI traffic (`controller/part.cc`).

**Envelopes.** 3, one-shot ADSR (`ATTACK/DECAY/SUSTAIN/RELEASE/DEAD`), no loop mode. All three are matrix sources; attack/decay/release are matrix destinations (sustain is not).

**Evaluation.** Entirely on the voicecard (audio core), once per 40-sample control block (~980 Hz), per voice. The order is `LoadSources()` → `ProcessModulationMatrix()` → `UpdateDestinations()`. There is **no dirtiness tracking**: all 14 rows and 4 modifiers run unconditionally every block. The only culls are a post-matrix VCA silence skip (`vca() < 2`) and the LFO SPI delta/parity throttle.

**Implementation.** Fixed arrays, no allocation. Per-voice state is 88 bytes (31 + 19 + 38). The patch table lives in flash (`PROGMEM`); the mutable copy in RAM. All arithmetic is byte/14-bit integer — no floating point anywhere in the matrix. The same `Patch::modulation[14]` bytes serve patch storage, the UI, MIDI CC mapping, and the runtime matrix via stride-3 parameter-descriptor tables.

**Strengths (grounded).** A single shared struct serves storage, UI, MIDI, and runtime with no translation layer; compact fixed footprint (88 B/voice, 42 B patch); integer-only arithmetic; the AC/unipolar source classification yields correct "128 = no modulation" behavior for bipolar sources without a per-route flag; VCA handled multiplicatively; render cull for silent voices.

**Limitations (grounded).** Hardcoded special cases (VCA branch, wheel-scaled last row, filter env/LFO bypass the matrix) are not user-routable; no per-route curve or general amount-modulation; no matrix dirtiness/culling (14 rows always evaluated); order-dependent VCA semantics; one-block chaining latency; LFO 1–3 are controller-side and global (cannot be per-voice, rate-limited by SPI); `modulation_destinations_` is `int8_t` holding unsigned 0..255 semantics.

### 4.3. DelugeFirmware (GPL3)

A single ARM Cortex-A9 device. The matrix is "patch cables": one fixed array per Sound (and per kit drum NoteRow), evaluated by two `Patcher` instances on the one audio/routine thread.

**Slots.** 32 cables max, fixed. `definitions_cxx.hpp` derives `kMaxNumPatchCables = kNumUnsignedIntegersToRepPatchCables * 32 = 32` — the multiplier is the width of the "which sources are patched" bitmask. Storage is `PatchCable patchCables[32]` inline in `PatchCableSet` (`patch_cable_set.h`), carrying a `TODO: store these in dynamic memory.`

**Sources — 15 values** (`enum class PatchSource : uint8_t`, `definitions_cxx.hpp`): `LFO_GLOBAL_1`, `LFO_GLOBAL_2`, `SIDECHAIN`, `ENVELOPE_0..3`, `LFO_LOCAL_1`, `LFO_LOCAL_2`, `X`, `Y`, `AFTERTOUCH`, `VELOCITY`, `NOTE`, `RANDOM`, `NONE` (sentinel). `kNumPatchSources = 15`. Envelopes and local LFOs are per-voice; global LFOs and sidechain are per-Sound.

**Destinations.** There is **no destination enum**. Destinations are the `deluge::modulation::params` parameter ids (`ParamType = uint8_t`): 45 `Local` params (`LOCAL_OSC_A_VOLUME`, `LOCAL_LPF_FREQ`, `LOCAL_PITCH_ADJUST`, `LOCAL_LFO_LOCAL_FREQ_1/2`, `LOCAL_ENV_0..3_ATTACK/DECAY/RELEASE`, `LOCAL_PAN`, etc.) and 10 `Global` params (`GLOBAL_VOLUME_POST_FX`, `GLOBAL_DELAY_FEEDBACK/RATE`, `GLOBAL_LFO_FREQ_1/2`, `GLOBAL_ARP_RATE`, etc.). Legality is filtered dynamically by `Sound::maySourcePatchToParam()` (e.g. `GLOBAL_VOLUME_POST_FX` is manual-only; `LOCAL_VOLUME` rejects envelopes and sidechain; `GLOBAL_LFO_FREQ_*` is rejected while that LFO is tempo-synced).

**Amount.** 32-bit Q31 in an `AutoParam` (`patch_cable.h`), range ±2^30 = ±1073741824 (the default velocity→volume amount `0x3FFFFFE8` is +50/50 full-scale). Each cable has a `Polarity` (`UNIPOLAR`/`BIPOLAR`) applied at read time. Per-destination shaping is applied in `getModifiedPatchCableAmount()` (pitch and delay-rate amounts are squared, with special scalars for velocity→pitch). Combination semantics differ by param class: linear/volume params **multiply** cable factors around a neutral 2^29, hybrid/exp params **add** deviations around 0 (`patcher.cpp`).

**Chaining.** Two forms, both present. (a) **Depth-of-depth**: a cable may target the *amount* of another cable rather than a param. `ParamDescriptor` packs the bottom-level param plus up to three source bytes; a depth-controlling cable's destination is `param + one source`, resolved by `setupPatching()` into a `rangeAdjustmentPointer` on the target cable. Only **one** level of depth chaining is actually exercised (the comment states destinations carry "up to one source and one param"). (b) **Modulator params as destinations**: LFO rates and envelope attack/decay/release/sustain are ordinary patchable params, so any source can modulate them. No cycle detection exists (`maySourcePatchToParam` has no patch-graph validation).

**LFOs.** 4 engines: 2 global (`LFO_GLOBAL_1` = LFO1, `LFO_GLOBAL_2` = LFO3) and 2 local per-voice (`LFO_LOCAL_1` = LFO2, `LFO_LOCAL_2` = LFO4). 7 shapes: sine, triangle, square, saw, sample-and-hold, random-walk, "warble". Rates are free-running or tempo-synced; local LFOs are reset and phase-initialized on note-on. Local LFOs render inside `Voice::render()` (audio core, per voice) only when their source bit is patched; global LFOs render inside `Sound::render()`.

**Envelopes.** 4, ADSR plus hold and fast-release; no loop. Envelope 0 is forced to always render (amplitude envelope); envelopes 1–3 render only when patched. All four are sources; their stage times and sustain are destinations.

**Evaluation.** Two `Patcher`s on the one core: `Sound` patches global params once per block, each `Voice` patches local params once per block (plus `performInitialPatching()` at note-on). Rate is per 128-sample block at 44100 Hz. Dirtiness is a `sourcesChanged` bitmask ANDed against a precomputed `sourcesPatchedToAnything[globality]` bitmask; the patcher early-outs when the intersection is zero, and skips destinations whose source mask doesn't intersect. Source generators (LFOs, envelopes) test the same bitmask before rendering — "only compute what's patched."

**Implementation.** Fixed cable array + a derived, heap-built `Destination*` index (grouped by destination, sorted deterministically). `PatchCableSet` is a `ParamCollection`, so each cable amount is a full `AutoParam` (automatable/interpolatable). Q31 integer math throughout (`multiply_32x32_rshift32`); no float in the matrix. The same table is authoritative for audio, UI, automation, MIDI CC (via param ids), and XML file I/O.

**Strengths (grounded).** Cheap culling via the `sourcesChanged & sourcesPatchedToAnything` gate; a single automatable table; genuine two-level chaining (depth + modulator-param destinations); deterministic destination ordering (insertion-order independent); Q31 integer throughout.

**Limitations (grounded).** Hard 32-cable ceiling structurally tied to a 32-bit bitmask (guarded by `if constexpr (kMaxNumPatchCables > 32)` elsewhere); static array with a move-to-dynamic-memory TODO; only one level of depth chaining committed despite the 3-source descriptor; no cycle detection; global LFO-rate patching forbidden while synced; only 4 LFOs and 4 envelopes; per-voice `rangeFinalValues` scratch noted as a possible optimization left out.

### 4.4. Surge XT (GPL3)

A desktop/plugin synth. The matrix is three dynamically sized route lists in float, evaluated per control block on the audio thread.

**Slots.** No fixed/compile-time ceiling. Three `std::vector<ModulationRouting>` collections: per-scene `modulation_scene` and `modulation_voice`, plus patch-global `modulation_global`. Routes are appended in `setModDepth01` and erased on zero depth or via `clearModulation`; de-duplication is by the tuple `(source_id, source_scene, source_index, destination_id)`. `ModulationRouting` is `{ int source_id; int destination_id; float depth; bool muted; int source_index; int source_scene; }`.

**Sources — 41 values** (`enum modsources`, `n_modsources = 41`): `ms_original` (off), `ms_velocity`, `ms_keytrack`, `ms_polyaftertouch`, `ms_aftertouch`, `ms_pitchbend`, `ms_modwheel`, `ms_ctrl1..8` (macros), `ms_ampeg`, `ms_filtereg`, `ms_lfo1..6` (voice LFOs), `ms_slfo1..6` (scene LFOs), `ms_timbre`, `ms_releasevelocity`, `ms_random_bipolar`, `ms_random_unipolar`, `ms_alternate_bipolar`, `ms_alternate_unipolar`, `ms_breath`, `ms_expression`, `ms_sustain`, `ms_lowest_key`, `ms_highest_key`, `ms_latest_key`.

**Destinations.** No dedicated enum — `destination_id` is a **parameter id**: per-scene `param_id_in_scene` (273 params) for scene/voice routes, or the global param tag (219 params) for global routes. This includes oscillators, filters, all FX params, **and LFO and envelope parameters**, which is what makes modulator-into-modulator routing free. `isValidModulation` gates legality (float-only params; LFO/ENV destination rules).

**Amount.** Raw `float depth` in parameter units; the UI/normalized form is `f01 = depth / (val_max - val_min)` clamped to [−1, 1]. No per-route curve field — shaping lives per-source (`LFOStorage::deform`, `unipolar`) and per-destination (`deform_type`, `extend_range`). `depth == 0` means "route absent" (erased).

**Chaining.** Yes — expressed by pointing `destination_id` at an LFO's or envelope's parameter. `isValidModulation` forbids an LFO from self-modulating its own rate and restricts envelope destinations to non-envelope sources. Scene LFOs consume their modulated rate **same-block** (scene matrix applied before scene LFO processing); voice LFOs consume the **previous** block's localcopy (one-block latency) because they process before the voice matrix pass. Macros have an additional underlayer (`modunderlyer`) for macro-into-macro depth.

**LFOs.** 6 per-voice + 6 scene-level per scene (24 across 2 scenes). 10 shapes (`lt_sine`, `lt_tri`, `lt_square`, `lt_ramp`, `lt_noise`, `lt_snh`, `lt_envelope`, `lt_stepseq`, `lt_mseg`, `lt_formula`). Rate range 2^−7..2^9 Hz with per-LFO tempo-sync. Trigger modes: free-run / key-trigger / random. Multi-output: 3 outputs normally, 16 for the formula shape (`max_lfo_indices = 16`). Voice LFOs run in `SurgeVoice::calc_ctrldata` per voice per block, gated by `modsource_doprocess`; scene LFOs in `SurgeSynthesizer::process` per scene.

**Envelopes.** 2 per scene (amp + filter), ADSR with curve selectors and an analog model switch; not loopable. Loopable envelope-like modulation is provided by the `lt_envelope` and `lt_mseg` LFO shapes instead.

**Evaluation.** Per **control block**, not per sample (default block 32, oversampled 64). Scene and global matrices once per block in `SurgeSynthesizer::process`; the voice matrix once per block per voice in `calc_ctrldata` via a `localcopy` snapshot: `memcpy(localcopy, paramptr, ...)` then `+=` accumulation of every route (`localcopy[dst].f += depth * source->get_output(idx) * (1 - muted)`). `modsource_doprocess[n_modsources]` culls unused sources. Route vectors are mutated from the GUI thread under a `std::recursive_mutex` (`modRoutingMutex`).

**Implementation.** Fixed per-voice `pdata localcopy[273]` snapshot + dynamic heap route vectors. Per-voice source pointers in `std::array<ModulationSource*, n_modsources>`. The same `ModulationRouting` vectors serve audio, XML patch save/load, the UI, OSC, Python bindings, and undo; MIDI CC drives controller *sources*, not the matrix directly. Patch load bounds-checks `source_id`/`destination_id`/`source_scene`/`source_index`.

**Strengths (grounded).** Unified destination model makes chained modulation free and every modulateable float param automatable; three-tier scoping (voice/scene/global) matches the DSP topology; `modsource_doprocess` culling; full float depth resolution; `source_index` gives multi-output routing; no slot ceiling with de-duplication.

**Limitations (grounded).** No per-route curve (a source used at two depths cannot have two curves); `depth == 0` overloads "absent" and is lossy for undo round-trips; voice-LFO-rate modulation has one-block latency while scene-LFO is same-block (asymmetric ordering); dynamic heap storage with a recursive mutex per mutation; the full 273-entry `pdata` snapshot is memcpy'd per voice per block even for unmodulated patches; evaluation is control-rate (not sample-accurate); several special cases ride outside the matrix (VCA velocity, keytrack, filter EG amount).

### 4.5. Comparison Matrix

| Dimension | Ambika | DelugeFirmware | Surge XT |
|---|---|---|---|
| Slot model | Fixed, 14 rows + 4 modifier ops | Fixed, 32 cables | Dynamic, no cap (3 vectors) |
| Destination model | Closed enum, 19 | Open param-id (45 local + 10 global) | Open param-id (273 scene + 219 global) |
| Source count | 31 | 15 | 41 |
| Amount format | int8 (−63..63), byte-domain | Q31 int32 (±2^30) | float (raw param units) |
| Polarity/shaping | AC/unipolar per-source branch | Per-cable polarity + per-destination curve | Per-source/per-destination shaping |
| Chaining | Modifier ops + LFO4-rate destination (restricted) | Cable→cable depth (1 level) + modulator-param destinations | Modulator-param destinations (emergent) |
| LFOs | 4 (3 global controller-side + 1 per-voice) | 4 (2 global + 2 local) | 24 (12/scene: 6 voice + 6 scene) |
| Envelopes | 3 | 4 | 2/scene |
| Evaluation rate | ~980 Hz/block, per voice | per 128-sample block, per voice (local) + per sound (global) | per 32/64-sample block, per voice (voice) + per scene/global |
| Dirtiness/culling | None (all rows always) | `sourcesChanged` bitmask gate | `modsource_doprocess` gate |
| Placement | Voicecard (matrix); controller (LFO 1–3) | Single ARM core | Audio thread (GUI thread mutates under mutex) |
| Arithmetic | Byte/14-bit integer | Q31 integer | Float |

## 5. Conclusions

### 5.1. Cross-Cutting Patterns

**The destination model is the single most consequential choice.** Ambika's closed 19-value destination enum is small and cheap but requires hardcoded special cases whenever a route falls outside it (VCA, filter env/LFO, wheel-scaled depth). Deluge and Surge converge on the same answer independently: destinations are ordinary parameter ids, and the matrix is evaluated against the same param table the rest of the synth reads. That one decision is what makes chained modulation (LFO rate, envelope times, depth-of-depth) fall out for free in Deluge and Surge, and what Ambika has to bolt on piecemeal.

**"Modulation as data, not code" holds in all three.** Every implementation keeps the matrix as a data table that is simultaneously the patch-storage payload, the UI-edited state, and the runtime routing — never a set of hardcoded calls. The differences are only in the table's size, arithmetic, and whether it's fixed or dynamic.

**Evaluation is always control-rate, never per-sample, and always culled in the mature implementations.** Ambika is the outlier: it evaluates all 14 rows unconditionally every ~980 Hz block. Deluge (bitmask gate) and Surge (`modsource_doprocess` + destination masks) both skip work for unpatched sources and unchanged destinations.

### 5.2. Notable Divergences

**Fixed-point (Ambika, Deluge) vs. float (Surge).** The two embedded targets use integer math end-to-end (byte-domain on the AVR, Q31 on the Cortex-A9), while Surge — a desktop/plugin with a host FPU — uses raw float. This tracks the target hardware more than any modulation-specific requirement.

**Closed vs. open destination set.** Ambika's closed enum keeps the footprint to 88 bytes/voice but pays in special cases; Surge's open param-id space buys generality at the cost of a per-voice 273-entry float snapshot memcpy per block even for unmodulated patches. Deluge sits between: open param-id destinations but a fixed 32-cable array.

**Chaining depth is bounded everywhere.** Deluge encodes up to three source levels in its descriptor but commits only one; Ambika's chaining is two restricted mechanisms; Surge forbids self-modulation and envelope-sourced envelope destinations. None of the three implements arbitrary-depth or cycle-checked patch graphs — the depth ceiling is set by data structure, not by a solver.

**LFO placement splits on the control/audio boundary.** Ambika computes its three global LFOs on the *controller* and streams values over SPI (rate-limited, half-rate for LFO 2/3); Deluge and Surge compute all LFOs on the audio core. The controller-side approach only works for *global* LFOs — a per-voice LFO cannot cross the boundary efficiently, which is why Ambika's per-voice LFO 4 lives on the voicecard.

### 5.3. Implications

The open destination model (parameter-id destinations) is what separates the general implementations from the special-cased one: Deluge and Surge get chained modulation for free because a modulator's parameters are just more destinations, while Ambika must special-case every route that falls outside its closed enum. The fixed-point vs. float choice tracks the target's FPU, not modulation semantics. And the Deluge/Surge pattern — cull by a source-patchness bitmask and evaluate once per control block — is the observed cost floor: neither computes a source nobody routes. Per-voice LFOs are computed on the audio core in all three when per-voice behavior is required; Ambika's controller-side LFOs demonstrate the bandwidth cost of the alternative.

---
