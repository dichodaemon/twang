---
title: Synth Routing (Phase 1: Foundation) -- Implementation Plan
status: approved
date: 2026-09-11
author: Dizan Vasquez
arch-design: ../arch-designs/synth-routing_arch-design.md
---

# Synth Routing — Phase 1 (Foundation)

This plan is the *how* for **Phase 1 of the build order** in the arch-design's Appendix A (`../arch-designs/synth-routing_arch-design.md`). The *what* and *why* are the arch-design and its design study (`../design-studies/2026-09-10_modulation-matrix-and-routing_design-study.md`); this plan sequences the work and names the files without duplicating their contracts or acceptance criteria.

Phase 1 scope (Appendix A): generalized part-state transport, `ModSourceId`/`ModRoute`/`ParamId`, matrix evaluation at control rate, source gating, bus indirection, and the 5 default routes (velocity→amp, env0→amp, env1→cutoff, key follow `kNote → cutoff`, pitchbend `kPitchBend → osc pitch coarse`). Phases 2–4 (LFOs, envelopes ×3 + chaining, audio routing) are **out of scope** and land as separate plans.

## 1. Phases

1. **Data model & transport** — types, enums, `Part` generalization, `ParamBlock` generalization. No behavior change (no deps).
2. **Matrix evaluation + cutover** — control-rate matrix, default routes, bus indirection, deletion of the hardcoded modulation, matrix route tests. Depends on phase 1.
3. **Callers, tests, verification** — update tools + existing tests, full build + user aural sign-off. Depends on phase 2.

## 2. Implementation Status

