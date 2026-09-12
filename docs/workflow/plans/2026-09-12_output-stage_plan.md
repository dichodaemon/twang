---
title: Output Stage -- Implementation Plan
status: approved
date: 2026-09-12
author: Dizan Vasquez
arch-design: ../arch-designs/output-stage_arch-design.md
brief: ../design-studies/2026-09-11_output-stage-headroom-and-distortion_design-study.md
---

# Output Stage — Implementation Plan

This plan sequences the implementation of the output stage described in the [Output Stage arch-design](../arch-designs/output-stage_arch-design.md): a per-voice drive/shaper (musicality) upstream of the bus sum, and a bus headroom/saturator/clamp/meter (protection) downstream of it. The companion design study ([2026-09-11_output-stage-headroom-and-distortion_design-study.md](../design-studies/2026-09-11_output-stage-headroom-and-distortion_design-study.md)) supplies the rationale and the measurement deliverable (§7); the arch-design is the settled "what".

## Implementation Status

**Phases:**

1. **Data model** — `ParamId::kDrive`, `ShaperState`, `CurveShape`, constants, the `kDrive`/`kAmp` descriptor entries, the meter field in `SharedIpc`, and the `test_params` update. No production logic; everything later depends on these types.
2. **DSP logic** — the curve (`CurveEval`/`AntiderivativeEval` + the `F`-table), `ShaperProcess` (ADAA), `DriveCurve`, and `EngineGetMeter`, plus the shaper unit tests. Depends on phase 1.
3. **Integration** — wire the shaper and the bus stage into `RenderBlock`, the `kDrive` matrix destination, the `drive_in_use` reset, and the render-level tests. Depends on phase 2.
4. **Measurement** — extend `bench` with the shaper stage and the per-evaluation AA-variant microbenchmark. Depends on phase 3.
5. **Verification & documentation** — full build, ctest, TSAN, WAV smoke, and the staleness audit. Depends on phase 4.

> **Target-gated deferral:** task 4.3 (M85 frame budget + meter cache-coherence) requires the M85 toolchain and is intentionally excluded from the desktop completion gate — phases 4 and 5 close without it. The study §7 `Measurement` checkbox is ticked with a "host microbenchmark + ordering done; M85 frame-budget pending task 4.3" note (task 5.3), and task 4.3 is not a `blocks` dependency of any phase-5 task.

