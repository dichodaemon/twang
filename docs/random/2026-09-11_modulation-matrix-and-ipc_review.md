---
title: Modulation Matrix and IPC Review
date: 2026-09-11
author: Claude (review)
---

# Modulation Matrix and IPC Review

## 1. Summary

Review of the `synth-routing` phase-1 series (`ccb9c53`..`14e18a9`) against
`synth-routing_arch-design.md` and the companion implementations report. Focus
requested: whether the cross-core IPC is efficient and correctly implemented.

The IPC **design** is sound — SPSC event ring for note events, double-buffered
`Part` for configuration, no locks, per-voice sources never crossing the
boundary. The `EventRing` implementation is correct. The `ParamBlock`
handshake, however, has an **ABA hazard** and a **missing StoreLoad
ordering**, neither of which TSan can be expected to catch, plus two avoidable
copy costs.

Separately, three matrix-evaluation defects were confirmed by execution: a
zero-amount route silences a voice on multiplicative destinations, bipolar
sources use the wrong multiplicative sub-form, and `CombinationClass` is
declared but never read.

| # | Finding | Area | Severity | Evidence |
|---|---|---|---|---|
| 1 | ABA in `ParamBlock::Commit` claim protocol | IPC | High | interleaving analysis |
| 2 | Dekker handshake lacks StoreLoad ordering | IPC | High | memory-model analysis |
| 3 | `Publish` copies all parts per single param write | IPC | Medium (perf) | 672 B measured |
| 4 | `Commit` copies unconditionally every block | IPC | Medium (perf) | 492 KB/s measured |
| 5 | `amount = 0` silences voice on multiplicative dst | Matrix | High | executed |
| 6 | Bipolar sources use unipolar multiplicative form | Matrix | High | executed |
| 7 | `CombinationClass` declared, never read | Matrix | Medium | grep |
| 8 | Routes to unhandled destinations silently ignored | Matrix | Low | executed |

## 2. Method

Read `engine/ipc.{h,cc}`, `engine/engine.{h,cc}`, `engine/params.{h,cc}`,
`tests/test_mod_route.cc`, the routing arch-design, and the implementations
report. Built with `-O2` and exercised:

- a two-thread `ParamBlock` stress harness (control thread publishing, audio
  thread committing at 750 Hz) measuring latency distributions;
- direct `Render` calls with constructed routes, comparing output RMS against
  the arch-design's stated combination semantics.

Not covered: the target build, actual M33/M85 behaviour, the DSP itself beyond
the matrix fold, and the allocator.

## 3. Measured baseline

```
sizeof(ModRoute)  =   8 B
sizeof(Part)      = 168 B      (9 params + key_follow_depth + 16 routes)
kNumParts         =   4
Publish copy      = 672 B      per single-parameter write
ParamBlock total  = 2024 B     (pending_ + two buffers)
Commit copy rate  = 492 KB/s   unconditional, at 750 blocks/s
matrix inner loop = 768,000 iterations/s   (750 × 4 steps × 24 voices × 16 slots)
```

Handshake latency on a host, control thread publishing in a tight loop
(12.4 M publishes/s — far beyond any real rate) with the audio thread
committing at block rate:

```
Publish   p50 = 31 ns   p99 = 43 ns   p99.9 = 53 ns
Commit    p50 = 53 ns   p99 = 187 ns
```

The spin path essentially never fires at this scale because `Commit` completes
faster than the publish interval. The concern below is correctness under rare
interleavings, not measured latency.

## 4. IPC findings

### 4.1 ABA hazard in the `Commit` claim protocol — High

`ParamBlock::Commit` claims a buffer, then verifies the front index has not
moved:

```cpp
do {
    f = front_.load(std::memory_order_acquire);
    reading_.store(f, std::memory_order_release);
} while (front_.load(std::memory_order_acquire) != f);
```

`front_` is a single bit. **Two publishes return it to its original value**,
and the verification cannot distinguish "never moved" from "moved and came
back". The failing interleaving:

