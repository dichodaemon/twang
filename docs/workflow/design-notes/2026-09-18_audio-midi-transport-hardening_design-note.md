---
title: Audio/MIDI Transport Hardening
status: accepted
date: 2026-09-18
author: Dizan Vasquez
---

# Audio/MIDI Transport Hardening

## 1. Problem

On the EK-RA8D2 target, notes occasionally stick at sustain: a voice keeps
sounding as if it just reached its sustain value, even though no key is held and
its release envelope never starts. The note-off never reaches the voice. Latency
is also higher than the desktop sim (`host/`), and the transport has glitch
sources — zero-padded underruns and a lost-wakeup in the UAC2 send chain — that
the sim does not exhibit. The engine (`engine/`) is identical on both cores and
is normative; the divergence is in the target transport and in the allocator's
note-off matching.

## 2. Context

The transport spans two cores over shared SDRAM rings:

- `target/zephyr/cm33/src/main.cc` — control core. Its `for(;;)` loop drains the
  cross-core MIDI ring, then `PollTouch`, `PanelDraw`, `Present()`; `Present()`
  blocks on vsync, so MIDI drains once per frame.
- `target/zephyr/cm85/src/usb_composite.cc` — audio core. UAC2 send is driven by
  `buf_release_cb` (one `k_work_submit` per completed packet into a single
  `struct k_work`); `SendPacket` zero-pads underruns; `MidiRxCb` forwards
  `ump.data[0]` into the ring and discards `Push`'s return.
- `controller/midi_ring.h` — the 256-word SPSC ring (`kMidiRingAddr`).
- `engine/allocator.{h,cc}` — `NoteOff(part, freq_hz)` matches `owner_[i].freq ==
  freq_hz`, an exact float comparison.

The ring was already relocated out of `Panel::fft_im` (commit `f3ab05f`) and the
render is now DMA-clocked off the SSIE (commit `86ccebc`), which removed the
k_timer rate mismatch that drove FIFO overflow. The remaining defects are
transport loss paths plus the frequency-keyed note-off.

## 3. Decisions

### Decision 1: a single control thread owns the control side

**Decision:** Move the control-side work onto one dedicated high-priority thread
that owns `Interaction` + `EngineControl` — the sole producer on the IPC rings.
It drains the MIDI ring and a touch-event queue continuously. The UI/render loop
only reads navigation state and draws; touch input posts `InputEvent`s into the
control thread's queue instead of calling `Interaction::OnInput` synchronously.

**Rationale:** MIDI timing must not be gated by the display frame rate — today a
note-off can wait a full frame, and a long `PanelDraw` (the spectrum FFT) widens
the gap further. The sim has no such gate and is normative. But the naive fix —
drain MIDI on a second thread while the UI thread still calls `OnInput` — creates
two producers: `NoteOn/Off` push `ipc_->events`, and `SetParam`/`SetRoute` write
`ipc_->params`, both of which `ipc.h` documents as single-producer /
single-writer. Two writers on a lock-free SPSC ring lose or corrupt entries — the
exact dropped-note-off symptom being chased. A single control thread keeps one
producer and still frees notes from the frame gate. The render thread reads
navigation state cross-thread, so `NavState` must not be read raw — it is a
multi-field struct (`part`, `subject`, `group`, `item[]`, `focus_col`, `mode`,
`scope_mode`), and a torn read (new `part`, old `item[]`) can index out of range.
A double-buffered `NavState` snapshot with an atomic index flip (or a seqlock)
makes the read safe; the cost is one copy per navigation change.

Trade-off accepted: touch is now a round trip — it posts an `InputEvent`, the
control thread applies it, and the UI reflects it next frame. Imperceptible at
display rates, but it is a behavioural change.

**Alternatives:**

- Mutex around the control side — correct and minimal, but gives up the lock-free
  property on a low-rate path and keeps two callers; rejected in favour of the
  single-owner structure the control side already assumes.
- Drain again after `Present()` — halves latency with no thread, but still
  frame-gated; a long `PanelDraw` still stalls MIDI. Rejected as incomplete.
- A larger ring — only delays the drop; it does not remove the gate. Rejected.

### Decision 2: self-healing UAC2 send with an in-flight counter

