---
title: Audio/MIDI Transport Hardening -- Implementation Plan
status: draft
date: 2026-09-18
author: Dizan Vasquez
design-note: ../design-notes/2026-09-18_audio-midi-transport-hardening_design-note.md
---

# Audio/MIDI Transport Hardening -- Implementation Plan

Companion: `2026-09-18_audio-midi-transport-hardening_design-note.md` (the
decisions, rationale, and acceptance criteria). This plan sequences the
implementation; it does not repeat the note's investigation.

## 2. Implementation Status

**Phases:**

1. **Instrumentation** — RTT + loss counters (no deps).
2. **UAC2 send chain** — slab-driven top-up + short packet (depends on 1).
3. **Control thread** — nav snapshot + touch queue + dedicated control thread (depends on 1).
4. **Two MIDI rings** — note ring + CC ring (depends on 2 and 3).
5. **Panic path** — CC 123 → `AllNotesOff` (depends on 4).
6. **Documentation + final verification** — staleness audit + hardware acceptance (depends on all).

| # | Task | Status |
|---|---|---|
| 1.1 | `west update segger`; add `CONFIG_USE_SEGGER_RTT=y` + `CONFIG_RTT_CONSOLE=y` to `target/zephyr/cm33/prj.conf` and `target/zephyr/cm85/prj.conf` | Pending |
| 1.2 | New `controller/loss_counters.h`: `LossCounters` struct (fixed SDRAM address) with `midi_ring_full`, `channel_reject`, `mt_reject`, `fifo_overflow_frames`, `ipc_event_drops` counters | Pending |
| 1.3 | Wire counters: `MidiRxCb` (`midi_ring_full`), `DrainMidi` (`channel_reject`/`mt_reject`), `FifoPush` (`fifo_overflow_frames`), `EngineControl::NoteOn/NoteOff` (`ipc_event_drops`) | Pending |
| 1.4 | Verify: `west build` cm33 and cm85 from their app dirs succeed | Pending |
| 2.1 | Dedicated `audio_queue` (`k_work_queue` + `K_THREAD_STACK_DEFINE`, high priority) in `target/zephyr/cm85/src/usb_composite.cc`; submit `send_work` and `prime_work` to it | Pending |
| 2.2 | Rewrite `SendHandler` top-up loop to `k_mem_slab_num_used_get(&send_slab) < kTargetInFlight`; `SendPacket` returns `bool`; delete `in_flight` and `in_send`; `terminal_enabled` → `std::atomic<bool>`; `Uac2BufReleaseCb` frees unconditionally; `Uac2TerminalCb` submits prime on enable | Pending |
| 2.3 | Short packet on underrun: `SendPacket` sends `got` frames, zero-length only when `got == 0` | Pending |
| 2.4 | Verify: `west build` cm85 succeeds | Pending |
| 3.1 | Add double-buffered `NavState` snapshot + `PublishNav()` to `nostromo/interaction.{h,cc}`; `Nav()` returns the published snapshot | Pending |
| 3.2 | Write test: `tests/test_interaction.cc` — publish then read yields a coherent snapshot (no torn `part`/`item[]`) | Pending |
| 3.3 | Add a cm33-local `InputEvent` queue; `PollTouch`/`PanelPointer` post `InputEvent`s instead of calling `Interaction::OnInput` synchronously | Pending |
| 3.4 | Extract `ControlThread` in `target/zephyr/cm33/src/main.cc` (drains MIDI + touch queue → `OnInput`/`PanelNoteOn/Off` → `PublishNav`); UI `for(;;)` keeps `PanelDraw` + `Present` | Pending |
| 3.5 | Verify: `west build` cm33 succeeds; `ctest` `interaction` passes | Pending |
| 4.1 | Two `MidiRing` instances at two fixed addresses + `IsNoteWord()` classifier in `controller/midi_ring.h`; add `note_ring_full` + `cc_ring_full` counters (replace `midi_ring_full`) | Pending |
| 4.2 | `MidiRxCb` classifies each UMP word and pushes to note or CC ring; reset both rings at init | Pending |
| 4.3 | `DrainMidi` drains the note ring before the CC ring | Pending |
| 4.4 | Write test: new `tests/test_midi_ring.cc` (classifier + note/CC SPSC behaviour); register in `CMakeLists.txt` | Pending |
| 4.5 | Verify: `west build` cm33 + cm85 succeed; `ctest` `midi_ring` passes | Pending |
| 5.1 | `Allocator::AllNotesOff(int part)` → `uint32_t` voice bitmask in `engine/allocator.{h,cc}` | Pending |
| 5.2 | `EngineControl::AllNotesOff(int part)` in `engine/engine_control.{h,cc}` — push one `kNoteOff` per released voice | Pending |
| 5.3 | Wire CC 123 → `AllNotesOff` in `DrainMidi` (`target/zephyr/cm33/src/main.cc`) and `MidiIo::Poll` (`host/midi_io.cc`) | Pending |
| 5.4 | Write test: `tests/test_allocator.cc` (releases all, respects part) + `tests/test_engine.cc` (one `kNoteOff` per voice) | Pending |
| 5.5 | Verify: `west build` cm33 + host succeed; `ctest` `allocator` + `engine` pass | Pending |
| 6.1 | Update `nostromo-interaction_arch-design.md` for the nav snapshot (`PublishNav`, `Nav()` semantics) | Pending |
| 6.2 | Verify: full desktop `cmake --build` + `ctest` pass; full target build (cm33 + cm85); flash and confirm the note's 8 acceptance criteria | Pending |