| # | Task | Status |
|---|---|---|
| 1.1 | Add `ParamId::kDrive` to the enum and bump `kNumParams` 9 → 10 in `engine/engine.h`; update the `Part` struct comment to list `drive` and drop the stale `kNumParams` comment ("grows in phases 2-4") | Pending |
| 1.2 | Add `struct ShaperState { float xp; float Fp; }` and `Voice::shaper` in `engine/engine.h` | Pending |
| 1.3 | Add `enum class CurveShape : uint8_t { kSoftSat = 0 }` and declare `CurveEval`/`AntiderivativeEval` in `engine/engine.h` | Pending |
| 1.4 | Add `kBusGain`, `kAdaaEps`, `kFTableSize`, `kFTableMax` constants in `engine/engine.h` | Pending |
| 1.5 | Declare `ShaperProcess` and `EngineGetMeter` in `engine/engine.h`; update the `Render` doc comment | Pending |
| 1.6 | Add `std::atomic<float> meter` to `SharedIpc` in `engine/ipc_shared.h` (audio → control direction) with `static_assert(std::atomic<float>::is_always_lock_free)` | Pending |
| 1.7 | Add the `kDrive` entry to `g_params` in `engine/params.cc` (name "drive", unit "dB", disp 0..+20, def 0, kExponential, comb kAdditive) | Pending |
| 1.8 | Change `kAmp` default 0.25 → 1.0 in `engine/params.cc` (headroom moves from `kAmp` to `kBusGain`) | Pending |
| 1.9 | Update `tests/test_params.cc`: change the `kAmp` `def` assertion 0.25 → 1.0 (and its message); add a `kDrive` descriptor assertion (offset == `offsetof(Part, params) + 9 * sizeof(float)`, pinning params index 9) | Pending |
| 1.10 | Verify: `cmake --build build` succeeds and `./build/test_params` passes (new enum/descriptor/default) | Pending |
| 2.1 | Implement `CurveEval`/`AntiderivativeEval` and the 256-entry `F`-table (`log(cosh(x))` over [−8, 8], `|x| − log 2` outside) in `engine/engine.cc` | Pending |
| 2.2 | Implement `ShaperProcess` (first-order ADAA with the `ε` midpoint fallback) in `engine/engine.cc` | Pending |
| 2.3 | Implement `DriveCurve` (gain mapping, `DriveCurve(0) = 1`) in `engine/engine.cc` | Pending |
| 2.4 | Implement `EngineGetMeter` (read-and-clear `exchange(0)`, relaxed) in `engine/engine.cc` | Pending |
| 2.5 | Verify: `cmake --build build` succeeds | Pending |
| 2.6 | Write test: ADAA correctness, `ε` precision, DC-input bounds, DC-gain unity, and ADAA group delay in new `tests/test_shaper.cc`; add the `test_shaper` target in `CMakeLists.txt` and append it to the `test-tsan.sh` `--target` list | Pending |
| 2.7 | Verify: `./build/test_shaper` passes | Pending |
| 3.1 | Add `kDrive` to the matrix destination switch and accumulate/clamp `drive_eff` in `RenderBlock`; update the switch's stale "grows in phases 2-4" comment (`engine/engine.cc`) | Pending |
| 3.2 | Allow `kDrive` as a destination in `EngineSetRoute`; update the `dst` doc comment in `engine/engine.h` and the "phase-1 destination" body comment in `engine/engine.cc` (drop the "phase-1" enumeration) | Pending |
| 3.3 | Wire the per-voice shaper (depth/gain/blend) into the voice loop in `RenderBlock` (`engine/engine.cc`) | Pending |
| 3.4 | Compute `drive_in_use` per part and reset `xp`/`Fp` on the false → true transition (`engine/engine.cc`) | Pending |
| 3.5 | Reset `xp = Fp = 0` in `StartNote` (`engine/engine.cc`) | Pending |
| 3.6 | Replace the final `Clamp` with the bus gain → tanh saturator → hard clamp → meter loop in `RenderBlock` (`engine/engine.cc`) | Pending |
| 3.7 | Zero the meter and the shaper state in `EngineInit`; update the stale "4-voice headroom carried in `kAmp` (0.25)" default-route comment (`engine/engine.cc`) | Pending |
| 3.8 | Verify: `cmake --build build` succeeds | Pending |
| 3.9 | Write test: bypass, state reset, drive enable, bus protection, and migration (4× level-scaling, per Design Decision 8) in `tests/test_engine.cc` | Pending |
| 3.10 | Verify: `./build/test_engine` passes | Pending |
| 4.1 | Add the shaper/ADAA stage to the `bench --breakdown` output in `tools/bench.cc` | Pending |
| 4.2 | Add a per-evaluation microbenchmark of the AA variants (plain tanh / ADAA 1D table / ADAA closed-form / 2× oversampling) in `tools/bench.cc`, emitted by `--breakdown` | Pending |
| 4.3 | (target-hardware-gated) Measure the M85 frame budget via `tools/bench.cc` on the target and confirm the `SharedIpc::meter` cache-coherence in `engine/ipc_shared.h` — deferred, requires the M85 toolchain | Pending |
| 4.4 | Verify: `./build/bench --breakdown 5` shows the §5.7 per-evaluation ratios (plain tanh ≈ 1×, tabulated ADAA ≈ 2.9×, closed-form ≈ 10×) and the newly-measured oversampling per-evaluation falling between tabulated and closed-form (the §5.7 voice-level projection orders them tabulated +69% < oversampling ~+141% < closed-form +236%); `./build/bench 5 24` reports full-voice `ns/sample/voice` in line with the §5.7 projection (~+69% for the tabulated-ADAA shaper); the M85 frame-budget check is task 4.3 | Pending |
| 5.1 | Verify: `./build.sh` (full build + ctest) passes | Pending |
| 5.2 | Verify: `./test-tsan.sh` passes (the new `std::atomic<float>` meter is race-free) | Pending |
| 5.3 | Update the design study §7 deliverable checkboxes in `docs/workflow/design-studies/2026-09-11_output-stage-headroom-and-distortion_design-study.md`: tick `Implementation` and `Test`; tick `Measurement` with a note "host microbenchmark + ordering done; M85 frame-budget pending task 4.3" | Pending |
| 5.4 | Update `docs/workflow/arch-designs/synth-routing_arch-design.md` §5.3/§5.4/§12 (headroom now lives in `kBusGain` 0.125; `kAmp` default 1.0) and `README.md`'s `Render` API one-liner ("sum of active voices, clamped" → "soft-saturated then clamped") | Pending |
| 5.5 | Verify: `./build/wav_render 1 /tmp/out.wav` produces a finite, non-silent render (peak ~0.13, consistent with the 6 dB headroom drop); the meter is verified by the bus-protection test (task 3.10); `grep -n "kAmp.*default 0.25" docs/workflow/arch-designs/synth-routing_arch-design.md` returns no matches (task 5.4) and the design-study §7 `Implementation`/`Measurement`/`Test` checkboxes read `[x]` (task 5.3) | Pending |