| Step | Reader (audio) | Writer (control) | State |
|---|---|---|---|
| 1 | `f = front_.load()` → 0 | | `front_ = 0`, `reading_ = -1` |
| 2 | *(stalls before the store)* | Publish: `back = 1`, sees `reading_ = -1`, writes `buf_[1]`, `front_ = 1` | `front_ = 1` |
| 3 | | Publish: `back = 0`, **still** sees `reading_ = -1`, begins writing `buf_[0]` | mid-copy |
| 4 | `reading_.store(0)`; `front_.load()` | | — |

If step 4's load returns `0`, the reader proceeds into `buf_[0]` while the
writer is copying into it. Read-read coherence permits that load to return the
*original* `0` (modification-order position 0) rather than the fresh one
(position 2) — the two are indistinguishable to the reader.

The window requires the reader to stall between loading `front_` and storing
`reading_`, and two publishes to complete in that window. Rare, but a MIDI CC
burst or a UI drag publishes continuously, and the M33 is not a real-time core.

### 4.2 Dekker handshake lacks StoreLoad ordering — High

The same code is a Dekker-style mutual-exclusion handshake:

```
Reader:  reading_.store(f, release);   then   front_.load(acquire)
Writer:  front_.store(back, release);  then   reading_.load(acquire)
```

Release/acquire does not order a store followed by a load. Both sides may have
their store sink past their load, and each can then fail to observe the
other's claim. The pair needs `memory_order_seq_cst`, or an explicit
`std::atomic_thread_fence(std::memory_order_seq_cst)` between store and load on
both sides.

This matters far more on the M33/M85 through shared SDRAM than on the x86 where
TSan runs, which is the core reason **TSan passing is evidence but not proof
here**: both 4.1 and 4.2 require rare interleavings that TSan's schedule
exploration will not reliably reach, and TSan models the C++ abstract machine
rather than weak hardware.

### 4.3 Recommended fix — seqlock

Replace the `reading_` protocol with a monotonic generation counter:

```cpp
std::atomic<std::uint32_t> gen_{0};   // even = stable, odd = writer in progress

// Writer (control thread) — never blocks.
void Publish() {
    gen_.fetch_add(1, std::memory_order_relaxed);            // -> odd
    std::atomic_thread_fence(std::memory_order_release);
    for (int p = 0; p < kNumParts; ++p) buf_[p] = pending_[p];
    std::atomic_thread_fence(std::memory_order_release);
    gen_.fetch_add(1, std::memory_order_relaxed);            // -> even
}

// Reader (audio thread) — retries instead of reading a torn buffer.
bool TryCommit(Part *parts) {
    const std::uint32_t g0 = gen_.load(std::memory_order_acquire);
    if (g0 & 1u) return false;                     // writer mid-copy
    for (int p = 0; p < kNumParts; ++p) parts[p] = buf_[p];
    std::atomic_thread_fence(std::memory_order_acquire);
    return gen_.load(std::memory_order_relaxed) == g0;
}
```

This resolves several things at once:

- **No ABA** — the counter is monotonic, so a double-publish is visible.
- **No writer spin** — the control thread, which has the frame deadline, never
  waits for the audio thread.
- **One buffer instead of two** — `buf_[2][kNumParts]` collapses to
  `buf_[kNumParts]`, saving 672 B.
- **Free change detection** — see 4.5.

The trade is that the audio thread may retry. Bound it: on a failed
`TryCommit`, keep the previous snapshot and try again next block. Config is
change-driven, so a one-block delay is inaudible; a torn `Part` is not.

Measured retry rate for this scheme, audio committing at 750 Hz:

| Publish rate | Commits | Retries | Retry % |
|---:|---:|---:|---:|
| 479/s | 2110 | 0 | 0.00 |
| 920/s | 2113 | 0 | 0.00 |
| 3,527/s | 2120 | 0 | 0.00 |
| 9,225/s | 2161 | 0 | 0.00 |
| 34,000,000/s | 606 | 817 | **57.4** |

Zero retries at every plausible rate — a batched control cycle is ~1 kHz and
even an unbatched CC stream is a few hundred per second. The last row is a
writer publishing in a tight loop with no delay, which is not a workload the
system can produce; it is included because it shows the failure mode, and
because it is the reason the retry must be bounded rather than looped. With a
`while (!TryCommit())` loop that row would be a stall in the audio path.