## 3. Architecture

### 3.1. Directory Layout

| File | Change |
|---|---|
| `controller/loss_counters.h` | New: `LossCounters` struct at a fixed SDRAM address |
| `controller/midi_ring.h` | Two `MidiRing` instances at two addresses + `IsNoteWord()` + per-ring counters |
| `target/zephyr/cm33/prj.conf` | Add `CONFIG_USE_SEGGER_RTT` + `CONFIG_RTT_CONSOLE` |
| `target/zephyr/cm85/prj.conf` | Add `CONFIG_USE_SEGGER_RTT` + `CONFIG_RTT_CONSOLE` |
| `target/zephyr/cm33/src/main.cc` | `DrainMidi` counters + CC 123; `ControlThread`; touch queue; two-ring drain |
| `target/zephyr/cm85/src/usb_composite.cc` | Dedicated queue; slab-driven `SendHandler`/`SendPacket`; short packet; `MidiRxCb` classify + counters |
| `nostromo/interaction.h` | `PublishNav()` + double-buffered snapshot; `Nav()` returns snapshot |
| `nostromo/interaction.cc` | `PublishNav()` implementation |
| `nostromo/panel.cc` | Touch path posts `InputEvent`s (instead of synchronous `OnInput`) |
| `engine/allocator.h` / `allocator.cc` | `AllNotesOff(int part)` |
| `engine/engine_control.h` / `engine_control.cc` | `AllNotesOff(int part)` + IPC event-drop counter |
| `host/midi_io.cc` | CC 123 → `AllNotesOff` |
| `tests/test_interaction.cc` | Nav snapshot test |
| `tests/test_midi_ring.cc` | New: classifier + ring test |
| `tests/test_allocator.cc` | `AllNotesOff` test |
| `tests/test_engine.cc` | `AllNotesOff` test |
| `CMakeLists.txt` | Register `test_midi_ring` |

### 3.2. Dependency Graph

No new inter-package edges. The desktop library structure (`engine`,
`nostromo`, `scope`, `spike`) is unchanged; `controller/midi_ring.h` and
`controller/loss_counters.h` are header-only and included directly by the two
target apps. The panic path adds allocator/engine-control methods consumed by
`host` and the cm33 app, both of which already link `engine`.

## 4. Interface Changes

### `MidiRing` + classifier (`controller/midi_ring.h`)

