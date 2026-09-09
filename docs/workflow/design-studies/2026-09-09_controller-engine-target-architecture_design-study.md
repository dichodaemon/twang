---
title: Controller/Engine Target Architecture
status: resolved
date: 2026-09-09
author: Dizan Vasquez
---

# Controller/Engine Target Architecture

## 1. Problem Statement

The codebase must compile for two targets: the desktop simulator (LVGL/SDL +
RtMidi + miniaudio) and the EK-RA8D2 firmware (M85 audio core + M33 control
core under Zephyr). Today the portable control/display logic — the LVGL UI,
the parameter-table walking, the controller model, the MIDI handler — is
entangled with desktop-only transport (SDL, RtMidi, miniaudio) inside one
`sim/` target, so it cannot be built for the target as written.

The flash point is the scope/spectrum visualization: it carries 160 KiB of
desktop-tuned buffers and a data source (a local audio tap) that has no target
equivalent. On the M33 it would need an M85→M33 audio-feedback channel that the
current inter-core design (digest §4: events + double-buffered params only) does
not include.

This study decomposes the target architecture into its decision dimensions —
where the portable/desktop line is drawn, whether the scope/spectrum ships on
target, and, if so, how its data flows and what it costs — and converges on an
end-to-end recommendation.

Out of scope: the analog-filter thread, the audio-output DMA design, the M85 DSP
internals, and the Phase-0 bring-up sequence. Those are governed elsewhere in
the digest and are not affected by this decision.

## 2. Current State

Directory layout, with portable vs desktop-only marked:

- `engine/` — **portable** (pure C++; includes only `<cmath>`, `<cstdio>`,
  `<cstdint>`, `<atomic>`, `<cstddef>`). DSP + allocator + IPC + params + MIDI
  handler.
- `sim/` — mixed:
  - `ui.cc` / `ui.h` — **portable** (LVGL + engine + fft + scope_ring; no
    SDL/RtMidi/miniaudio includes), but compiled into the `sim` target that
    links all three.
  - `fft.{h,cc}`, `scope_ring.{h,cc}`, `lv_conf.h` — **portable**.
  - `main.cc` — **desktop-only** (SDL + miniaudio + RtMidi wiring + loop).
  - `midi_io.{h,cc}` — **desktop-only** (RtMidi transport).
- `audio/` — `audio_out.h` (portable `audio::Output` interface) +
  `audio_out_miniaudio.cc` (desktop backend).
- `third_party/` — `rtmidi`, `miniaudio` (**desktop-only**); `lvgl` (portable
  submodule).
- `tools/` — host-side dev tools (`bench`, `wav_render`, `live_render`).

Seams that already exist and do **not** need redesign:

- **Audio device** — `audio::Output` + render callback (miniaudio now, SSIE/I2S
  later).
- **MIDI** — handler (`engine/midi.*`, portable) vs transport (`sim/midi_io.*`,
  desktop).
- **Controller model** — mouse (LVGL events) and MIDI both write
  `EngineSetParam` / `ui_note_on`; the engine is the single source of truth.

