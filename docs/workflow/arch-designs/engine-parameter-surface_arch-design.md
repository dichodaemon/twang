---
title: Engine Parameter Surface
status: draft
date: 2026-09-14
author: Dizan Vasquez
design-study: ../design-studies/2026-09-10_modulation-matrix-and-routing_design-study.md
---

# Engine Parameter Surface

## 1. Objective

The engine has no single owner for "what is a parameter." `ParamId` was defined by the routing
subsystem as its set of *modulatable destinations*, but the UI, patch storage, and MIDI CC map
all address the same underlying values — and the UI also addresses discrete and configuration
parameters (filter mode, wave select, mono/poly) that routing never targets. This arch-design
defines the **parameter surface**: the complete set of addressable parameters, their addressing
(`ParamRef`), and their descriptor (`ParamDesc`) — the single source of truth consumed by the
UI, patch save/load, MIDI CC, and, through the modulatable subset, the routing subsystem.

## 2. Non-Goals

- **Modulation semantics** — how sources combine into destinations (combination classes, route
  evaluation). Owned by [`synth-routing_arch-design.md`](synth-routing_arch-design.md).
- **DSP consumption** — how a parameter's value becomes filter coefficients, oscillator
  increment, or gain. Owned by the engine render path and
  [`output-stage_arch-design.md`](output-stage_arch-design.md).
- **UI layout** — which page/column a parameter appears on. Owned by
  [`nostromo-interaction_arch-design.md`](nostromo-interaction_arch-design.md).
- **Populating the full table** — the actual enumeration of every parameter (near 60 kinds).
  Additive growth, tracked as an open question; the *shape* here is what matters first.

## 3. Terminology

| Term | Definition | Maps to |
|---|---|---|
| Parameter kind | What a parameter is, independent of instance — `kOscCoarse`, not `kOsc3Coarse`. | `ParamId` |
| Instance | A per-module occurrence of a kind: oscillator 0–3, envelope 0–2, LFO 0–2. `0` for single-instance modules. | `ParamRef.instance` |
| Address | The `{instance, id}` pair that names one parameter value. | `ParamRef` |
| Parameter surface | The complete set of addressable parameters — modulatable, discrete, and config. | `ParamId` (full) + `g_params` |
| Modulatable | Whether a route may target the parameter. | `ParamDesc.modulatable` |
| Descriptor | Static per-kind metadata: name, unit, range, curve, storage layout, labels, combination class. | `ParamDesc` / `g_params` |

## 4. System Context

The parameter surface sits between the control-side API and part state, and is read by four
consumers:

- **UI** (`nostromo`) — resolves encoder bindings to parameters and formats display values.
- **Patch storage** — serialises and restores every addressable value.
- **MIDI CC map** — maps CC numbers to parameters.
- **Routing** (`synth-routing`) — targets only the modulatable subset as route destinations.

The surface itself is one enum (`ParamId`), one address type (`ParamRef`), and one descriptor
table (`g_params`). The part state it addresses is `Part`, double-buffered by the IPC transport.

## 5. Architecture

**Kind-not-instance addressing.** `ParamId` names a kind; `ParamRef` carries the instance. This
keeps the enum near the number of *kinds* (≈39) rather than kinds × instances (≈130), and lets
the four oscillator pages share one column list — the instance is derived from `SubjectId`, not
the enum.

**One descriptor table.** `g_params` is the single source of truth. Every parameter — modulatable,
discrete, or config — has exactly one `ParamDesc` row. `modulatable` is a descriptor flag, not an
enum category, so a parameter that later becomes modulatable (oscillator shape) flips a flag
rather than changing its identity.

**Byte-offset storage.** The descriptor carries `base` (byte offset of instance 0 within
`Part`) and `stride` (byte distance between instances), so an address resolves as
`bytes + base[id] + instance × stride[id]` into a float member. Byte offsets — not indices
into a single flat bank — are what let `kKeyFollowDepth` (a named field outside `params[]`)
use the same mechanism as the bank members. `stride == 0` for single-instance parameters,
which makes `instance` harmless there rather than a value callers must remember to pass as
`0`. A dense `[maxInstance][nParams]` bank is rejected — it is mostly holes (four
oscillators, one filter).

**Design decisions**

| Decision | Choice | Rationale |
|---|---|---|
| Instance addressing | `ParamRef` carries `instance`; `ParamId` stays per-kind | One address type shared by the API, routes, and UI bindings; ≈39 vs ≈130 entries |
| Storage layout | Descriptor `base`/`stride`, byte offsets into `Part` | No holes; single-instance params ignore `instance` |
| Modulatable vs discrete | Descriptor flag, one enum | A param's modulatability can change without changing its identity or stored patches |
| Discrete rendering | Descriptor value labels | SAW/LIN/HANN are labels, not numbers |