**Decision:** Track outstanding `usbd_uac2_send` packets with an
`std::atomic<int>` `in_flight` (target 2, matching the `uac2_pool` depth).
`Uac2BufReleaseCb` decrements it; `SendHandler` tops up in a loop
`while (in_flight < kTargetInFlight && SendPacket())`; `SendPacket` returns
success and increments on a successful send. Run all send work — `SendHandler`
and `PrimeHandler` — on one dedicated high-priority `k_work_queue`, not the
shared system queue. `terminal_enabled` becomes `std::atomic<bool>`; `in_send` is
deleted: the single work queue serializes `SendPacket`, so the re-entrancy guard
is dead code, and its test-then-set was not atomic anyway.

**Rationale:** One `k_work_submit` per completion collapses when two releases
arrive before the handler runs — submitting an already-pending `k_work` is a
no-op — so two completions produce one send and in-flight depth degrades. A
counter plus a top-up loop is self-healing regardless of how many submissions
collapse, and a dedicated queue keeps the audio send off the queue shared with
logging and the USB stack.

`in_flight` reset: on `terminal_enabled` → false, reset `in_flight` to 0 and have
`Uac2BufReleaseCb` skip the decrement while disabled. Otherwise a pre-disable
release can land after re-enable, drift the counter, and leave the top-up loop
reading a stale non-zero count that sends nothing. The slab free still happens
unconditionally; only the counter decrement is gated on `terminal_enabled`.

**Alternatives:**

- One work item per completion (current) — fragile under any scheduling hiccup.
  Rejected.
- Timer-driven send — drifts against the host's IN tokens; the send must track
  host consumption. Rejected.

### Decision 3: short packet on underrun

**Decision:** On FIFO underrun, send the `got` frames actually produced as a
short packet; fall back to a zero-length packet only when `got == 0`.

**Rationale:** On an async IN stream a short packet is legal and is the correct
underrun signal — the host adapts. Zero-padding splices silence into the stream
and produces the sawtooth discontinuities in the captured audio.

**Alternatives:**

- Pad with zeros to the nominal frame count (current) — injects clicks. Rejected.

### Decision 4: match note-off by note number, not frequency

**Decision:** Thread the MIDI note number (`uint8_t`) through
`PanelNoteOn/Off → EngineControl::NoteOn/Off → Allocator::NoteOn/Off`; store
`note` in `Owner`; match note-off on `(part, note)`. `freq` remains a stored
render parameter, computed at note-on and carried unchanged in the event to cm85.

**Rationale:** Frequency is a derived rendering parameter, not identity. Tuning,
transpose, or per-note pitch bend breaks a frequency key outright — a note-on and
its note-off can then map to different frequencies. The note number is the stable
identity. Exact float matching is currently safe (a single non-inline
`MidiNoteToFreq`, no `-ffast-math` anywhere), but it forecloses the above and is
structurally wrong.

**Alternatives:**

- Exact float match on frequency — deterministic today, but structurally broken
  for tuning/MPE/microtuning. Rejected.

Same-pitch retrigger (a second note-on while the note still sounds) is out of
scope: the current allocator assigns a second voice, and each note-off releases
one — which one is indistinguishable by construction. Retrigger-on-same-note is a
separate allocation-policy choice, not part of this fix.

### Decision 5: panic path for All Notes Off

**Decision:** In `DrainMidi` and `host/midi_io.cc`, special-case CC 123 (All
Notes Off) → `Allocator::AllNotesOff(part)`, releasing every active voice. CC 120
(All Sound Off) is out of scope for this change: honest "silence now" semantics
need a kill event that bypasses the release envelope, which is a separate change.

**Rationale:** Today a stuck note has no software recovery — CC 123/120 fall
through to `FindControl` and are dropped as unmapped, so the only cure is a
reboot. All Notes Off converts "stuck note ruins the session" into "annoyance",
making the residual races cheap while they are chased.

**Alternatives:**

- Handle CC 120 as a release too — conflates "release" with "silence now".
  Rejected.

### Decision 6: instrument the loss paths with J-Link-readable counters

**Decision:** Add counters at fixed SDRAM addresses for the silent-drop paths:
note-ring full, CC-ring full, channel-reject, MT-reject, FIFO overflow frames,
and IPC event-ring drops.