The scope/spectrum (the OUTPUT module's SCOPE/CYCLE/SPEC modes) is the one piece
that is portable *draw code* with a desktop-only *data source*: it reads a
`ScopeRing` fed by `ui_audio_tap`, called today only from the miniaudio render
callback.

Buffer inventory (all `float`, 4 B, owned by `Ui` + `ScopeRing`):

| Buffer | Count | Size | Why this size |
|---|---|---|---|
| `scope_ring.buf_` | 16384 | 64 KiB | ~341 ms @ 48 kHz; trailing-window read at stride 48 |
| `cycle_buf` | 4096 | 16 KiB | 3× longest period; low notes overflowed the old 1024 |
| `fft_re` | 8192 | 32 KiB | 8192-pt FFT; 2048 smeared low-note harmonics |
| `fft_im` | 8192 | 32 KiB | same |
| `fft_mag` | 4096 | 16 KiB | one value per FFT bin |
| **Total** | | **160 KiB** | |

Target memory (Zephyr cm33 target / RA8D2): 128 KiB M33 TCM; 640 KiB on-target
RAM (its TCM + a ~512 KiB slice of the 1664 KiB user SRAM); 64 MiB SDRAM at
0x68000000, CPU-addressable from the M33 (`device_type = "memory"` in the board
devicetree, shared by both cores).

## 3. Dimensions

### 3.1. Portable/Desktop Boundary

**Definition.** Where the source-tree line is drawn between code that compiles
for both targets and code that is desktop-only. Independent of the
scope/spectrum question: the boundary can be drawn first, and the scope/spectrum
placement is then a decision made within it.

#### Option 1: `controller/` + `host/` split

- **Properties.** Portable UI moves to `controller/` (ui, fft, scope_ring,
  lv_conf); desktop main + RtMidi transport to `host/`. `engine/` and `audio/`
  unchanged. New CMake lib `controller` (links `engine` + `lvgl` only); `host`
  executable (links `controller` + SDL + RtMidi + `audio`).
- **Pros.** Explicit boundary; a Zephyr app builds `engine/` + `controller/` with
  zero desktop deps; the seam is visible in the tree, not buried in a build file.
- **Cons.** One new directory; `lv_conf.h` may need desktop/target variants (SDL
  driver vs GLCDC).

#### Option 2: keep `sim/`, list portable sources in the Zephyr app

- **Properties.** No restructure now; the target app's `CMakeLists` explicitly
  lists `sim/ui.cc sim/fft.cc sim/scope_ring.cc engine/*.cc`.
- **Pros.** Zero churn today; the portable set is already factually clean
  (verified includes).
- **Cons.** The real boundary lives in a build file, invisible to a reader of the
  tree; "sim" misnames the portable half; nothing structurally prevents desktop
  code from leaking into the portable set later.

#### Option 3: `lib/` + `app/` (library vs application)

- **Properties.** `lib/engine`, `lib/controller` (portable); `app/sim` (desktop),
  `app/firmware` (target).
- **Pros.** Clearest lib/app distinction; scales if more apps emerge.
- **Cons.** Deeper nesting than the project's flat, lean layout; renames `engine/`
  and `audio/` too.

**Observation.** Interacts with 3.2: if the scope/spectrum is desktop-only, it
must be excluded from `controller/` (Option 1) or from the target source list
(Option 2).

**Conclusion.** Option 1 (`controller/` + `host/`). It makes the boundary a
first-class, greppable fact rather than an implicit build-file list, at minimal
churn.

### 3.2. Scope/Spectrum Feature Scope

**Definition.** Does the OUTPUT module's SCOPE/CYCLE/SPEC visualization run on
the target, or is it a desktop-only debug aid?

#### Option 1: desktop-only

- **Properties.** The scope/spectrum (draw code, 160 KiB buffers, and feed) is
  excluded from the target build; the target OUTPUT module shows the readout
  only.
- **Pros.** Zero target memory; zero feedback-IPC surface; zero coherency burden.
- **Cons.** The target UI loses a visualization that is part of the mockup.

#### Option 2: target-optional (gated off by default)

- **Properties.** The draw code stays in `controller/`; the buffers + feed are
  behind a build flag (default off). Desktop enables it; the target defaults to
  readout-only.
- **Pros.** Portable code preserved; reversible without restructuring; costs
  nothing until enabled.
- **Cons.** To actually save the memory, the gate must cover the buffer *members*
  in `Ui`, not just the feed — a flag around the draw functions and the buffer
  fields. Enabling later still inherits 3.3/3.4.

#### Option 3: target-feature (committed)

- **Properties.** Ship it; commit to a feedback path and a buffer budget now.
- **Pros.** Full mockup fidelity on target.
- **Cons.** Needs an M85→M33 feedback channel not in the digest's inter-core
  design. The memory cost is nil once the buffers live in SDRAM (§2).

**Observation.** This dimension gates 3.3 and 3.4: if desktop-only, the data path
and budget are moot for the target.

**Conclusion.** Option 3, committed — once the memory objection is removed. The
160 KiB that motivated the gate now lives in the 64 MiB SDRAM (verified
CPU-addressable from the M33, §2), so it no longer taxes the 640 KiB SRAM. The
scope/spectrum ships compiled-in with its buffers in SDRAM, and stays dormant
until the GLCDC display and the M85→M33 feedback feed exist — a missing feed
makes the feature dormant, not a build-time exclusion. No compile-time gate.

### 3.3. Scope/Spectrum Data Path (conditional on shipping)

**Definition.** How the rendered samples reach the M33's display, given the source
is the M85's output.

#### Option 1: M33-local ring fed by an IPC copy

- **Properties.** The M85 sends a (decimated) block over IPC; the M33 copies it
  into its own SDRAM ring (§3.4).
- **Pros.** Single-core ownership — no cache coherency, plain cacheable SRAM, no
  shared-region management.
- **Cons.** One memcpy per block + IPC bandwidth for the feedback channel.

#### Option 2: zero-copy shared ring

- **Properties.** The M85 writes directly into a ring in a shared non-cacheable
  region; the M33 reads it.
- **Pros.** No copy.
- **Cons.** Cache maintenance and non-cacheable placement (back toward TCM or an
  SRAM non-cacheable alias), plus cross-core handoff synchronization.

**Observation.** The digest's "buffers in TCM" rule is really about this axis —
DMA/cache coherency — not about display buffers per se. A M33-local ring sidesteps
it entirely.

**Conclusion.** Option 1 (M33-local) if ever shipped. The decimated memcpy is
negligible, and it removes the entire coherency class of bug for a debug feature.

### 3.4. Scope/Spectrum Buffer Budget (conditional on shipping)

**Definition.** How much memory the visualization is allowed on the M33.

#### Option 1: keep the desktop-tuned 160 KiB

- **Properties.** Identical visualization; FFT 8192, ring 16384.
- **Pros.** No re-tuning; pixel-identical to desktop.
- **Cons.** ~25% of the 640 KiB M33 RAM; overkill for a small panel.

#### Option 2: shrink (FFT 1024–2048, ring 4096)

- **Properties.** Coarser spectrum (more Hz/bin), shorter scope window; ~30–45 KiB.
- **Pros.** Fits comfortably; still legible on a small display.
- **Cons.** Low-note harmonics less resolved (the reason 8192 was chosen on
  desktop).

#### Option 3: move to SDRAM

- **Properties.** Relocate the ring/scratch to the 64 MiB SDRAM.
- **Pros.** Offloads SRAM/TCM entirely.
- **Cons.** Requires SDRAM controller init — but that init is already required
  for the GLCDC framebuffer, and the M33's access is verified (§2). Slower than
  SRAM, but irrelevant at the 20–50 Hz display rate.

**Conclusion.** Option 3 — move to SDRAM and keep the desktop-tuned sizes. With
the 64 MiB SDRAM verified CPU-addressable from the M33 (§2), 160 KiB costs 0.24%
of free SDRAM instead of 25% of scarce SRAM, so there is no memory reason to
shrink the FFT and lose low-frequency resolution. Shrinking remains available as
a display-quality choice only.

## 4. Design Options (end-to-end)

### Option A: `controller/` + `host/`, scope/spectrum desktop-only

`controller/` = engine + UI core (no scope/spectrum draw); `host/` = desktop
main + RtMidi + the scope/spectrum + its buffers + the audio tap. The target
builds `controller/` only; OUTPUT shows the readout.

- **Pros.** Smallest target footprint; the scope/spectrum and its 160 KiB never
  enter the target build; the boundary is exact.
- **Cons.** The scope/spectrum draw code must move out of `ui.cc` into `host/`
  (splitting the OUTPUT module), a real code edit; the mockup's visualization
  is desktop-only, with no in-tree path to re-add it.

### Option B: `controller/` + `host/`, scope/spectrum gated off by default

`controller/` keeps the full UI including the scope/spectrum draw code; the
buffers + feed are behind a build flag (default off). Desktop builds with the
flag on; the target builds with it off (or on, paying memory + adding a feed).

- **Pros.** No code split of `ui.cc`; enabling the feature on target later is a
  flag + a feed, not a restructure; reversible.
- **Cons.** The gate must cover the buffer members and the three draw functions,
  not just the feed; if the flag is ever flipped on, the 3.3/3.4 decisions come
  due.

### Option C: `controller/` + `host/`, scope/spectrum committed

`controller/` ships the feature; commit now to the M85→M33 feedback path and a
buffer budget.

- **Pros.** Full mockup fidelity on target from day one.
- **Cons.** Most work and most risk; commits a new IPC channel before the
  Phase-0 gate has proven the basic split (the memory cost is nil once the
  buffers live in SDRAM, §2).

## 5. Evaluation

Criteria (stated before the comparison): (1) target memory cost, (2) new IPC
surface, (3) restructure churn, (4) target mockup fidelity, (5) reversibility.

| Criterion | A (desktop-only) | B (gated off) | C (committed) |
|---|---|---|---|
| Target memory cost | None | None (default) | ~0% (SDRAM) |
| New IPC surface | None | None (default) | M85→M33 feedback channel |
| Restructure churn | Move files + split `ui.cc` | Move files + gate buffers/feed | Move files + add IPC |
| Target mockup fidelity | Readout only | Readout only (default) | Full |
| Reversibility | Low (re-add = re-split) | High (flip a flag) | N/A (committed) |

**Recommendation.** Option C (committed) — the `controller/` + `host/` split with
the scope/spectrum shipped compiled-in, buffers in SDRAM, no gate flag. The
original reason to gate (160 KiB = 25% of the M33's SRAM) was dissolved by the
SDRAM finding (§2): the buffers now cost 0.24% of 64 MiB, are CPU-addressable
from the M33, and the SDRAM controller is already required for the framebuffer.
The feature stays dormant — not compiled out — until the GLCDC display and the
M85→M33 feedback feed exist.

The decisive trade-off: A needs an irreversible `ui.cc` split to save memory that
is now free; B's gate was the old answer to that same memory cost and is now
unnecessary. C's one real remaining cost — the feedback IPC channel — is deferred
naturally: the feature is dormant until that channel is built, which needs no
compile-time flag.

## 6. Open Questions

1. **Buffer size — resolved.** The desktop-tuned sizes (FFT 8192, ring 16384)
   are kept, in SDRAM (§3.4); no shrink is needed because memory is no longer
   the constraint. Shrinking reopens only as a display-quality choice.
2. **Feedback IPC framing.** When the feed is added, does the M85 send a
   decimated block every N samples, or only while the OUTPUT module is visible?
   Blocks 3.3. Candidate: piggyback on the existing IPC mailbox, decimated to
   display refresh (~20–50 Hz). Decide when adding the feed.

## 7. Deliverables

Resolved by direct implementation (the arch-design and plan were skipped by
decision, not written):

- [x] `controller/` + `host/` split — `sim/` was split into `controller/`
  (portable UI + scope/fft) and `host/` (desktop transport); the `controller`
  library links `engine` + `lvgl` only.
- [x] Scope/spectrum ships compiled-in, buffers in SDRAM — no gate flag. The
  160 KiB moved out of the M33's SRAM into the 64 MiB SDRAM (verified
  CPU-addressable from the M33 at 0x68000000); the feature stays dormant until
  the GLCDC display and the M85→M33 feedback feed exist.