### 4.4 `Publish` copies all parts per single-parameter write — Medium

`Set` and `SetRoute` each call `Publish`, which copies
`kNumParts × sizeof(Part)` = **672 B** to change one float. On the M33 that is
~170 word writes into shared SDRAM, on the thread with the frame deadline,
once per MIDI CC or per UI drag step.

Batch instead: mark dirty in `Set`/`SetRoute`, and publish once per control
cycle from the host loop. The API change is additive (`ParamBlock::Flush()`),
and the arch-design's postcondition — "becomes the effective base value at the
next block boundary" — is unchanged.

### 4.5 `Commit` copies unconditionally — Medium

`RenderBlock` calls `Commit` every block regardless of whether anything
changed: 750 × 672 B = **492 KB/s** of pure overhead in the steady state, which
is the common case for a synth being played rather than edited.

With the generation counter from 4.3 this is one comparison:

```cpp
const std::uint32_t g = gen_.load(std::memory_order_acquire);
if (g != last_committed_gen_) { /* copy */ last_committed_gen_ = g; }
```

### 4.6 Documentation accuracy

`ipc.h` states "the audio thread itself never blocks". True of locks, but
`Commit`'s retry loop is unbounded while the control thread publishes
continuously — every publish flips `front_` and fails the verification. Worth
restating as "never takes a lock; may retry" until 4.3 lands.

The parenthetical "rare, ~100 ns in practice" on the writer spin is consistent
with what I measured, but it holds only while the audio thread is never
preempted mid-`Commit`. That is true of a dedicated M85 and false of a host
audio callback thread.

## 5. Matrix findings

Confirmed by executing `Render` with constructed routes and comparing output
RMS:

```
baseline (default routes)          rms = 0.13895
amount=0 route -> kCutoff (add)    rms = 0.13895
amount=0 route -> kAmp   (mult)    rms = 0.00000
bipolar pitchbend -> kAmp, bend=0  rms = 0.00000
route -> kResonance (unhandled)    rms = 0.13895
```

### 5.1 `amount = 0` silences the voice on multiplicative destinations — High

`ModRoute::amount` is documented as *"signed; 0 == present but silent"*. That
holds for additive destinations. For multiplicative ones the evaluation is
`amp_eff *= contrib` with `contrib = amount × source`, so `amount = 0` gives
`amp_eff = 0` — the voice goes silent.

This is reachable by ordinary use: create a route to amp, dial the amount to
zero, expect neutrality, get silence. Either the doc comment is wrong or the
evaluation is; the arch-design's intent (5.2) says the evaluation.

### 5.2 Bipolar sources use the unipolar multiplicative form — High

Arch-design §5 specifies two sub-forms and explains why:

> Multiplicative has two sub-forms because a unipolar source (velocity,
> envelope) *attenuates* a level (`× a·s`), while a bipolar source (LFO)
> *tremolos around* the base (`× (1 + a·s)`).

`RenderBlock` implements only `amp_eff *= contrib` — the unipolar form. A
bipolar source into amp attenuates toward zero instead of modulating around the
base, which the pitchbend case above demonstrates.

**Polarity is not modelled anywhere in the code.** `ModSourceId` has no
polarity field and there is no polarity table, so the second sub-form currently
cannot be expressed. This must close before LFOs land in phase 2 — LFO → amp
tremolo is the canonical case for the bipolar form.

Suggested shape: a `constexpr bool kSourceBipolar[]` indexed by `ModSourceId`,
alongside the existing `g_params` table.

### 5.3 `CombinationClass` is declared but never read — Medium

`ParamDesc::comb` is populated for all nine parameters in `g_params[]`.
Nothing reads it. `RenderBlock` hardcodes the rule:

```cpp
switch (r.destination) {
case ParamId::kAmp:         amp_eff *= contrib;   break;  // multiplicative
case ParamId::kCutoff:      cutoff_eff += contrib; break; // additive
case ParamId::kPitchCoarse: pitch_route += contrib; break;// exponential
default: break;
}
```