**Rationale:** The stuck-note root cause is a three-way guess (ring drop vs
channel filter vs IPC delivery). Counters readable over J-Link turn it into a
fact in one reading, and they are cheap to keep. Counters are also the right
instrument for these high-rate paths, where logging would perturb timing.

RTT is supported on the RA8D2 — the RA SoC selects `HAS_SEGGER_RTT` when the
SEGGER module is present. The module is in the west manifest
(`modules/debug/segger`) but not checked out, so `west update segger` then
`CONFIG_USE_SEGGER_RTT` + `CONFIG_RTT_CONSOLE` enable it. Doing this first (step
1) would have surfaced the SSIE `-ENOMEM`, the MIDI 1.0 altsetting warning, and
future error lines for free. The counters stay regardless.

**Accepted risk:** `MidiRxCb` forwards only `ump.data[0]`. MT=2 (MIDI 1.0 channel
voice) is a complete message; MT=4 (MIDI 2.0 channel voice) would be a
half-message and is not handled — the MT-reject counter makes that visible if
ALSA ever negotiates it. Known and counter-visible, not fixed here.

**Alternatives:**

- Log-based diagnosis over RTT — useful once RTT is enabled, but still perturbs
  the high-rate paths; counters remain the primary instrument. Rejected as the
  sole mechanism.

### Decision 7: split the MIDI ring into a note ring and a CC ring

**Decision:** Replace the single generic word ring with two strictly-SPSC FIFO
rings: a small note ring (note-on/note-off + CC 123) and a larger CC ring
(everything else). `MidiRxCb` classifies each word once and pushes to one or the
other; the control thread drains the note ring before the CC ring. Each ring has
its own drop counter, so a lost note-off and a lost CC are separately visible.

**Rationale:** Decoupling the drain makes overflow unlikely but not impossible —
a fader sweep or SysEx can burst faster than any consumer, and a dropped note-off
is the exact stuck-note symptom being fixed. Making the single ring "note-aware"
by dropping a queued CC when a note arrives would have the producer mutate the
consumer's region (compaction, or advancing the consumer's `read` index) — the
same SPSC invariant violation Decision 1 fixed. Two plain rings keep both sides
strictly single-producer / single-consumer and still give notes priority.

Layering consequence: classification moves into `MidiRxCb` (cm85), so the ring
stops being a generic word ring.

**Alternatives:**

- Note-aware single ring (drop a queued CC to admit a note) — the producer writes
  the consumer's region. Rejected.
- Uniform FIFO drop (current) — a note-off can be the casualty. Rejected.
- Unbounded ring — no bound on SDRAM or latency. Rejected.

## 4. Interface & Type Outline

### MIDI rings (`controller/midi_ring.h`, `usb_composite.cc`)

```cpp
// Two strictly-SPSC FIFO rings at distinct fixed addresses. MidiRxCb (cm85)
// classifies each UMP word and pushes to one or the other; the control thread
// drains the note ring first.
MidiRing note_ring;   // notes + CC 123 (small, priority)
MidiRing cc_ring;     // all other words (larger)
std::atomic<std::uint32_t> note_ring_drops{0};  // fixed SDRAM address (the stuck-note signal)
std::atomic<std::uint32_t> cc_ring_drops{0};    // fixed SDRAM address
```

### UAC2 send chain (`usb_composite.cc`)

```cpp
std::atomic<int> in_flight{0};
std::atomic<bool> terminal_enabled{false};
constexpr int kTargetInFlight = 2;

bool SendPacket(const struct device *dev);  // true on successful send

void SendHandler(struct k_work *work) {
    while (g_uac2_dev && terminal_enabled.load() &&
           in_flight.load() < kTargetInFlight) {
        if (!SendPacket(g_uac2_dev)) break;
    }
}

void Uac2BufReleaseCb(...) {
    k_mem_slab_free(&send_slab, buf);
    if (terminal_enabled.load()) in_flight.fetch_sub(1);  // skip while disabled
    k_work_submit_to_queue(&audio_queue, &send_work);     // dedicated queue
}

void Uac2TerminalCb(... bool enabled ...) {
    terminal_enabled.store(enabled);
    if (enabled) k_work_submit_to_queue(&audio_queue, &prime_work);
    else in_flight.store(0);  // reset on disable
}
```