| # | Task | Status |
|---|---|---|
| 1.1 | Add `ModSourceId`, `ModRoute`, `CombinationClass` + `kModSlots`, `kNumParams`, `kNumBuses` constants to `engine/engine.h` | Pending |
| 1.2 | Extend `ParamId` (add `kAmp`, `kPitchCoarse`, `kKeyFollowDepth`, `kPitchBend`) + add `CombinationClass` to `ParamDesc` in `engine/params.h`; populate the descriptor table in `engine/params.cc` | Pending |
| 1.3 | Generalize `Part` in `engine/engine.h` to `float params[kNumParams]` + `key_follow_depth` + `routes[kModSlots]`; retarget `ParamDesc::offset` in `engine/params.cc` | Pending |
| 1.4 | Generalize `ParamBlock` in `engine/ipc.h` + `engine/ipc.cc` to double-buffered `Part` structs; add `ParamBlock::SetRoute`; declare + implement the public `EngineSetRoute` wrapper (`engine/engine.h` + `engine/engine.cc`) | Pending |
| 1.5 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` green (all existing tests pass; behavior unchanged); `cmake -B /tmp/twang-build-tsan -DTWANG_ENABLE_TSAN=ON . && cmake --build /tmp/twang-build-tsan --target test_split && /tmp/twang-build-tsan/test_split` race-free | Pending |
| 2.1 | Implement control-rate matrix evaluation in `engine/engine.cc` (gate sources, advance, accumulate 16 routes per class, write effective amp/cutoff/pitch) | Pending |
| 2.2 | Add `vel`/`note`/`key_follow`/`gate` fields to `Voice` in `engine/engine.h`; latch per-note sources at `StartNote` in `engine/engine.cc` (vel, note, key-follow octaves, gate; steal path latches `steal_vel`) | Pending |
| 2.3 | Pre-populate the 5 default routes in `EngineInit` (`engine/engine.cc`) | Pending |
| 2.4 | Bus indirection: `Bus` + `g_buses[kNumBuses]` in `engine/engine.cc`; route voices through `g_buses[0]`, produce mono `out` | Pending |
| 2.5 | Delete `Voice::gain`/`steal_gain`, `kVoiceHeadroom`, `VelocityToGain`, `Part::filter_env_amount` + `kFilterEnvAmount`; make `UpdateFilterCoeffs` consume the effective cutoff | Pending |
| 2.6 | Write test: `tests/test_mod_route.cc` (route behavior, empty slot, zero amount, key follow, pitchbend, source gating) and register `test_mod_route` in `CMakeLists.txt` | Pending |
| 2.7 | Verify: `cmake --build /tmp/twang-build --target engine test_mod_route && /tmp/twang-build/test_mod_route` — all checks pass | Pending |
| 3.1 | Add pitchbend (`0xE0`) → `kPitchBend` routing in `engine/midi.cc` | Pending |
| 3.2 | Update `tools/wav_render.cc`, `tools/live_render.cc`, `tools/bench.cc`: `EngineSetParam(kFilterEnvAmount, …)` → `EngineSetRoute(… env1→cutoff …)` | Pending |
| 3.3 | Update `tests/test_params.cc`, `tests/test_param_block.cc`, `tests/test_midi.cc` for the removed/added params + pitchbend dispatch | Pending |
| 3.4 | Verify: full `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` green | Pending |
| 3.5 | Verify (migration gate): `wav_render` before/after — **user aural sign-off** that the default-route sound matches today's | Pending |

## 3. Architecture

### 3.1 Directory layout

| File | Change |
|---|---|
| `engine/engine.h` | Add `ModSourceId`, `ModRoute`, `CombinationClass`, `kModSlots`/`kNumParams`/`kNumBuses`; generalize `Part`; extend `Voice` (vel/note/key_follow/gate/steal_vel replace gain/steal_gain); declare `EngineSetRoute` |
| `engine/params.h` | Extend `ParamId`; add `CombinationClass` to `ParamDesc` |
| `engine/params.cc` | Add descriptor rows + classes; retarget offsets to `params[]` index / `key_follow_depth` offsetof; remove `kFilterEnvAmount` row (phase 2) |
| `engine/engine.cc` | Matrix evaluation, source gating, default-route init, key-follow derivation, bus indirection, `UpdateFilterCoeffs` rework, `EngineSetRoute` body; delete `VelocityToGain`/`kVoiceHeadroom` |
| `engine/ipc.h`, `engine/ipc.cc` | Generalize `ParamBlock` to double-buffered `Part`; add `SetRoute` |
| `engine/midi.cc` | Add pitchbend (`0xE0`) → `kPitchBend` param routing |
| `tools/wav_render.cc`, `tools/live_render.cc`, `tools/bench.cc` | `EngineSetParam(kFilterEnvAmount, …)` → `EngineSetRoute(env1→cutoff)` |
| `tests/test_mod_route.cc` | New: matrix route tests |
| `tests/test_params.cc`, `tests/test_param_block.cc`, `tests/test_midi.cc` | Update for removed/added params |
| `CMakeLists.txt` | Register `test_mod_route` |

### 3.2 Dependency graph

```
engine/engine.cc ──> engine/params.h (ParamId + CombinationClass), engine/engine.h (types), engine/ipc.h (Part transport)
engine/ipc.cc    ──> engine/engine.h (Part), engine/params.h (g_params offsets)
engine/midi.cc   ──> engine/engine.h (EngineSetParam → kPitchBend)
tests/test_mod_route.cc ──> engine (EngineSetRoute + Render)
```

No inter-package edges change; `engine` is a single static lib. The `Part` struct is the one boundary both `engine/ipc.cc` (transport) and `engine/engine.cc` (DSP) consume.

## 4. Interface Changes

### `engine/engine.h` — types added

```cpp
enum class CombinationClass : std::uint8_t { kAdditive, kMultiplicative, kExponential };

enum class ModSourceId : std::uint8_t {
    kNone = 0,      // empty-slot sentinel; zero-init marks a slot empty
    kVelocity, kNote, kGate,
    kLfo0, kLfo1, kLfo2,
    kEnv0, kEnv1, kEnv2,
    kModWheel, kAftertouch, kPitchBend, kExpression,
    kRandom, kConstant,
};

struct ModRoute {
    ModSourceId source = ModSourceId::kNone;  // kNone == empty slot
    ParamId destination;                        // valid only when source != kNone
    float amount = 0.0f;                        // signed; 0 == "present but silent"
};