## Architecture

### Directory Layout

| File | Change |
|---|---|
| `engine/engine.h` | Add `ParamId::kDrive`, `ShaperState` + `Voice::shaper`, `CurveShape`, `CurveEval`/`AntiderivativeEval`, `ShaperProcess`, `EngineGetMeter`, the four constants; bump `kNumParams`; update the `Render` doc comment |
| `engine/engine.cc` | Implement `CurveEval`/`AntiderivativeEval` (+ `F`-table), `ShaperProcess`, `DriveCurve`, `EngineGetMeter`; wire the shaper and bus stage into `RenderBlock`; `drive_in_use` + reset; `StartNote` reset; `EngineInit` reset; `EngineSetRoute` `kDrive` |
| `engine/params.cc` | Add the `kDrive` descriptor; change `kAmp` default to 1.0 |
| `engine/ipc_shared.h` | Add `std::atomic<float> meter` to `SharedIpc` (+ lock-free `static_assert`) |
| `tests/test_params.cc` | Update the `kAmp` `def` assertion (0.25 → 1.0); add a `kDrive` descriptor assertion (offset == params index 9) |
| `tests/test_shaper.cc` | New file: ADAA correctness, `ε` precision, DC-input, DC-gain unity, group-delay unit tests |
| `tests/test_engine.cc` | Add bypass, state reset, drive enable, bus protection, migration render tests |
| `tools/bench.cc` | Add the shaper stage to `--breakdown`; add the AA-variant microbenchmark |
| `CMakeLists.txt` | Add the `test_shaper` executable and `add_test` |
| `test-tsan.sh` | Append `test_shaper` to the TSAN build `--target` list |
| `docs/workflow/design-studies/2026-09-11_output-stage-headroom-and-distortion_design-study.md` | Tick §7 `Implementation`/`Measurement`/`Test` checkboxes |
| `docs/workflow/arch-designs/synth-routing_arch-design.md` | Update §5.3/§5.4/§12 (headroom moves from `kAmp` default 0.25 to `kBusGain` 0.125; `kAmp` default 1.0) |
| `README.md` | Update the `Render` API one-liner ("sum of active voices, clamped" → "soft-saturated then clamped") |

### Dependency Graph

No new inter-package edges. All production changes land in the existing `engine` library; `test_shaper`, `test_engine`, and `bench` already link against it. The only build-file change is the new `test_shaper` target.

## Interface Changes

### `ParamId` enum and `kNumParams` (`engine/engine.h`)

```cpp
enum class ParamId : std::uint8_t {
    kCutoff = 0, kResonance, kAttack, kDecay, kSustain, kRelease, kAmp,
    kPitchCoarse, kPitchBend,
    kDrive,             // params[9]: normalized [0,1], the drive blend depth
    kKeyFollowDepth,    // named field (not params[]); addressed via offsetof
    kCount,
};
// kNumParams 9 -> 10
```

`kDrive` is inserted before `kKeyFollowDepth` so it is a `params[]` member (offset 9), not a named field. `kNumParams` becomes 10.

### `ShaperState` and `Voice::shaper` (`engine/engine.h`)

```cpp
struct ShaperState {
    float xp;   // x[n-1]: previous shaper input
    float Fp;   // F(x[n-1]): previous antiderivative value
};

struct Voice {
    // ... existing fields ...
    ShaperState shaper;  // ADAA state; reset on note-on, steal, and drive enable
};
```

### `CurveShape` and the dispatch (`engine/engine.h`)

```cpp
enum class CurveShape : std::uint8_t { kSoftSat = 0 };
float CurveEval(CurveShape s, float x);           // f(x) = tanh(x)
float AntiderivativeEval(CurveShape s, float x);  // F(x) = log(cosh(x)) via table + asymptote
```

Declared now so a future curve set slots into the same dispatch without touching the shaper or bus code.

### Constants (`engine/engine.h`)

```cpp
inline constexpr float kBusGain    = 0.125f;  // −18 dB headroom
inline constexpr float kAdaaEps    = 1e-3f;   // precision guard (re-derive in Q31)
inline constexpr int   kFTableSize = 256;     // antiderivative table entries
inline constexpr float kFTableMax  = 8.0f;    // table covers x ∈ [−8, 8]
```