## 6. Types

### ParamId

The full parameter-kind enum: every modulatable, discrete, and configuration parameter. The
modulatable membership is a `ParamDesc` flag, not an enum split. The enumeration grows
additively as the engine does; the shape — a `std::uint8_t` enum — is fixed so `ParamRef` packs
to 16 bits.

### ParamRef

```cpp
struct ParamRef {
    std::uint8_t instance;  ///< 0 for single-instance modules
    ParamId      id;        ///< parameter kind
};
```

### ParamDesc

```cpp
struct ParamDesc {
    const char *name;             ///< parameter name
    const char *unit;             ///< display unit ("Hz", "%", "s", "semi")
    float disp_min, disp_max;     ///< display range
    float def;                    ///< default normalized value
    ParamCurve curve;             ///< display mapping (linear / exponential)
    std::uint16_t base;           ///< byte offset of instance 0 within the Part
    std::uint16_t stride;         ///< byte distance between instances; 0 = single-instance
    bool modulatable;             ///< whether a route may target this parameter
    const char *const *labels;    ///< discrete value labels; nullptr = continuous
    std::uint8_t n_labels;        ///< 0 = continuous
    CombinationClass comb;        ///< meaningful only when modulatable (defined by routing)
};
```

`CombinationClass` — the fold semantics of a modulatable destination — is defined by the routing
arch-design; the parameter surface only carries the field.

## 7. Contracts

### ParamGet / ParamSet

```cpp
float ParamGet(const Part *p, ParamRef ref);
void  ParamSet(Part *p, ParamRef ref, float norm);
```

- **Precondition**: `ref.id < ParamId::kCount`; `ref.instance` within the kind's instance count
  (`stride == 0` ⇒ `instance` ignored).
- **Postcondition**: resolves `base[id] + instance × stride[id]` and reads/writes the normalized
  value, clamped to `[0, 1]`.

### EngineSetParam

```cpp
void EngineSetParam(int part, ParamRef ref, float norm);
```

- **Precondition**: `part` in `[0, kNumParts)`, `ref` valid, `norm` in `[0, 1]`.
- **Postcondition**: the value is queued to the pending part state and published at the next
  block boundary (or on `EngineFlush`).

## 8. System Invariants

- `base[id] + instance × stride[id]` always lands inside the `Part` and is float-aligned.
- `stride == 0` iff the parameter is single-instance; `instance` is then ignored.
- `comb` is meaningful only when `modulatable`.
- Every `ParamId` has exactly one `ParamDesc` row; `g_params` has `ParamId::kCount` entries.
- `ref.id < ParamId::kCount` and `ref.instance < 256` — the bounds that keep `ParamRef` at 16
  bits. This size is a persisted-format commitment (`ParamRef` is `ModRoute.dst` in stored
  patches) and is compile-time asserted, not left to inspection.

## 9. Acceptance Criteria

- [ ] Given a single-instance parameter, `ParamSet(part, {0, id}, v)` and `ParamSet(part, {3, id}, v)`
      write the same value (instance is ignored when `stride == 0`).
- [ ] Given a multi-instance parameter, `ParamSet(part, {i, id}, v)` writes a value that
      `ParamGet(part, {i, id})` returns, independent of the other instances.
- [ ] Given a route whose destination is not modulatable, `EngineSetRoute` rejects it.
- [ ] Given a discrete parameter, formatting renders its label (SAW/LIN/HANN), not a number.
- [ ] Given a parameter marked modulatable after previously being discrete, its `ParamId` and
      stored patches are unchanged (only the flag flips).

## 10. Code Pointers

| File | Purpose |
|---|---|
| `engine/engine.h` | `ParamId`, `ParamRef`, `EngineSetParam/GetParam/SetParamDisp` |
| `engine/params.h` | `ParamDesc`, `g_params`, `ParamGet/Set/Format` |
| `engine/params.cc` | the descriptor table |
| `engine/ipc.h` | `ParamBlock` set/get, the double-buffered part state |

## 11. Open Questions

1. **The full enumeration.** `ParamId` today holds 11 modulatable entries against a target near
   60 (≈39 modulatable kinds plus ≈20 discrete/config). Populating the table (oscillator ×4, envelopes ×3, LFOs ×3, filter, part, FX, the OUT
   views' controls, plus every discrete/config parameter) is additive and does not block the
   shape; it proceeds as the engine grows.
2. **The `< 256` bound.** `ParamRef` packs to 16 bits while `ParamId < 256` and instances `< 256`.
   If the surface ever exceeds 255 kinds, the packing widens — a named constraint, not an
   assumption.