Two rings replace the single `kMidiRingAddr` instance:

```cpp
inline constexpr std::uintptr_t kNoteRingAddr = 0x68580000UL;  // 256 words (was kMidiRingAddr)
inline constexpr std::uintptr_t kCcRingAddr   = 0x68584000UL;  // larger (1024 words)

/// @return true if the UMP word is a note (0x80/0x90) or CC 123 (All Notes Off) —
///         the words that must never be dropped.
bool IsNoteWord(std::uint32_t word);
```

The existing `MidiRing` struct is unchanged; it is instantiated twice. The
addresses are placeholders to verify against the SDRAM map — the clear gap is
`0x68528000` (Panel end) to `0x68600000` (GLCDC FB0).

### Send chain (`target/zephyr/cm85/src/usb_composite.cc`)

```cpp
std::atomic<bool> terminal_enabled{false};   // was: bool
// in_flight and in_send are removed (slab used-count is the in-flight count;
// the single work queue serializes SendPacket).

bool SendPacket(const struct device *dev);   // was: void; true on successful send

void SendHandler(struct k_work *work) {
    while (g_uac2_dev && terminal_enabled.load() &&
           k_mem_slab_num_used_get(&send_slab) < kTargetInFlight) {
        if (!SendPacket(g_uac2_dev)) break;
    }
}

void Uac2BufReleaseCb(...) {
    k_mem_slab_free(&send_slab, buf);                 // unconditional
    k_work_submit_to_queue(&audio_queue, &send_work);
}

void Uac2TerminalCb(... bool enabled ...) {
    terminal_enabled.store(enabled);
    if (enabled) k_work_submit_to_queue(&audio_queue, &prime_work);
}
```

### `Interaction` nav snapshot (`nostromo/interaction.h`)

```cpp
// Nav() returns the published snapshot instead of nav directly. The control
// thread calls PublishNav() after mutating nav; the renderer reads the snapshot.
const NavState &Nav() const { return nav_snap_[nav_snap_idx_.load(relaxed) & 1]; }
void PublishNav();   // copies nav into the back slot, flips the index

NavState nav_snap_[2];              // double-buffered
std::atomic<uint32_t> nav_snap_idx_{0};
```

`Nav()` keeps its signature; the renderer call sites in `nostromo/panel.cc`
(`p.interaction->Nav()`) are unchanged.

### `AllNotesOff` (`engine/allocator.h`, `engine/engine_control.h`)

```cpp
// Allocator — releases every active voice in part; returns a bitmask of
// released voices (bit i = voice i was released). Was unspecified in the note
// (outlined as void); the bitmask lets EngineControl push one event per voice.
uint32_t AllNotesOff(int part);

// EngineControl — pushes one kNoteOff per released voice, then signals.
void AllNotesOff(int part);
```

## 5. Solution Breakdown

### 5.1. Instrumentation (tasks 1.1–1.4)

RTT is enabled by vendoring the SEGGER module and two Kconfig lines; the module
is already in the west manifest (`modules/debug/segger`) but not checked out.
`LossCounters` is a plain struct of `std::atomic<uint32_t>` fields
placement-new'd at a fixed SDRAM address, following the `ScopeTap`/`MidiRing`
rendezvous pattern (both cores read/write it by `reinterpret_cast`). The five
Phase-1 counters cover the loss paths that exist today; the two ring-full
counters are added in Phase 4.

