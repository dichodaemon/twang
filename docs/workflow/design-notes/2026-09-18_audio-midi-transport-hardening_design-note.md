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

### Decision 1: drain MIDI on a dedicated thread

**Decision:** Move `DrainMidi` out of the cm33 `for(;;)` UI loop into a dedicated
high-priority thread that drains the MIDI ring continuously. The UI loop keeps
only `PollTouch`, `PanelDraw`, `Present`.

**Rationale:** MIDI timing must not be gated by the display frame rate. Today a
note-off can wait a full frame — 16.7 ms at 60 fps, worse under load — and a long
`PanelDraw` (the spectrum FFT) widens the gap further. The sim has no such gate,
and the sim is normative. Decoupling makes note timing independent of UI load and
stops the 256-word ring from filling during a long draw.

**Alternatives:**

- Drain again after `Present()` — halves the worst-case latency with no thread,
  but the drain is still frame-gated; a long `PanelDraw` still stalls MIDI.
  Rejected as incomplete.
- A larger ring — only delays the drop; it does not remove the gate. Rejected.

Trade-off accepted: `PanelNoteOn/Off` and `Interaction::OnInput` write plain
fields that `PanelDraw` reads, so the MIDI thread races the UI thread on those
fields. Worst case is one frame of stale playhead or control value — benign for a
synth panel.

### Decision 2: self-healing UAC2 send with an in-flight counter

**Decision:** Track outstanding `usbd_uac2_send` packets with an
`std::atomic<int>` `in_flight` (target 2, matching the `uac2_pool` depth).
`Uac2BufReleaseCb` decrements it; `SendHandler` tops up in a loop
`while (in_flight < kTargetInFlight && SendPacket())`; `SendPacket` returns
success and increments on a successful send. Run the work on a dedicated
high-priority `k_work_queue`, not the shared system queue. `terminal_enabled` and
`in_send` become `std::atomic<bool>`.

**Rationale:** One `k_work_submit` per completion collapses when two releases
arrive before the handler runs — submitting an already-pending `k_work` is a
no-op — so two completions produce one send and in-flight depth degrades. A
counter plus a top-up loop is self-healing regardless of how many submissions
collapse, and a dedicated queue keeps the audio send off the queue shared with
logging and the USB stack.

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

**Decision:** Add counters at fixed SDRAM addresses for the five silent-drop
paths: MIDI ring `Push` full, channel-reject, MT-reject, FIFO overflow frames,
and IPC event-ring drops.

**Rationale:** The stuck-note root cause is a three-way guess (ring drop vs
channel filter vs IPC delivery). Counters readable over J-Link turn it into a
fact in one reading, and they are cheap to keep. The console is unreadable (UART8,
no RTT), so logging is blind.

**Alternatives:**

- Log-based diagnosis — no readable console exists on this board. Rejected.

## 4. Interface & Type Outline

### MIDI ring drop counter (`controller/midi_ring.h`, `usb_composite.cc`)

```cpp
// MidiRxCb checks Push's return and increments a loss counter on false.
std::atomic<std::uint32_t> midi_ring_drops{0};  // fixed SDRAM address
```

### UAC2 send chain (`usb_composite.cc`)

```cpp
std::atomic<int> in_flight{0};
std::atomic<bool> terminal_enabled{false};
std::atomic<bool> in_send{false};
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
    in_flight.fetch_sub(1);
    k_work_submit_to_queue(&audio_queue, &send_work);  // dedicated queue
}
```

`SendPacket` sends `got` frames (short packet) instead of padding to `n`.

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

### DrainMidi (`cm33/src/main.cc`)

```cpp
void DrainMidiThread(void *, void *, void *) { /* drain ring in a loop */ }
K_THREAD_DEFINE(midi_drain, kStackBytes, DrainMidiThread, NULL, NULL, NULL,
                /* prio */ 5, 0, 0);
```

CC 123 special-cased before the surface-map lookup.

## 5. Acceptance Criteria

- [ ] Given a note-off for a held note, the voice transitions to release and falls silent — no stuck note.
- [ ] Given sustained encoder/fader traffic during a long spectrum draw, no note-off is dropped (`midi_ring_drops` stays 0 under load).
- [ ] Given a UAC2 underrun, the device sends a short packet (no zero-padded splice, no audible click).
- [ ] Given the host pauses and resumes the stream, the send chain recovers (`in_flight` returns to 2).
- [ ] Given two overlapping notes of the same pitch, each note-off releases exactly its own voice.
- [ ] Given a stuck note, CC 123 releases every active voice.
- [ ] Given the loss counters are read over J-Link after reproducing a stuck note, exactly one counter names the culprit.

## 6. Approach

1. Add the loss counters and the MIDI-ring drop counter; flash; reproduce; read
   the counters over J-Link to confirm the stuck-note cause.
2. Fix the UAC2 send chain: `in_flight` counter, top-up loop, dedicated queue,
   short packet on underrun, and the two atomics.
3. Decouple `DrainMidi` onto a dedicated thread.
4. Refactor note identity through allocator → engine control → panel → both
   transports, then update the tests (`test_allocator.cc`, `test_engine.cc`,
   `test_mod_route.cc`, `test_split.cc`) and tools (`panel_shot.cc`, `bench.cc`,
   `live_render.cc`).
5. Add the CC 123 panic path.
6. Verify on hardware and confirm sim parity.
