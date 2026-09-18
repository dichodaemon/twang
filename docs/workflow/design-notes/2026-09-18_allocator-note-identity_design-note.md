---
title: Allocator Note Identity
status: accepted
date: 2026-09-18
author: Dizan Vasquez
---

# Allocator Note Identity

## 1. Problem

The voice allocator keys note-off on the note's frequency:
`Allocator::NoteOff(part, freq_hz)` matches `owner_[i].freq == freq_hz`, an exact
float comparison. Frequency is a derived rendering parameter, not a note's
identity. It works today — `MidiNoteToFreq` is a single non-inline function and
there is no `-ffast-math` anywhere, so a note-on and its note-off of the same
note produce bit-identical floats — but it is structurally wrong: the moment
tuning, transpose, or per-note pitch bend exists, a note-on and its note-off can
map to different frequencies, the lookup fails, and the voice sticks at sustain.

## 2. Context

- `engine/allocator.{h,cc}` — `Owner { active, freq, part, serial }`;
  `NoteOff(part, freq_hz)` returns the first active voice whose `freq == freq_hz`.
- `engine/engine_control.{h,cc}` — `NoteOn/Off(part, freq_hz, …)` forward the
  frequency to the allocator and push a `{…, freq_hz}` event to cm85.
- Callers compute the frequency from the note number via `engine::MidiNoteToFreq`
  in `host/midi_io.cc` and `target/zephyr/cm33/src/main.cc` `DrainMidi`, then pass
  only the frequency into `PanelNoteOn/Off` (`nostromo/panel.{h,cc}`).

## 3. Decisions

### Decision 1: match note-off on (part, note number), not frequency

**Decision:** Thread the MIDI note number (`uint8_t`) through
`PanelNoteOn/Off → EngineControl::NoteOn/Off → Allocator::NoteOn/Off`; store
`note` in `Owner`; match note-off on `(part, note)`. `freq` remains a stored
render parameter, computed at note-on and carried unchanged in the event to cm85.

**Rationale:** The note number is the stable identity; the frequency is derived
from it and is only a rendering parameter. Using frequency as the key forecloses
tuning, transpose, per-note pitch bend (MPE), and microtuning — any of which
makes a note-on and its note-off map to different frequencies and leaves the
voice stuck. An integer key removes the ambiguity outright.

**Alternatives:**

- Exact float match on frequency (current) — deterministic today, but structurally
  broken for tuning/MPE/microtuning. Rejected.

Same-pitch retrigger (a second note-on while the note still sounds) is out of
scope: the allocator still assigns a second voice, and each note-off releases one
— which is indistinguishable by construction. Retrigger-on-same-note is a
separate allocation-policy choice.

## 4. Interface & Type Outline

### `Owner` (`allocator.h`)

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
```

### Panel / EngineControl (`nostromo/panel.{h,cc}`, `engine/engine_control.{h,cc}`)

```cpp
void PanelNoteOn(Panel *p, std::uint8_t note, std::uint8_t velocity);  // was (freq)
void PanelNoteOff(Panel *p, std::uint8_t note);                        // was (freq)
```

`MidiNoteToFreq` moves inside the note-on path (panel or engine control); the
event to cm85 still carries `freq_hz`.

## 5. Acceptance Criteria

- [ ] Given a note-off for a held note, the voice transitions to release and falls silent.
- [ ] Given two overlapping notes of the same pitch, two note-offs release two voices (which is indistinguishable by construction).
- [ ] Given `test_allocator.cc` and `test_engine.cc`, `NoteOff` matches by note number, not by frequency.

## 6. Approach

1. Change `Owner` to carry `note`; match `NoteOff` on `(part, note)`.
2. Propagate the signature through engine control, panel, and both MIDI
   transports (`host/midi_io.cc`, `target/zephyr/cm33/src/main.cc`).
3. Update the tests (`test_allocator.cc`, `test_engine.cc`, `test_mod_route.cc`,
   `test_split.cc`) and tools (`panel_shot.cc`, `bench.cc`, `live_render.cc`).
4. Verify sim parity and the criteria above.