inline constexpr int kModSlots = 16;
inline constexpr int kNumParams = 9;  // phase-1 params[] floats (grows in phase 2–4)
inline constexpr int kNumBuses = 1;
```

The full 16-source enum is defined now (cheap, matches arch-design §7); phase 1 *computes* seven sources — `kVelocity`, `kNote`, `kGate`, `kEnv0`, `kEnv1`, `kPitchBend`, `kConstant` — the five the default routes reference plus `kGate`/`kConstant` (latched/fixed, effectively free). LFOs, `kEnv2`, `kRandom`, `kModWheel`, `kAftertouch`, `kExpression` are enum members whose value-read returns their rest value (0/neutral) in phase 1; they are unreachable because no route references them, so source gating never advances them.

### `engine/engine.h` — `Part` generalized

```cpp
struct Part {
    float params[kNumParams];       // normalized [0,1]; snapshotted per block
    float filter_env_amount;        // legacy named field; kept in phase 1, removed in task 2.5
    float key_follow_depth;         // key-follow depth for kNote→cutoff, [0,1], default 0 (authoritative; see §5.5)
    ModRoute routes[kModSlots];     // kModSlots = 16; zero-init == all empty
};
```

`kNumParams` = 9 in phase 1: `cutoff, resonance, attack, decay, sustain, release, amp, pitch_coarse, pitchbend`. Six of the existing 7 named fields collapse into `params[]`; `filter_env_amount` stays a legacy named field (addressed via `offsetof`) for phase 1 only, so `kNumParams` doesn't churn, and is removed in task 2.5. `key_follow_depth` is a named field (not in `params[]`), addressed via `offsetof`, and is the **single source of truth** for key-follow depth: the matrix reads it directly (§5.5 step 4), not the default route slot 3's generic `amount`. `EngineSetParam(part, kKeyFollowDepth, x)` is how key-follow depth is changed.

### `engine/params.h` / `engine/params.cc` — `ParamId` + `ParamDesc`

```cpp
enum class ParamId : std::uint8_t {
    kCutoff = 0, kResonance, kAttack, kDecay, kSustain, kRelease,
    kAmp, kPitchCoarse, kPitchBend,    // 6,7,8 → params[] (index × sizeof(float))
    kKeyFollowDepth, kFilterEnvAmount, // 9,10 → offsetof (named fields)
    kCount,
};