`SendHandler` and `PrimeHandler` are both submitted to `audio_queue`; the single
queue serializes `SendPacket`, so `in_send` is removed (dead guard with a
non-atomic test-then-set). `SendPacket` sends `got` frames (short packet) instead
of padding to `n`.

### Allocator (`allocator.h`)

```cpp
struct Owner {
    bool active;
    std::uint8_t note;   // identity for note-off matching
    float freq;          // render parameter, computed at note-on
    std::uint8_t part;
    std::uint32_t serial;
};

Decision NoteOn(int part, std::uint8_t note, float freq_hz);
int      NoteOff(int part, std::uint8_t note);   // was (part, float freq_hz)
void     AllNotesOff(int part);
```

### Panel / EngineControl (`nostromo/panel.{h,cc}`, `engine/engine_control.{h,cc}`)

```cpp
void PanelNoteOn(Panel *p, std::uint8_t note, std::uint8_t velocity);  // was (freq)
void PanelNoteOff(Panel *p, std::uint8_t note);                        // was (freq)
```

`MidiNoteToFreq` moves inside the note-on path (panel or engine control); the
event to cm85 still carries `freq_hz`.

### Control thread (`cm33/src/main.cc`)

```cpp
// Sole owner of Interaction + EngineControl (the single IPC producer).
void ControlThread(void *, void *, void *) {
    for (;;) { DrainMidi(...); DrainTouchQueue(...); }
}
K_THREAD_DEFINE(control, kStackBytes, ControlThread, NULL, NULL, NULL,
                /* prio */ 5, 0, 0);
```

Touch input posts `InputEvent`s into a queue drained by `ControlThread` instead
of calling `Interaction::OnInput` synchronously. The UI loop reads a
double-buffered `NavState` snapshot (atomic index flip) and draws; it never reads
`Interaction::nav` directly. CC 123 is special-cased before the surface-map
lookup.

## 5. Acceptance Criteria

- [ ] Given a note-off for a held note, the voice transitions to release and falls silent — no stuck note.
- [ ] Given sustained encoder/fader traffic during a long spectrum draw, no note-off is dropped (`note_ring_drops` stays 0 under load).
- [ ] Given a UAC2 underrun, the device sends a short packet (no zero-padded splice, no audible click).
- [ ] Given the host pauses and resumes the stream, the send chain recovers (`in_flight` returns to 2).
- [ ] Given two overlapping notes of the same pitch, each note-off releases one voice (both are released after two note-offs; the voices are indistinguishable by construction).
- [ ] Given `arecord -D hw:N,0 -f S16_LE -c 2 -r 48000 -d 60`, the capture returns in ~60.0 s (regression-tests the SSIE render clock; the prior k_timer clock measured 61.10 s ≈ 47,136 Hz).
- [ ] Given a stuck note, CC 123 releases every active voice.
- [ ] Given the loss counters are read over J-Link after reproducing a stuck note, exactly one counter names the culprit.

## 6. Approach

1. Enable RTT (`west update segger` + `CONFIG_USE_SEGGER_RTT` +
   `CONFIG_RTT_CONSOLE`) and add the loss counters; flash; reproduce; read the
   counters over J-Link to confirm the stuck-note cause.
2. Fix the UAC2 send chain: `in_flight` counter, top-up loop, one dedicated queue
   for send + prime (`in_send` removed), short packet on underrun,
   `terminal_enabled` as an atomic, and the `in_flight` reset on disable.
3. Add the control thread (sole owner of `Interaction` + `EngineControl`) with the
   double-buffered `NavState` snapshot; touch posts `InputEvent`s into it.
4. Split the MIDI ring into note + CC rings (`MidiRxCb` classifies); count drops
   per ring.
5. Refactor note identity through allocator → engine control → panel → both
   transports, then update the tests (`test_allocator.cc`, `test_engine.cc`,
   `test_mod_route.cc`, `test_split.cc`) and tools (`panel_shot.cc`, `bench.cc`,
   `live_render.cc`).
6. Add the CC 123 panic path.
7. Verify on hardware and confirm sim parity.