Two sources of truth for the same fact, and the authoritative one is the
switch. Driving the fold from `g_params[dst].comb` would make the table live,
remove the per-destination `case`, and make phases 2–4 a table edit rather than
a switch edit.

### 5.4 Unhandled destinations vanish silently — Low

`default: break` drops routes to destinations not yet implemented
(resonance, envelope times). A user can create such a route, see it stored,
and observe no effect with no indication. Either reject the route at
`EngineSetRoute` for unimplemented destinations, or surface a "deferred" state
the UI can show.

## 6. Worth keeping

- **The exact-skip in `UpdateFilterCoeffs`.** Comparing `cutoff_norm_eff`,
  `key_follow_factor`, and `q` by float equality to skip a `pow` and a `tan`,
  with the comment justifying it — bit-identical inputs imply bit-identical
  outputs for deterministic float ops. Correct reasoning, correctly recorded.
- **Control-step event draining.** Applying note events every 16 samples
  rather than once per block bounds note latency to 0.33 ms for one extra loop
  level, and the comment explains why it is safe.
- **Matrix cost is a non-issue.** ~768k inner iterations/s with an early-out on
  `kNone`; no optimisation warranted.
- **`EventRing` is correct.** Acquire/release pairing is right, capacity is a
  power of two with a mask, and `Reset` is documented as init-only.

## 7. Suggested order

1. §5.1 and §5.2 — audible, reachable by ordinary use, and §5.2 blocks phase 2
2. §4.3 — the seqlock, which subsumes §4.1, §4.2, §4.4 and §4.5
3. §5.3 — drive the fold from `CombinationClass` before phases 2–4 add cases
4. §5.4 and §4.6 — hygiene

## 8. Limits of this review

The concurrency findings are analytical, not observed: §4.1 and §4.2 describe
interleavings permitted by the C++ memory model that I did not reproduce. They
cannot be dismissed by TSan passing, but neither have they been demonstrated on
hardware. The measured figures in §3 are from a host build under artificial
write pressure and establish orders of magnitude only; nothing here was
measured on the M33 or M85.

## Appendix A: Re-review verdict (Oh My Pi, 2026-09-11)

Every claim above was re-checked against `engine/ipc.{h,cc}`, `engine/engine.cc`,
`engine/params.cc`, and arch-design §5.3/§8/§10. The review is high quality —
the two IPC hazards and the two matrix defects are real — but the central fix
(§4.3, the seqlock) is unsound and must be rejected.

### A.1 Verdict table

| # | Finding | Verdict | Basis |
|---|---|---|---|
| 1 | ABA in `Commit` claim | Real | `front_` is 1-bit; two publishes (0→1→0) are indistinguishable from "never moved" |
| 2 | Dekker lacks StoreLoad ordering | Real | `release`/`acquire` does not order store→load; on ARM the claim can sink past the verify |
| 3 | `Publish` copies 672 B per param-write | Real, minor | ~170 word writes on the non-RT M33; wasteful, not dangerous |
| 4 | `Commit` copies every block | Real, minor | 492 KB/s steady state; fix comes free with the ABA fix |
| 5 | `amount=0` silences multiplicative dst | Real, but doc/acceptance bug, not code | code matches §5.3 formula; doc + criterion contradict the formula |
| 6 | Bipolar sources use unipolar form | Real, blocks phase 2 | §5.3 requires `×(1+a·s)`; code has only `×a·s`; no polarity modelled |
| 7 | `CombinationClass` declared, never read | Real | `comb` populated in `params.cc`, never read; the switch hardcodes it |
| 8 | Unhandled dst silently ignored | Real, low | `default: break` drops resonance/env-time routes |
| 4.3 | seqlock fix | Reject as written | reads plain `Part` while writer mid-copy = data race (UB) |
| 4.6 | doc accuracy | Real, low | "never blocks" true of locks, false of claim-retry |

### A.2 Critical correction — the seqlock (§4.3) is UB

The proposed scheme has the reader copy plain (non-atomic) `Part` bytes while
the writer may be mid-copy, then checks the generation counter *afterwards*:

```cpp
for (p) parts[p] = buf_[p];        // plain read
atomic_thread_fence(acquire);
return gen_.load(relaxed) == g0;   // discard if torn
```

The torn read itself is a concurrent read/write on non-atomic data — a data
race, UB, exactly what TSan flags. A 168-byte `Part` cannot be "optimistically
read" without either per-element atomics or temporal exclusion; Linux's seqlock
only works because it reads a single word under LKMM, not the C++ abstract
machine. This is the same reason a seqlock was rejected during phase 2.

The seqlock's claimed benefits do not require it:

- **Change detection (§4.5)** falls out of the monotonic counter that fixes ABA.
- **"Both sides never block"** is unattainable for plain 168-byte data. The
  current design already has the correct split: the writer spins on the non-RT
  M33, the reader never blocks on the RT M85.

### A.3 Recommended implementation

1. **Fix the handshake (replaces §4.1 + §4.2 + §4.5, one change).** Make
   `front_` a monotonic `uint32_t` (buffer index = `front_ & 1`, `back =
   (front_+1) & 1`), and use `memory_order_seq_cst` on all `front_`/`reading_`
   operations. This closes the ABA (a double-publish moves `front_` 0→2, so the
   reader's re-check fails and it retries) and closes the StoreLoad gap (seq_cst
   total order). Change detection becomes `if (f != last_front_)`. The writer
   still spins; the reader still never blocks.
2. **Add polarity + bipolar multiplicative form (§5.2/6).** A `constexpr bool
   kSourceBipolar[]` indexed by `ModSourceId` (the arch-design source table
   already labels polarity), and in the fold a bipolar source into a
   multiplicative destination uses `×(1 + amount·src)`, unipolar `×amount·src`.
   Not reachable in the default patch today, but canonical in phase 2 (LFO→amp
   tremolo) and cheap to add now.
3. **Drive the fold from `g_params[dst].comb` (§7).** Kills the second source of
   truth before phase 2 adds destinations. Not a pure table edit: the switch must
   still select the accumulator (`amp_eff`/`cutoff_eff`/`pitch_route`); `comb`
   selects the operator (`*=`/`+=`/exp-accumulate).
4. **Optional perf (§4.4):** additive `Flush()` + dirty flag so `Set`/`SetRoute`
   batch into `pending_` and publish once per control cycle. Low urgency.
5. **Hygiene (§8, §4.6):** reject unimplemented destinations at `EngineSetRoute`
   (a stored route can't silently do nothing), and reword "never blocks" to
   "never takes a lock; bounded claim-retry".

### A.4 Design decision — §5.1 is a doc/acceptance bug, not a code bug

The code correctly implements §5.3's `base × Π(amount·src)`, which gives `×0`
at `amount=0`. The arch-design is internally inconsistent:

- §5.3 formula → `amount=0` ⇒ silence
- `ModRoute` doc `0 == "present but silent"` ⇒ implies neutral
- §10 criterion "`amount==0` ⇒ rendering unchanged" ⇒ neutral

The clean "amount=0 always neutral" model (`×(1 + a(s−1))` for unipolar) is
incompatible with the velocity→amp default: that route must reach gain 0 at
velocity 0, which `×(1+a(s−1))` cannot do. The `amount=0.25` currently conflates
the headroom (0.25) with the velocity depth; that conflation is what makes
"amount" non-uniform.

**Recommendation:** accept the formula, fix the docs/criterion — document that
multiplicative `amount` is a *gain* (unipolar: 0=mute, 1=full-scale; bipolar:
0=neutral) and re-scope the "amount==0 unchanged" criterion to additive/
exponential only. The alternative (uniform-depth semantics) is a larger
redesign of velocity→amp that would invalidate the signed-off migration gate.

### A.5 Revised priority order

1. Handshake fix (memory safety on target — first)
2. Polarity + bipolar form (§5.2 — unblocks phase 2)
3. `comb`-driven fold (§7 — before phase 2)
4. §5.1 doc/criterion fix (pending the decision in A.4)
5. §4.4 batching, §8, §4.6 (hygiene/perf)