- **Edge case:** `EngineControl::NoteOn` may return early ("full and nothing to
  steal") before pushing — that path is a legitimate drop (allocation failure),
  distinct from the event-ring-full drop counted here. Only `events.Push()`
  returning false increments `ipc_event_drops`.
- **Dependencies:** produces `LossCounters` (consumed by later phases). Requires
  nothing.
- **Done condition:** task 1.4 — both target apps build; counters observable
  over J-Link after the next flash.

### 5.2. Slab-driven send (tasks 2.1–2.4)

The send slab (`K_MEM_SLAB_DEFINE(send_slab, kSendBytes, 4, 32)`) already tracks
exactly the in-flight count: a block is allocated in `SendPacket` and freed
unconditionally in `Uac2BufReleaseCb`. The top-up loop therefore reads
`k_mem_slab_num_used_get(&send_slab)` instead of a parallel counter, eliminating
the epoch-drift race a counter would have. `in_send` is deleted because the
single `audio_queue` serializes `SendPacket`; `PrimeHandler` and `SendHandler`
both run on that queue.

- **Edge case:** a pre-disable release landing after re-enable simply frees a
  slab block; the top-up loop re-primes to 2 on the next handler run, with no
  counter to drift.
- **Edge case:** the third send attempt when 2 are in flight fails on the
  `uac2_pool` depth (2); `SendPacket` returns false and the loop breaks.
- **Dependencies:** requires nothing beyond Phase 1 (same file). Produces the
  final send contract consumed by Phase 6 verification.
- **Done condition:** task 2.4 — cm85 builds; short-packet and recovery
  behaviour confirmed on hardware (criteria in §7).

### 5.3. Control thread + nav snapshot (tasks 3.1–3.5)

`Interaction::nav` is a multi-field struct read by the renderer while the
control thread mutates it; a torn read (new `part`, old `item[]`) can index out
of range. The fix is a double-buffered `NavState` snapshot: `PublishNav()` copies
`nav` into the back slot and flips an atomic index; `Nav()` returns the published
slot. The control thread owns `Interaction` + `EngineControl` (the single IPC
producer); touch posts `InputEvent`s into a queue the control thread drains.

- **Edge case:** `p.interaction->out` and `p.interaction->feel` are also read
  cross-thread by the renderer (single-field reads, `timebase_ms`/`cycles`/
  `scope_interval_ms`); they are left as-is — single-field reads do not tear the
  way `NavState` does. The panel note playhead (`note_on`, `note_at`, …) is
  likewise left as a benign one-frame stale read.
- **Dependencies:** requires Phase 1 (main.cc counters in place). Produces the
  control-thread structure consumed by Phases 4–5 (DrainMidi edits).
- **Done condition:** task 3.5 — cm33 builds; `test_interaction` passes; the
  device-side latency criterion (§7) is measurable over RTT.

### 5.4. Two MIDI rings (tasks 4.1–4.5)

`MidiRxCb` (cm85) classifies each UMP word with `IsNoteWord()` and pushes to the
note ring (notes + CC 123) or the CC ring. The control thread drains the note
ring first. Both rings are strictly SPSC; no producer ever touches the
consumer's region. The generic Phase-1 `midi_ring_full` counter is replaced by
`note_ring_full` + `cc_ring_full`.

- **Edge case:** a fader sweep or SysEx burst can still fill the CC ring; that
  drops only CCs (self-correcting), never notes.
- **Dependencies:** requires Phases 2 (usb_composite.cc) and 3 (DrainMidi).
- **Done condition:** task 4.5 — both target apps build; `test_midi_ring`
  passes; `note_ring_drops` stays 0 under sustained CC traffic (§7).

### 5.5. Panic path (tasks 5.1–5.5)

CC 123 (All Notes Off) is special-cased before the surface-map lookup in both
transports. `Allocator::AllNotesOff(part)` releases every active voice in `part`
and returns a bitmask; `EngineControl::AllNotesOff(part)` pushes one `kNoteOff`
per released voice and signals the audio core.

- **Edge case:** CC 120 (All Sound Off) is intentionally out of scope — the note
  records that honest "silence now" semantics need a kill event, not a release.
- **Edge case:** `AllNotesOff` is additive and orthogonal to the
  note-identity refactor (the companion allocator note) — it touches neither
  `NoteOn` nor `NoteOff`.
- **Dependencies:** requires Phase 4 (DrainMidi finalized).
- **Done condition:** task 5.5 — cm33 + host build; `test_allocator` +
  `test_engine` pass; CC 123 releases every voice on hardware.

## 6. Design Decisions

- **Slab used-count over a parallel counter (task 2.2).** A parallel `in_flight`
  counter drifts when a pre-disable release lands after re-enable (the release
  cannot tell which epoch its buffer belonged to). `k_mem_slab_num_used_get` is
  the in-flight count by construction — allocated on send, freed unconditionally
  on release — with no reset semantics. The note's Decision 2 is implemented
  exactly this way.
- **Counter split across phases (deviation from the note's approach).** The note
  lists six counters in Decision 5 and "add the loss counters" in step 1, but
  `note_ring_full`/`cc_ring_full` only exist after the two-ring split (Decision
  6). The plan adds the five pre-existing-path counters in Phase 1 and the two
  per-ring counters in Phase 4. No design change; a sequencing clarification.
- **`AllNotesOff` returns a bitmask (minor refinement over the note's `void`).**
  `EngineControl` must push one `kNoteOff` per released voice, so the allocator
  returns the released-voice bitmask rather than `void`.
- **Note-ring-first drain (task 4.3).** The control thread drains the note ring
  to empty before the CC ring, so a held note's off is processed even when a
  fader sweep floods the CC ring.
- **Snapshot scope is `NavState` only (task 3.1).** `out` and `feel` are
  single-field cross-thread reads (no tear); the panel note playhead is a
  one-frame stale read. Snapshotting only `NavState` fixes the crash-capable
  tear without widening the change.
- **Touch round-trip latency accepted.** Touch posts an `InputEvent`, the control
  thread applies it, and the UI reflects it next frame — imperceptible at
  display rates, and the note records it as an accepted trade-off.

## 7. Success Criteria

Each criterion traces to a Verify: task in §2.

### Instrumentation
- [ ] RTT console prints from both cores (task 1.4).
- [ ] `channel_reject`, `mt_reject`, `fifo_overflow_frames`, `ipc_event_drops` are readable over J-Link after a stuck-note reproduction (task 1.4).

### Send chain
- [ ] `k_mem_slab_num_used_get(&send_slab)` returns to 2 after a host pause/resume (task 2.4).
- [ ] Underrun produces a short packet, no zero-padded splice (task 2.4).

### Control thread
- [ ] `Nav()` returns a coherent snapshot under concurrent publish (task 3.5).
- [ ] Device-side note latency (`MidiRxCb` timestamp → first non-silent FIFO frame) is within ~3 ms over RTT (task 6.2).

### Two rings
- [ ] `note_ring_drops` stays 0 under sustained encoder/fader traffic (task 4.5).

### Panic path
- [ ] CC 123 releases every active voice (task 5.5).

### End-to-end (task 6.2)
- [ ] No stuck note across the note's 8 acceptance criteria.
- [ ] `arecord … -d 60` returns ~60.0 s (SSIE render clock).

## 8. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../design-notes/2026-09-18_audio-midi-transport-hardening_design-note.md` | No — it is the source; the plan implements it (two sequencing refinements recorded in §6) | None |
| `../design-notes/2026-09-18_allocator-note-identity_design-note.md` | No — `AllNotesOff` is additive and orthogonal to the note-identity refactor | None |
| `../arch-designs/nostromo-interaction_arch-design.md` | Yes — `Nav()` now returns a published snapshot and `PublishNav()` is added | Task 6.1: update the Interaction structure/contract sections |
| `../briefs/2026-09-18_usb-audio-midi-device_brief.md` | Describes the one-work-item-per-completion send; superseded by this plan's Phase 2, but it is a frozen point-in-time brief | None (write-once; not retrofitted) |
| `../arch-designs/engine-parameter-surface_arch-design.md`, `synth-routing_arch-design.md`, `output-stage_arch-design.md`, `spike_arch-design.md` | No — untouched | None |

## 9. Cleanup

None. The loss counters and RTT are permanent instrumentation per the design
note ("the counters stay regardless"), not temporary diagnostics.