struct ParamDesc {
    const char *name;
    const char *unit;
    float disp_min, disp_max, def;
    ParamCurve curve;
    std::size_t offset;               // byte offset into Part (params[] idx*4 or offsetof)
    CombinationClass comb;            // meaningful for destinations; kAdditive elsewhere
};
```

Phase-1 descriptor rows (combinations per arch-design §5.3):

| Param | class | default | notes |
|---|---|---|---|
| `kCutoff` | `kAdditive` | 1.0 | existing |
| `kResonance` | `kAdditive` | 0.0 | existing |
| `kAttack`/`kDecay`/`kRelease` | `kExponential` | existing | env0 times; modulatable in phase 3 |
| `kSustain` | `kMultiplicative` | existing | env0 level (a level, not a time — see §6) |
| `kAmp` | `kMultiplicative` | 1.0 | amp/level base |
| `kPitchCoarse` | `kExponential` | 0.5 | static tuning, ±24 semi, 0.5 = neutral |
| `kPitchBend` | `kAdditive` (unused) | 0.5 | performance input (source); 0.5 = center; disp 0..1 linear |
| `kKeyFollowDepth` | `kAdditive` (unused) | 0.0 | named field (offsetof); not a destination |

The `CombinationClass` is only *meaningful* for destinations; `kKeyFollowDepth` (a route amount) and `kPitchBend` (a source) default to `kAdditive` and are never targeted as destinations in phase 1. Destinations phase 1 actually folds into DSP — `kCutoff`, `kAmp`, `kPitchCoarse` — are the three the default routes target; `kResonance`/env-times/pan/wave/sends/LFO-rates are deferred destinations (phase 2–4).

### `engine/ipc.h` — `ParamBlock` generalized + `SetRoute`

```cpp
class ParamBlock {
  public:
    void Set(int part, ParamId id, float norm);
    float Get(int part, ParamId id) const;
    void SetRoute(int part, int slot, ModSourceId src, ParamId dst, float amount);
    void Commit(Part *parts);
    void Reset(const ParamDesc *table);
  private:
    Part pending_[kNumParts];      // control-thread authoritative state
    Part buf_[2][kNumParts];       // shared: audio reads buf_[front_]
    std::atomic<int> front_{0};
};
```

`Set`/`Get` write/read `pending_[part]` at `g_params[id].offset` (the same byte-arithmetic `ParamGet`/`ParamSet` use), so the flat-slot linearization (`slot(part, i)`) disappears. `SetRoute` writes `pending_[part].routes[slot]`. Both publish `pending_` → the back buffer, then release-store `front_`; `Commit` copies `buf_[front_]` whole `Part` structs into the audio-side parts. The per-slot `atomic<float>` is replaced by one atomic front/back flip (the whole `Part` is written before the release-store). `test_split.cc` (TSAN) covers this boundary.

### `engine/engine.h` — `Voice` modified

```cpp
struct Voice {
    // ... existing DSP state (phase, inc, g/k/a1/a2/a3, ic1eq/ic2eq, env, env_inc, stage) ...
    float vel;          // per-note latched velocity (raw 1..127); REPLACES gain
    float note;         // per-note MIDI note number (integer-valued float)
    float key_follow;   // per-note octave offset from C4
    float gate;         // per-note (derived from stage; 1 held, 0 released)
    float steal_freq;   // unchanged
    float steal_vel;    // REPLACES steal_gain
    std::uint8_t part;
    float last_env_cutoff;
    float last_q;
};
```

`gain` → `vel`; the velocity→amp route amount (0.25) now multiplies `vel/127` in the matrix, not at note-on. `steal_gain` → `steal_vel`.

### Functions added

```cpp
/// @brief Set one modulation route for a part (control thread).
/// @param part Part index in [0, kNumParts).
/// @param slot Route slot in [0, kModSlots).
/// @param src Modulation source; kNone clears the slot.
/// @param dst Destination parameter (phase-1 set: kCutoff, kAmp, kPitchCoarse).
/// @param amount Signed normalized amount in [-1, 1] (key follow [0, 1]).
void EngineSetRoute(int part, int slot, ModSourceId src, ParamId dst, float amount);
```

Called by `tools/*` (env1→cutoff) and tests. Precondition/error semantics per arch-design §8: invalid `part`/`slot`/`dst` → no-op; `src == kNone` clears the slot; the routed-source bitmask keys off `src != kNone`, never `amount`.

### Internal functions modified (`engine/engine.cc`, anonymous namespace)

`StartNote`/`StealNote`/`ApplyEvents`: the `float gain` argument becomes the raw MIDI `velocity`; `StartNote` latches `vel`, `note`, `key_follow` (derived from `freq_hz`), and `gate` (task 2.2). `UpdateFilterCoeffs` gains `cutoff_norm_eff` and the key-follow factor inputs, replacing its internal `p->cutoff + p->filter_env_amount * v->env` (tasks 2.1/2.5); its `p->resonance` read retargets to `params[kResonance]` (task 1.3). `EnterDecay`/`UpdateEnvelope` retarget their direct `p->sustain` reads to `params[kSustain]` (task 1.3) — the only two functions that read `sustain` as a named field.

### Functions removed (replaced)

| Removed | Replacement |
|---|---|
| `VelocityToGain(std::uint8_t)` (static, `engine/engine.cc`) | velocity→amp route (amount `kVoiceHeadroom` = 0.25, multiplicative) |
| `kVoiceHeadroom` (constant, `engine/engine.cc`) | default-route amount 0.25 |

### Fields/params removed (replaced)

| Removed | Replacement | Callers updated |
|---|---|---|
| `Voice::gain` | `Voice::vel` + velocity→amp route | `engine.cc` (StartNote/StealNote/RenderBlock) |
| `Voice::steal_gain` | `Voice::steal_vel` | `engine.cc` (StealNote) |
| `Part::filter_env_amount` | env1→cutoff route (slot 2 amount) | `engine.cc` (UpdateFilterCoeffs) |
| `ParamId::kFilterEnvAmount` | `EngineSetRoute(part, 2, kEnv1, kCutoff, amt)` | `tools/wav_render.cc`, `tools/live_render.cc`, `tools/bench.cc`, `params.cc` |

The six remaining named `Part` fields (`cutoff`, `resonance`, `attack`, `decay`, `sustain`, `release`) are *renamed into* `params[0..5]` (task 1.3), not deleted. Direct field accesses: `p->cutoff`/`p->resonance` in `UpdateFilterCoeffs`, `p->sustain` in `EnterDecay`/`UpdateEnvelope`; `attack`/`decay`/`release` are read via `ParamGetDisp` (offset-based, no retarget needed).

## 5. Solution Breakdown

### 5.1 Types + enums (task 1.1)

Add `CombinationClass`, `ModSourceId`, `ModRoute`, and the `kModSlots`/`kNumParams`/`kNumBuses` constants to `engine/engine.h`. `ModSourceId::kNone = 0` is the empty-slot sentinel so `Part{}` zero-init empties all 16 routes (arch-design §5.5). `ModRoute` is trivially copyable (enum + enum + float), so `Part` stays POD/memcpy-able.

- **Edge cases**: `kNumParams` counts only the `params[]` floats (9), not `key_follow_depth`.
- **Dependencies**: produces the types consumed by 1.2–1.4 and 2.x.
- **Done**: task 1.5 — engine compiles.

### 5.2 `ParamId` + `ParamDesc::CombinationClass` (task 1.2)

Extend `ParamId` with `kAmp`, `kPitchCoarse`, `kPitchBend` (the three new `params[]` members) then `kKeyFollowDepth` (named field), inserted before `kCount`; `kFilterEnvAmount` stays until task 2.5. Add `CombinationClass comb` to `ParamDesc` and populate the descriptor rows + classes per the table in §4. `kPitchCoarse` is bipolar: `disp_min = -24`, `disp_max = +24`, `def = 0.5` (0 semitones), `ParamCurve::kLinear` (norm 0.5 → 0 semi); `kPitchBend` stores normalized [0,1] with 0.5 = center.

- **Edge cases**: designated initializers (`[static_cast<size_t>(ParamId::kX)] = …`) make the insertion order-independent; `test_params.cc` iterates `[0, kCount)` asserting a non-empty name + `def ∈ [0,1]` — every new row must satisfy both.
- **Dependencies**: consumes 1.1; produces the descriptor table 2.x reads.
- **Done**: task 1.5.

### 5.3 `Part` generalization + offset retarget (task 1.3)

Move the 6 surviving float fields (`cutoff, resonance, attack, decay, sustain, release`) into `float params[kNumParams]` (first member, offset 0); keep `filter_env_amount` a named field through phase 1; add `key_follow_depth`; add `routes[kModSlots]`. Retarget each `ParamDesc::offset`: params become `index * sizeof(float)`; `kFilterEnvAmount` and `kKeyFollowDepth` become `offsetof(Part, filter_env_amount)` / `offsetof(Part, key_follow_depth)`. `ParamGet`/`ParamSet` byte-arithmetic (`base + desc.offset`) is unchanged.

- **Edge cases**: `filter_env_amount` remains a named field (addressed via `offsetof`) through phase 1 and is removed in task 2.5, so `kNumParams` stays 9 and no `params[]` index shifts mid-plan.
- **Dependencies**: consumes 1.1/1.2; produces the `Part` shape `ParamBlock` (1.4) and the DSP (2.x) consume.
- **Done**: task 1.5.

### 5.4 `ParamBlock` generalization + `SetRoute` (task 1.4)

Replace `float pending_[kSlots]` + `atomic<float> v[kSlots]` with `Part pending_[kNumParts]` + `Part buf_[2][kNumParts]` + `atomic<int> front_`. `Set`/`Get` address `pending_[part]` via `g_params[id].offset`; `SetRoute` writes `pending_[part].routes[slot]`. Publish = copy `pending_` → back buffer + release-store `front_`; `Commit` memcpy's `buf_[front_]` → `parts[]`.

- **Edge cases**: the whole `Part` (routes included) must be written to the back buffer *before* the `front_` release-store — the single flip replaces the per-slot atomics; `test_split.cc` (TSAN) is the regression net.
- **Dependencies**: consumes 1.3; produces the transport `EngineSetParam`/`EngineSetRoute` publish through.
- **Done**: task 1.5 — `test_param_block` + `test_split` pass. `test_split` is the existing TSAN test (needs no change; it exercises `EngineSetParam`→`Commit`→`Render` through the stable engine API). The front/back flip must also pass under TSAN (`cmake -B /tmp/twang-build-tsan -DTWANG_ENABLE_TSAN=ON . && cmake --build /tmp/twang-build-tsan --target test_split && /tmp/twang-build-tsan/test_split`), which Verify 1.5 gates.

### 5.5 Matrix evaluation (tasks 2.1–2.4)

In `RenderBlock`, per control step, per active voice:

1. **Read sources** — `vel/127`, `key_follow` (latched), `gate` (stage-derived), `env` (single envelope, read as both `kEnv0` and `kEnv1`), `pitchbend` (`2*params[kPitchBend] − 1`), `kConstant` = 1.0.
2. **Gate** — compute the per-part routed-source bitmask from `routes[].source != kNone`; in phase 1 the only gateable sources are LFO/env2 (absent), so the mask is inert but wired (arch-design §5.5 "Culling").
3. **Accumulate** — for each of 16 slots with `source != kNone`, fold `amount × source` into per-destination accumulators: `kAmp` multiplicative (`Π`), `kCutoff` additive, `kPitchCoarse` exponential (`Σ amount·src` in semitones). `kNote` (key follow) is the one source that does *not* accumulate into `cutoff_norm` — its latched octave offset is applied exponentially in Hz (step 4).
4. **Write** — `amp_eff = base_amp × Π(amount·src)`; `cutoff_norm_eff = clamp(base_cutoff + Σ additive)`; `inc_eff = inc × 2^((pitch_semitones + Σ pitch routes)/12)`.

The sample loop becomes `g_buses[0].L[start+i] += lp * amp_eff` (amp_eff replaces `env * gain`); `UpdateFilterCoeffs` takes `cutoff_norm_eff` and applies the key-follow factor in Hz — `fc = NormToHz(cutoff_norm_eff) × 2^(key_follow_depth × key_follow)`; the pitch factor scales `inc` before `DspOscTick`. `StartNote` latches `vel`, `note`, `key_follow = (69 + 12·log2(freq/440) − 60)/12`, `gate = 1` (task 2.2); `ReleaseNote`/idle set `gate = 0`. `EngineInit` pre-populates the 5 default routes at slots 0–4 (task 2.3): slot 0 `kVelocity→kAmp` (0.25), slot 1 `kEnv0→kAmp` (1.0), slot 2 `kEnv1→kCutoff` (0.0), slot 3 `kNote→kCutoff` (amount seeded from `key_follow_depth`, 0.0), slot 4 `kPitchBend→kPitchCoarse` (0.0). Slot 3's `amount` is a seed only — the key-follow factor always reads the named field `key_follow_depth`, so `EngineSetRoute` on slot 3 edits the generic route, not key-follow depth.

- **Edge cases**: an empty slot (`kNone`) and a zero-amount route both contribute nothing, but only the zero-amount route keeps its source bit set; the three migration routes reproduce today's `lp * env * gain` and `cutoff + filter_env_amount * env`; key follow and pitchbend default to amount 0 so both are no-ops at rest; `cutoff_norm_eff` clamps to [0,1], and the key-follow factor applies in Hz *after* `NormToHz` (arch-design §9), not on the normalized value.
- **Dependencies**: consumes 1.x; produces the effective-value path 2.5 deletes the hardcoded fields into.
- **Done**: task 2.7 — `test_mod_route`; user sign-off at 3.5.

### 5.6 Cutover (task 2.5)

Delete `Voice::gain`/`steal_gain` (→ `vel`/`steal_vel`), `kVoiceHeadroom`, `VelocityToGain`, `Part::filter_env_amount`, and `ParamId::kFilterEnvAmount` + its descriptor row. `UpdateFilterCoeffs` consumes `cutoff_norm_eff` (the matrix's additive sum) and applies the key-follow factor in the Hz domain — `fc = ParamNormToDisp(&g_params[kCutoff], cutoff_norm_eff) × 2^(key_follow_depth × key_follow)` — instead of `p->cutoff + p->filter_env_amount * v->env`.

- **Edge cases**: `filter_env_amount` has exactly the callers listed in §4; no others (verified via grep — `spike/panel.cc` never references it).
- **Dependencies**: depends on 2.1–2.4 producing the effective values; produces the breakage 3.1/3.2 repair.
- **Done**: task 2.7 (engine + test_mod_route build); full build at 3.4.

### 5.7 Route tests (task 2.6)

`tests/test_mod_route.cc` drives `EngineSetRoute`/`EngineSetParam` + `Render`:

- **Empty slot / zero amount** — both contribute nothing; zero-amount keeps the source computed (assert via the routed-source mask or a source-render counter).
- **Velocity → amp** — soft note quieter than full-velocity note (parity with `test_engine`).
- **Key follow** — `EngineSetParam(0, kKeyFollowDepth, 1.0)`: note at C5 renders a cutoff ×2 the C4 cutoff in Hz; `key_follow_depth = 0`: identical (measured via a deterministic render, not a golden hash).
- **Pitchbend** — `kPitchBend→kPitchCoarse` amount 2: full bend ±2 semitones; amount 0 (default): non-zero bend changes nothing.
- **Source gating** — an unrouted source's phase/counter does not advance (inert in phase 1; becomes observable when LFOs land in phase 2).

- **Done**: task 2.7.

### 5.8 Pitchbend MIDI routing (task 3.1, `engine/midi.cc`)

Add a `case 0xE0` to `MidiMessage` that maps the 14-bit bend (`d1 | d2<<7`, center 0x2000) to `kPitchBend` normalized [0,1] via `EngineSetParam(part, kPitchBend, bend / 16383.0f)`. This is the phase-1 performance-source wiring (arch-design §12); modwheel/aftertouch/expression land in later phases.

- **Done**: task 3.4 (covered by `test_midi` pitchbend dispatch + `test_mod_route`).

### 5.9 Callers + tests (tasks 3.2–3.3)

`tools/{wav_render,live_render,bench}.cc` replace `EngineSetParam(0, kFilterEnvAmount, 0.5f)` with `EngineSetRoute(0, 2, kEnv1, kCutoff, 0.5f)` (the plucky-patch filter sweep). `test_params.cc` gains explicit assertions for the new params' defaults/classes (its generic `[0, kCount)` loop already tolerates the removed `kFilterEnvAmount` with no edit needed); `test_param_block.cc` adds a `SetRoute`/`Commit` round-trip; `test_engine.cc` is unchanged in intent (behavioral checks still hold); `test_midi.cc` adds pitchbend dispatch.

- **Done**: task 3.4 — full build + ctest green.

### 5.10 Migration gate (task 3.5)

`wav_render` before and after the change, same patch, same note — the user listens and signs off that the default-route sound is unchanged. This is the aural sign-off the arch-design's migration gate now specifies (see §6, decision 1). The objective regression net is the existing `test_engine` checks (peak/rms/tail/velocity-loudness), which must still pass.

- **Done**: task 3.5.

## 6. Design Decisions

- **Migration gate is a user aural sign-off, not a golden hash.** The arch-design originally specified "bit-identical / golden hash" as the migration gate (§10/§11/Appendix A); the user directed dropping the hash, and the arch-design was amended accordingly during this audit. The replacement gate: existing behavioral tests (`test_engine` peak/rms/tail/velocity) must still pass, plus the user listens to `wav_render` before/after and signs off. Rationale: bit-identity across a float reassociation is brittle and couples the plan to incidental rounding order; the actual acceptance is "it sounds the same."
- **`Part` generalizes to a flat `params[]` array + named `key_follow_depth` + `routes[]`.** The descriptor offset becomes a `params[]` index (×`sizeof(float)`) except `key_follow_depth` (`offsetof`). `ParamBlock` double-buffers whole `Part` structs so routes ride the same snapshot as params (one buffer, one swap — arch-design §4 transport / §5 part state).
- **`key_follow_depth` is the named field, not the route slot's `amount`.** Key follow is a source-level exception (arch-design §5.3): `kNote` combines exponentially in Hz and never accumulates into the generic destination sum. Its depth lives in `key_follow_depth` (set via `EngineSetParam(kKeyFollowDepth)`) and is read directly by the matrix (§5.5 step 4). The default route slot 3 (`kNote→kCutoff`) seeds `amount = key_follow_depth` at `EngineInit` for discoverability, but editing slot 3 via `EngineSetRoute` changes the generic route, not key-follow depth.
- **Phase 1 keeps the single existing envelope as both `kEnv0` and `kEnv1`.** The 3 independent envelopes are phase 3; phase 1's env0→amp and env1→cutoff routes both read `v->env`, reproducing today's single-envelope sound exactly. The arch-design's default-route table lists env0/env1 as distinct — that split lands with phase 3, not here.
- **`kPitchCoarse` is a static tuning param (default neutral).** Pitch needs a stored base so the exponential destination has a value and `test_params`' "every param has a def" invariant holds. `pitch_semitones = (params[kPitchCoarse] − 0.5) × 48` (±24 semitone display range); `inc_eff = inc × 2^((pitch_semitones + Σ route semitones)/12)`; at defaults both terms are 0 → ×1.0 no-op. (Alternative — a param-less destination with a note-derived base — rejected: it breaks the every-ParamId-has-a-`ParamDesc` invariant.)
- **Full `ModSourceId` enum now; source computation is incremental.** Defining all 16 sources costs nothing and matches arch-design §7; phase 1 computes only velocity/note/gate/env0/env1/pitchbend/constant, and the rest return rest values. Source gating (the bitmask) makes unrouted sources free, so unimplemented sources are never advanced.
- **Bus indirection is mono in phase 1.** `Bus` (`L`/`R`) + `g_buses[kNumBuses]` (== 1) are introduced, but phase 1 accumulates only `g_buses[0].L` and downmixes to mono `out`; the stereo pan law, per-voice level, and the 2 send taps are phase 4 (Appendix A "audio routing"). The seam (voice → bus by index) is what phase 4 builds on.
- **`kSustain` is multiplicative, not exponential.** The arch-design §5.3 lumped "env A/D/S/R ×3" as one exponential row, but the write (`time ×= 2^…`) only applies to the time parameters. Sustain is a level in [0,1], so it combines multiplicatively (like `kAmp`), not exponentially. The arch-design §5.3 row is corrected to match (fixed in this audit pass).
- **Float reassociation is absorbed by the aural gate.** The multiplicative amp accumulation reorders `(lp * env) * gain` into `lp * amp_eff`; IEEE commutativity keeps `gain*env == env*gain`, but associativity may shift the last-digit rounding. Inaudible, and no bit-identity requirement remains (decision 1).

## 7. Success Criteria

### Data model
- [ ] `Part` is `float params[kNumParams]` + `key_follow_depth` + `routes[16]`, still POD/memcpy-able — Verify 1.5
- [ ] Every `ParamId` has a non-empty name and a `def ∈ [0,1]` (`test_params`) — Verify 1.5
- [ ] `ParamBlock` round-trips params *and* routes through one double-buffer flip (`test_param_block`, `test_split`) — Verify 1.5

### Matrix + cutover
- [ ] Default routes reproduce today's velocity/env/cutoff behavior (peak/rms/tail/velocity-loudness in `test_engine`) — Verify 3.4
- [ ] Empty slot and zero-amount route both contribute nothing; only zero-amount keeps its source computed — Verify 2.7
- [ ] Key follow at `key_follow_depth` 1 doubles the cutoff Hz one octave up; depth 0 leaves it identical — Verify 2.7
- [ ] Pitchbend at amount 2 gives ±2 semitones; amount 0 is a no-op — Verify 2.7
- [ ] `Voice::gain`, `steal_gain`, `kVoiceHeadroom`, `VelocityToGain`, `filter_env_amount`, `kFilterEnvAmount` are gone — Verify 2.7
- [ ] User aural sign-off: default-route `wav_render` matches today's sound — Verify 3.5

### Integration
- [ ] Full desktop build + `ctest` green — Verify 3.4
- [ ] MIDI pitchbend (`0xE0`) reaches `kPitchBend` (`test_midi`) — Verify 3.4

## 8. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/synth-routing_arch-design.md` | No — the hash/bit-identity migration gate (§10 Test Architecture, §11 Acceptance first bullet, Appendix A Phase-1 verify gate) and the earlier §5.3 sustain-class/key-follow-domain, §5.4 "bit-for-bit", and §9 "sound exactly" sloppiness were all corrected in the arch-design during this audit; the migration gate now reads "user aural sign-off + existing behavioral tests" | None |
| `../design-studies/2026-09-10_modulation-matrix-and-routing_design-study.md` | No — point-in-time study; its "current state" and cutover analysis describe the pre-matrix engine and are not maintained | None |
| `README.md` | No — documents build/run, not modulation internals | None |

## 9. Cleanup

No diagnostic instrumentation is added by this plan. The `test_mod_route` source-render counter (if used for the source-gating assertion) is test-only, not production code. The arch-design hash-gate amendment was completed during this audit; no post-change documentation obligation remains.