### `ShaperProcess` (new, `engine/engine.h`)

```cpp
float ShaperProcess(Voice *v, float x);
```

Precondition: `x = lp × gain`. Postcondition: returns the ADAA-anti-aliased `f(x)` and advances `v->shaper` to `{x, F(x)}`. Declared public (not file-local) so `test_shaper.cc` can drive it directly; the arch-design §9 already treats it as a contract.

### `EngineGetMeter` (new, `engine/engine.h`)

```cpp
float EngineGetMeter();
```

Returns the peak pre-saturator magnitude since the last read and clears it (`exchange(0)`, relaxed). The control-side read of `Shared().meter`.

### `SharedIpc::meter` (new field, `engine/ipc_shared.h`)

```cpp
struct SharedIpc {
    EventRing events;        // control -> audio (SPSC)
    ParamBlock params;       // control writes, audio snapshots
    std::atomic<float> meter;  // audio writes, control reads-and-clears (reverse direction)
};
```

The meter is the first audio → control signal; it lives in the same shared region as `events`/`params`, not as a file-scope `float`. On the target it is at the fixed `kSharedIpcAddr`; on the desktop it is the static `Shared()` instance.

### `g_params[kDrive]` (`engine/params.cc`)

```cpp
[static_cast<std::size_t>(ParamId::kDrive)] =
    { "drive", "dB", 0.0f, 20.0f, 0.0f, ParamCurve::kExponential,
      offsetof(Part, params) + 9 * sizeof(float),
      CombinationClass::kAdditive },
```

The lowercase name `"drive"` follows the existing `g_params` convention (`"cutoff"`, `"amp"`, `"pitchbend"`); the arch-design §8 spells the human-readable name `"Drive"`.

### `kAmp` default (`engine/params.cc`)

`kAmp`'s `def` changes 0.25 → 1.0 (its headroom role moves to `kBusGain`). The migration is output-preserving at the same scale: `kAmp 0.25 × 1.0` ≡ `kAmp 1.0 × bus_gain 0.25` below the rail. The shipped `bus_gain` is 0.125, a deliberate 6 dB quieter than today. The migration test verifies this as a ratio with the shipped `kBusGain` held fixed: `kAmp 0.25` vs `kAmp 1.0` differ by ≈4× below the rail (tanh is near-linear there; the headroom lives entirely on the bus, not the level; see Design Decision 8).

**Note:** the net 6 dB drop (0.25 → 1.0 × 0.125) lowers the render output — a single vel-127 note now peaks ~0.13 instead of ~0.26. The existing `test_engine.cc` absolute thresholds (`peak > 0.1`, `rms > 0.01`) become marginal; task 3.9 re-derives them alongside the new tests.

## Solution Breakdown

### 1.1–1.9 — Data model

The type, constant, and descriptor changes are fully specified in [Interface Changes](#interface-changes): `ParamId::kDrive` + `kNumParams = 10`, `ShaperState` in `Voice`, `CurveShape` + the dispatch, the four constants, `ShaperProcess`/`EngineGetMeter` declarations, `SharedIpc::meter`, and the `g_params[kDrive]`/`kAmp` descriptor entries. Each lands as a declaration or initializer — no logic. Task 1.9 updates `test_params` to match the new `kAmp` default and to pin the `kDrive` descriptor offset to params index 9.

**Done condition:** `cmake --build build` succeeds and `./build/test_params` passes (the new enum, descriptor, and `kAmp` default) — task 1.10.

### 2.1 / 2.2 — The curve and ADAA (`CurveEval`, `AntiderivativeEval`, `ShaperProcess`)

`f(x) = tanh(x)`; `F(x) = log(cosh(x))`. `AntiderivativeEval` looks up a `static constexpr float kFTable[kFTableSize]` over `x ∈ [−8, 8]` with linear interpolation; for `|x| > 8` it returns `|x| − log 2` (the asymptote — exact to float precision, and *required*, since clamping the table would freeze `F` and drive the ADAA quotient to 0 where `tanh` is saturated).

`ShaperProcess`:

```
dx = x − xp
if |dx| < kAdaaEps:  y = CurveEval(CurveShape::kSoftSat, (x + xp) / 2)   // midpoint fallback
else:                y = (F(x) − Fp) / dx
xp = x;  Fp = F(x)
return y
```

**Edge cases:** `|dx| < ε` (low-frequency or DC input) → midpoint fallback, no divide; `|x| > 8` → closed-form `F`, no NaN; `dx == 0` exactly → the fallback fires before the divide.

**Dependencies:** produces `CurveEval`/`AntiderivativeEval`/`ShaperProcess` for phase 3; requires phase 1 types/constants.

**Done condition:** `test_shaper` ADAA and DC cases pass (task 2.7).

### 2.3 — `DriveCurve`

```cpp
float DriveCurve(float drive_eff);  // file-local
```

`depth = drive_eff` (the blend amount, directly the normalized value); `gain = DriveCurve(drive_eff)` with `DriveCurve(0) = 1`. Starting implementation: `gain = exp2f(drive_eff * log2f(10.0f))` — a 1→10 (20 dB) span. The exact curve and ceiling are tuning (arch-design §8); this pins a sane starting point.

**Done condition:** `gain = 1` at `drive_eff = 0` and monotonic — exercised by the bypass (bit-identical at depth 0) and drive tests (tasks 2.7, 3.10).

### 2.4 — `EngineGetMeter`

```cpp
float EngineGetMeter() {
    return Shared().meter.exchange(0.0f, std::memory_order_relaxed);
}
```

**Done condition:** returns 0 after `EngineInit` and > 1 when the rail is driven — verified by the bus-protection test (task 3.10).

### 2.5 / 2.6 / 2.7 — Build gate and `test_shaper`

Task 2.5 is the phase-2 build gate. Task 2.6 writes `tests/test_shaper.cc` (ADAA correctness against a float64 `log(cosh)` reference, `ε` precision at 1e-3 vs 1e-6, DC-input bounds, small-signal DC-gain unity — `tanh'(0) = 1` — and the bounded ~half-sample ADAA group delay) and registers `test_shaper` in `CMakeLists.txt` + the `test-tsan.sh` `--target` list. Task 2.7 runs it.

**Done condition:** `./build/test_shaper` passes — task 2.7.

### 3.1 / 3.2 — `kDrive` in the matrix

In `RenderBlock`'s route loop, add `case ParamId::kDrive: acc = &drive_eff; break;` alongside `kAmp`/`kCutoff`/`kPitchCoarse`, then clamp `drive_eff` to `[0, 1]` after the loop (matching the `cutoff_eff` clamp) and derive `depth = drive_eff` and `gain = DriveCurve(drive_eff)` per control step (consumed by task 3.3); update the switch's stale "grows in phases 2-4" comment. In `EngineSetRoute`, add `case ParamId::kDrive:` to the allowed-destination switch so a stored drive route is never silently rejected, and update the body's "phase-1 destination" comment.

**Done condition:** build succeeds (task 3.8) and the drive-enable test passes (task 3.10).

### 3.3 — Per-voice shaper in the voice loop

Replace `g_buses[0].L[start + i] += lp * amp_eff;` with, per voice:

```
if (drive_in_use[voice->part]) {
    wet = ShaperProcess(voice, lp * gain);
    g_buses[0].L[start + i] += lerp(lp, wet, depth) * amp_eff;
} else {
    g_buses[0].L[start + i] += lp * amp_eff;
}
```

`lerp(a, b, t) = a + (b − a)·t` is a one-line file-local helper (added alongside `ShaperProcess`). `depth`/`gain` are derived from `drive_eff` (per-voice, since an envelope route makes it per-voice). At `drive_eff = 0` the blend is the identity, so the bypass and unity-drive paths are bit-identical.

**Done condition:** bypass and drive tests pass (task 3.10).

### 3.4 — `drive_in_use` and the enable reset

Per control step, compute `bool drive_in_use[kNumParts]` (`kDrive != 0` or any route targets `kDrive`). Track `static bool g_prev_drive_in_use[kNumParts]`; on a false → true transition, reset `xp = Fp = 0` for every voice bound to that part. A ramped enable needs no reset (`depth ≈ 0` masks the stale wet term), but a jumped enable (preset load, CC 0 → 100) would otherwise resume from stale state and click.

**Done condition:** drive-enable test passes (task 3.10).

### 3.5 — `StartNote` reset

Add `v->shaper.xp = 0.0f; v->shaper.Fp = 0.0f;` to `StartNote` (which `StealNote` reaches after its ramp), so a voice's first sample is always computed against silence.

**Done condition:** state-reset (voice-steal) test passes (task 3.10).

### 3.6 — Bus protection in `RenderBlock`

Replace the final `for (i) out[i] = Clamp(g_buses[0].L[i]);` with:

```
float block_peak = 0.0f;
for (int i = 0; i < frames; ++i) {
    float s = g_buses[0].L[i] * kBusGain;
    float a = std::fabs(s);
    if (a > block_peak) block_peak = a;
    out[i] = Clamp(std::tanhf(s));          // saturator, then hard clamp
}
// block end: CAS-max block_peak into Shared().meter (relaxed)
```

The meter reads the *pre*-saturator magnitude (`|s|`), so 1.0 means "at the rail" and > 1.0 means the saturator is compressing. The CAS-max keeps the arch-design §8 NaN guard (`block_peak > cur`): a NaN peak fails the comparison and is dropped, so it can never poison the meter.

**Done condition:** bus-protection and migration tests pass (task 3.10).

### 3.7 — `EngineInit` reset

Zero `Shared().meter` and, since voices are zero-initialized already, confirm `shaper` starts at `{0, 0}` (it does — `Voice{}` value-initializes the floats).

**Done condition:** build succeeds (task 3.8).

### 3.8 / 3.9 / 3.10 — Build gate and render-level tests

Task 3.8 is the phase-3 build gate. Task 3.9 extends `tests/test_engine.cc` with the bypass (bit-identical at depth 0), voice-steal state reset, discontinuous drive-enable, bus-protection (meter > 1), and migration (`kAmp 0.25` vs `1.0` ≈ 4× below the rail at the shipped `kBusGain 0.125` — the shipped-code-realizable form of the headroom move, per Design Decision 8) cases, re-deriving the peak/rms thresholds for the 6 dB headroom drop. Task 3.10 runs it.

**Done condition:** `./build/test_engine` passes — task 3.10.

### 4.1 / 4.2 — Measurement

Extend `bench`'s `RunBreakdown` with a shaper/ADAA stage, and add a microbenchmark (emitted by `--breakdown`) that times a single evaluation of each AA variant (plain tanh, ADAA 1D table, ADAA closed-form, 2× oversampling). §5.7 measures the tabulated (2.9×) and closed-form (10×) per-evaluation ratios; the oversampling per-evaluation figure is *not* in §5.7 (only its voice-level "+141% modelled" projection), so task 4.2 measures it here and confirms it falls between tabulated and closed-form (the `log1p`/`exp` penalty is libm-dependent).

**Done condition:** `./build/bench --breakdown 5` shows the §5.7 per-evaluation ratios (plain tanh ≈ 1×, tabulated ADAA ≈ 2.9×, closed-form ≈ 10×) and the newly-measured oversampling per-evaluation between tabulated and closed-form; `./build/bench 5 24` reports full-voice `ns/sample/voice` in line with the §5.7 projection (~+69% for the tabulated-ADAA shaper) — task 4.4.

### 4.3 — Target-gated M85 frame budget

Task 4.3 measures the engine's frame budget on the M85 target (`tools/bench.cc` at 24 voices, expressed as a fraction of the 48 kHz frame budget) and confirms the `SharedIpc::meter` cache-coherence between the M85's L1 D-cache and the M33's access (arch-design §5). It is **target-hardware-gated**: it requires the M85 toolchain, so it is not a `blocks` dependency of any phase-5 task and phases 4 and 5 close on the host without it.

**Done condition:** not closable on the host — deferred until the M85 toolchain is available. The host portion of the study §7 `Measurement` deliverable (microbenchmark + ordering) is gated by task 4.4; the M85 frame-budget portion stays open and is noted in the study §7 checkbox (task 5.3).

### 5.1 / 5.2 / 5.5 — Verification gates

Task 5.1 runs the full build + ctest (`./build.sh`); task 5.2 runs `./test-tsan.sh` (the new `std::atomic<float>` meter is race-free); task 5.5 renders a WAV smoke (`./build/wav_render 1 /tmp/out.wav`) and confirms the render is finite, non-silent, and well under the rail after the 6 dB headroom drop.

**Done condition:** `./build.sh` and `./test-tsan.sh` exit 0, `wav_render` produces a finite, non-silent render, and the doc-content checks in task 5.5 pass (no stale "`kAmp.*default 0.25`" in the synth-routing arch-design; study §7 checkboxes `[x]`) — tasks 5.1/5.2/5.5.

### 5.3 / 5.4 — Documentation updates

After the host deliverables land, tick the study §7 `Implementation` and `Test` checkboxes, and tick `Measurement` with a note that the host portion (microbenchmark + ordering) is done while the M85 frame-budget portion (task 4.3) stays open until target hardware is available. Task 5.4 updates `synth-routing_arch-design.md` §5.3/§5.4/§12 — the "headroom in `kAmp` default 0.25" statements become "headroom in `kBusGain` (0.125); `kAmp` default 1.0" — and `README.md`'s `Render` API one-liner ("sum of active voices, clamped" → "soft-saturated then clamped") so neither document goes stale when the output stage lands.

**Done condition:** the study §7 `Implementation`/`Test`/`Measurement` checkboxes read `[x]` and `synth-routing_arch-design.md` §5.3/§5.4/§12 no longer state "`kAmp` default 0.25" — confirmed by the doc-content checks in task 5.5.

## Design Decisions

1. **`F`-table as a `static constexpr` literal array.** 256 precomputed `log(cosh(x))` values pasted as a literal, not a runtime-computed `static` (which would land in `.data`, not flash) and not a `constexpr` computed table (C++17 has no `constexpr` `std::log`/`std::cosh`). The values are generated once (a Python/numpy one-liner emits the literals); the array is the authored source of truth. The literals are validated indirectly by the `test_shaper` SNR test (task 2.6), which checks the shaper against a float64 closed-form `log(cosh)` reference — a wrong table entry would fail the ≥ ~90 dB bar.

2. **`depth = drive_eff` directly; `gain = exp2f(drive_eff · log2(10))`.** The blend amount is the raw normalized value (this is what makes depth 0 exactly transparent); the gain is a separate 1→10 curve with unity at 0. The exact gain span is tuning; this pins 20 dB as a starting point.

3. **Meter in `SharedIpc::meter`, not a file-scope global.** The arch-design names it `g_meter`; the codebase's shared IPC object is `SharedIpc`, so the meter is a field there (audio → control, the first reverse-direction signal). `EngineGetMeter()` is the only reader.

4. **`ShaperProcess`/`CurveEval`/`AntiderivativeEval` declared in `engine.h`.** The arch-design §13 lists them under `engine.cc`, but §9 treats `ShaperProcess` as a public contract and §11 tests it directly. Declaring them public lets `test_shaper.cc` drive a pure sine into the shaper for the SNR/aliasing measurements; a file-local implementation would force those tests through the full render (envelope + filter noise in the way).

5. **`drive_in_use` computed per control step per part** (scan `kDrive` + 16 routes), with `g_prev_drive_in_use[kNumParts]` for transition detection. Drive routes change only at control rate, so a 4-part × 16-route scan per control step is negligible; caching would add a second source of truth that can go stale.

6. **New `tests/test_shaper.cc` for the DSP unit tests.** The SNR/aliasing helpers (float64 reference ADAA, narrowband energy) don't belong in the monolithic `test_engine.cc` render smoke test; a dedicated unit-test file keeps `test_engine.cc` for render-level behavior.

7. **Parameter name `"drive"` (lowercase), not the arch-design §8 `"Drive"`.** The `g_params` table convention is lowercase names (`"cutoff"`, `"amp"`, `"pitchbend"`); the arch-design's `"Drive"` is the human-readable form. The descriptor uses lowercase to match its siblings; a UI display layer can capitalize for presentation.

8. **Migration test asserts ≈4× level-scaling (the shipped-code-realizable form of the headroom move).** The study §5.3 states the migration is output-preserving — "`kAmp 0.25` ≡ `kAmp 1.0 × bus_gain 0.25` (ratio 1.0000)". That identity holds only at a bus gain of 0.25, the pre-drop headroom value. The shipped `kBusGain` is 0.125 (a deliberate 6 dB quieter — study §5.3), and `kBusGain` is `constexpr`, so the shipped binary cannot reproduce the ratio-1.0000 comparison without a special build. The shipped-code-realizable form of the migration fact is that `kAmp` is now pure level: at the fixed shipped bus gain, `kAmp 0.25` vs `kAmp 1.0` differ by ≈4× below the rail (tanh is near-linear there, so the test uses a ~1% tolerance). Task 3.9 asserts that scaling; the arch-design §11/§12 have been updated to this 4× form, and the ratio-1.0000 output-preservation is its corollary at the equal-headroom value, covered by the study's own §5.3 measurement (a single velocity-127 voice peaks at 0.257671 in both `kAmp 0.25` and `kAmp 1.0 × bus_gain 0.25`).

## Success Criteria

### Data model
- [ ] `test_params` passes after the `kDrive` enum/descriptor and `kAmp` default changes (task 1.10)
- [ ] `kDrive`'s descriptor offset is `offsetof(Part, params) + 9 * sizeof(float)` (params index 9), asserted in `test_params` (task 1.9, run at task 1.10)

### Shaper
- [ ] A sine through `ShaperProcess` shows folded-back-vs-harmonic energy within ~1 dB of the study §5.2/§5.4 figures at ×3 and ×10 drive (task 2.6/2.7)
- [ ] 30–110 Hz sines hold ≥ ~90 dB SNR against a float64 reference at `ε = 1e-3` (task 2.6/2.7)
- [ ] DC input is bounded and NaN-free (task 2.6/2.7)
- [ ] Small-signal DC gain of the wet path is unity (`tanh'(0) = 1`) (task 2.6/2.7)
- [ ] ADAA group delay is bounded and ~half-sample (measured, not assumed) (task 2.6/2.7)

### Render integration
- [ ] `kDrive = 0` + no drive route → output bit-identical to the shaper-removed path (task 3.9/3.10)
- [ ] Voice steal → no impulse discontinuity on the first sample (task 3.9/3.10)
- [ ] Discontinuous drive enable mid-note → no impulse discontinuity (task 3.9/3.10)
- [ ] Bus over the rail → `out` in [−1, 1] and `EngineGetMeter() > 1` (task 3.9/3.10)
- [ ] `kAmp 0.25` vs `kAmp 1.0` differ by ≈4× below the rail (headroom is on the bus, not the level) (task 3.9/3.10)

### Measurement & verification
- [ ] `bench --breakdown 5` shows the §5.7 per-evaluation ordering and ratios (plain tanh ≈ 1×, tabulated ADAA ≈ 2.9×, closed-form ≈ 10×); `bench 5 24` reports full-voice `ns/sample/voice` in line with the §5.7 projection (~+69%) (task 4.4)
- [ ] `./build.sh` and `./test-tsan.sh` pass (tasks 5.1/5.2)
- [ ] `wav_render` produces a finite, non-silent render with a single voice well under the rail (task 5.5)

## Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/output-stage_arch-design.md` | No — it is the companion; the plan implements it, not changes it. (The §4/§8/§13 symbol `g_meter` is realized as `SharedIpc::meter` per Design Decision 3 — a naming realization, not a contract change.) §11/§12's migration criterion has been reconciled from the "ratio 1.0000" wording (`kAmp 1.0 × bus_gain 0.25`) to the shipped ≈4× level-scaling form (`kBusGain` 0.125) — updated in the companion. | None. |
| `../design-studies/2026-09-11_output-stage-headroom-and-distortion_design-study.md` | Yes — §7 `Implementation`/`Test` checkboxes are `[ ]` and become `[x]`; `Measurement` becomes `[x]` with a note that the M85 frame-budget portion is pending (task 4.3). | Task 5.3. |
| `../arch-designs/synth-routing_arch-design.md` | Yes — §5.3/§5.4/§12 state "headroom in `kAmp` default 0.25"; the plan moves headroom to `kBusGain` (0.125) and sets `kAmp` default to 1.0. (§8 `Render` postcondition already defers the clamp to the Output Stage.) | Task 5.4. |
| `README.md` (`Render` API one-liner) | Yes — says "sum of active voices, clamped"; becomes "soft-saturated then clamped". | Task 5.4. |
| `engine/engine.h` (`Render` doc comment) | Yes — says "clamped to [−1, 1]"; becomes "soft-saturated then clamped". | Task 1.5. |
| `engine/engine.h` (`EngineSetRoute` doc comment) | Yes — "phase-1 set: kCutoff, kAmp, kPitchCoarse" omits `kDrive`. | Task 3.2. |
| `engine/engine.h` (`Part` struct comment) | Yes — enumerates the `params[]` bank but omits `drive`. | Task 1.1. |
| `engine/engine.cc` (matrix-switch comment) | Yes — "the switch only selects which accumulator a destination folds into (grows in phases 2-4)" omits `kDrive`. | Task 3.1. |
| `engine/engine.cc` (`EngineSetRoute` body comment) | Yes — "dst must be a phase-1 destination the matrix folds" omits `kDrive`. | Task 3.2. |
| `engine/engine.cc` (`EngineInit` default-route comment) | Yes — "4-voice headroom carried in the `kAmp` base level (default 0.25)". | Task 3.7. |

## Cleanup

No diagnostic instrumentation is added. The AA-variant microbenchmark in `bench` (task 4.2) is a permanent measurement tool, not temporary instrumentation.
