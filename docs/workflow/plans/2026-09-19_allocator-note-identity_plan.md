---
title: Allocator Note Identity -- Implementation Plan
status: approved
date: 2026-09-19
author: Dizan Vasquez
design-note: ../design-notes/2026-09-18_allocator-note-identity_design-note.md
---

# Allocator Note Identity -- Implementation Plan

Companion: the [allocator note identity design note](../design-notes/2026-09-18_allocator-note-identity_design-note.md) (Decisions, interface outline, and acceptance criteria). This plan is the *how*; the note is the *what/why*.

## 2. Implementation Status

**Phases:**

1. **Allocator identity** — add `note` to `Owner` and match note-off on `(part, note)`. No deps.
2. **EngineControl + Panel propagation** — thread `note` through the control-side engine and the panel; move `MidiNoteToFreq` into `PanelNoteOn`. Depends on phase 1.
3. **Caller migration + tests** — update both MIDI transports, the four engine tests, and the four tools. Depends on phase 2.
4. **Target build + docs + final verify** — cm33 build, arch-design signature updates, full-suite gate. Depends on phase 3.

| # | Task | Status |
|---|---|---|
| 1.1 | Add `note` to `Owner`; update `Reset()` aggregate init — `engine/allocator.h`, `engine/allocator.cc` | Pending |
| 1.2 | Change `NoteOn`/`NoteOff`/`Claim` signatures; `NoteOff` matches `(part, note)` — `engine/allocator.h`, `engine/allocator.cc` | Pending |
| 1.3 | Update `tests/test_allocator.cc` — note signatures + same-note (criterion 2) case | Pending |
| 1.4 | Verify: `cmake --build build && ctest --test-dir build -R allocator` | Pending |
| 2.1 | Change `EngineControl::NoteOn`/`NoteOff` signatures — `engine/engine_control.h`, `engine/engine_control.cc` | Pending |
| 2.2 | Change `PanelNoteOn`/`PanelNoteOff` signatures; compute `freq` via `MidiNoteToFreq`; add `#include "midi.h"` — `nostromo/panel.h`, `nostromo/panel.cc` | Pending |
| 2.3 | Verify: `cmake --build build` (engine + nostromo libraries compile) | Pending |
| 3.1 | Update `host/midi_io.cc` — pass note number; drop `MidiNoteToFreq` call + `#include "midi.h"` | Pending |
| 3.2 | Update `target/zephyr/cm33/src/main.cc` `HandleMidiWord` — pass note number; drop `MidiNoteToFreq` call + `#include "midi.h"` | Pending |
| 3.3 | Update `tests/test_engine.cc` — thread note through `NoteOn`/`NoteOff` | Pending |
| 3.4 | Update `tests/test_split.cc` — thread note through `NoteOn`/`NoteOff` | Pending |
| 3.5 | Update `tests/test_mod_route.cc` — thread note through `NoteOn` | Pending |
| 3.6 | Update `tests/test_event_drops.cc` — thread note through `NoteOn` | Pending |
| 3.7 | Update `tools/panel_shot.cc` — `PanelNoteOn(p, 69, 127)` | Pending |
| 3.8 | Update `tools/wav_render.cc` — thread note through `NoteOn`/`NoteOff` | Pending |
| 3.9 | Update `tools/bench.cc` — thread note through `NoteOn` | Pending |
| 3.10 | Update `tools/live_render.cc` — thread note through `NoteOn`/`NoteOff` | Pending |
| 3.11 | Verify: `cmake --build build && ctest --test-dir build` (full desktop suite) | Pending |
| 4.1 | Update `docs/workflow/arch-designs/spike_arch-design.md` — `PanelNoteOn`/`PanelNoteOff` signatures | Pending |
| 4.2 | Update `docs/workflow/arch-designs/synth-routing_arch-design.md` — `EngineNoteOn`/`EngineNoteOff` signatures | Pending |
| 4.3 | Verify: `ZEPHYR_BASE=/workspace/zephyrproject/zephyr ZEPHYR_SDK_INSTALL_DIR=/workspace/zephyr-sdk/zephyr-sdk-1.0.1 west build -b ek_ra8d2/r7ka8d2kflcac/cm33 -d /tmp/twang-cm33-build -p always` (from `target/zephyr/cm33`); cm85 `west build -b ek_ra8d2/r7ka8d2kflcac/cm85 -d /tmp/twang-cm85-build -p always`; full desktop `cmake --build build && ctest --test-dir build` | Pending |

## 3. Architecture

### 3.1. Directory Layout

| File | Change |
|---|---|
| `engine/allocator.h` | `Owner` gains `note`; `NoteOn`/`NoteOff`/`Claim` signatures |
| `engine/allocator.cc` | `Reset()` init; `NoteOff` match on `(part, note)`; `Claim` stores `note` |
| `engine/engine_control.h` | `NoteOn`/`NoteOff` signatures |
| `engine/engine_control.cc` | `NoteOn`/`NoteOff` pass `note` to the allocator |
| `nostromo/panel.h` | `PanelNoteOn`/`PanelNoteOff` signatures |
| `nostromo/panel.cc` | `PanelNoteOn` computes `freq`; add `#include "midi.h"` |
| `host/midi_io.cc` | Pass note number; drop `MidiNoteToFreq` call + `#include "midi.h"` |
| `target/zephyr/cm33/src/main.cc` | `HandleMidiWord` passes note number; drop `MidiNoteToFreq` call + `#include "midi.h"` |
| `tests/test_allocator.cc` | Note signatures + same-note case |
| `tests/test_engine.cc` | Thread note through `NoteOn`/`NoteOff` |
| `tests/test_split.cc` | Thread note through `NoteOn`/`NoteOff` |
| `tests/test_mod_route.cc` | Thread note through `NoteOn` |
| `tests/test_event_drops.cc` | Thread note through `NoteOn` |
| `tools/panel_shot.cc` | Note argument |
| `tools/wav_render.cc` | Thread note through `NoteOn`/`NoteOff` |
| `tools/bench.cc` | Thread note through `NoteOn` |
| `tools/live_render.cc` | Thread note through `NoteOn`/`NoteOff` |
| `docs/workflow/arch-designs/spike_arch-design.md` | Signatures |
| `docs/workflow/arch-designs/synth-routing_arch-design.md` | Signatures |

No BUILD-file changes: every edited source is already in the `engine` or `nostromo` static library; no new files, no new dependencies.

### 3.2. Dependency Graph

No inter-package edge changes. `nostromo` already links `engine` PUBLIC, so `panel.cc` calling `engine::MidiNoteToFreq` needs no CMake change. The cm85 (audio) target compiles none of the edited files — the `Event` struct in `engine/ipc.h` is untouched — so the audio side is unaffected (confirmed by the phase-4 build gate).

## 4. Interface Changes

### `Owner` + allocator methods (`engine/allocator.h`, `engine/allocator.cc`)

```cpp
struct Owner {
    bool active;           ///< a note occupies this voice
    std::uint8_t note;     ///< MIDI note number; identity for note-off matching
    float freq;            ///< render parameter, computed at note-on
    std::uint8_t part;     ///< owning part
    std::uint32_t serial;  ///< note-on order, for "oldest" stealing
};

Decision NoteOn(int part, std::uint8_t note, float freq_hz);
int      NoteOff(int part, std::uint8_t note);   // was (part, float freq_hz)
// private:
void Claim(int voice, int part, std::uint8_t note, float freq_hz);
```

- `note` is the new identity key; `freq` stays stored (still read by `VoiceFreq`, asserted by `test_allocator.cc`).
- `NoteOff` matches `owner_[i].note == note` instead of `owner_[i].freq == freq_hz`.
- `Reset()` becomes `owner_[i] = Owner{false, 0, 0.0f, 0, 0};`.
- No `VoiceNote` accessor: the note does not add one; matching is asserted behaviorally via the `NoteOff` return value, and `VoiceFreq` remains for the render-freq assertions.

### `EngineControl::NoteOn`/`NoteOff` (`engine/engine_control.h`, `engine/engine_control.cc`)

```cpp
void NoteOn(int part, std::uint8_t note, float freq_hz, std::uint8_t velocity);
void NoteOff(int part, std::uint8_t note);
```

`NoteOn` forwards `note` and `freq_hz` to `alloc_.NoteOn`; the pushed `Event` is unchanged (still `{type, part, voice, velocity, freq_hz}`). `NoteOff` forwards `note`. `AllNotesOff` is unchanged (it iterates `owner_` by part, not by note).

### `PanelNoteOn`/`PanelNoteOff` (`nostromo/panel.h`, `nostromo/panel.cc`)

```cpp
void PanelNoteOn(Panel *p, std::uint8_t note, std::uint8_t velocity);  // was (float freq_hz, uint8_t velocity)
void PanelNoteOff(Panel *p, std::uint8_t note);                        // was (float freq_hz)
```

`PanelNoteOn` computes `const float freq = engine::MidiNoteToFreq(note);`, sets `p->freq = freq` (display unchanged), and calls `p->control->NoteOn(0, note, freq, velocity)`. `PanelNoteOff` calls `p->control->NoteOff(0, note)`. `panel.cc` gains `#include "midi.h"`.

## 5. Solution Breakdown

### 5.1. Allocator identity (tasks 1.1, 1.2)

- **Where:** `engine/allocator.h`, `engine/allocator.cc`.
- **Logic:** add `note` to `Owner`; `NoteOn`/`Claim` accept and store `note`; `NoteOff` scans for `(part, note)`.
- **Edge cases:** `NoteOff(part, note)` with no match returns `-1` (unchanged contract); duplicate `note` within a part (same-pitch retrigger) releases the lowest-index active voice — the same first-match order as today's freq scan, preserving parity.
- **Dependencies:** produces the `note`-carrying `Owner` and the `NoteOn`/`NoteOff`/`Claim` signatures that phase 2 consumes.
- **Done:** Verify 1.4 (`ctest -R allocator`).

### 5.2. EngineControl propagation (task 2.1)

- **Where:** `engine/engine_control.h`, `engine/engine_control.cc`.
- **Logic:** `NoteOn` becomes `(part, note, freq_hz, velocity)`; `NoteOff` becomes `(part, note)`. Both forward `note` to the allocator; the pushed `Event` is byte-identical to today (the event carries `freq`, not `note`).
- **Edge cases:** dropped note (`voice < 0`) path unchanged; `event_drops` bump unchanged.
- **Dependencies:** requires phase 1 signatures; produces the public control-side signatures the panel (phase 2.2) and the tests/tools (phase 3) consume.
- **Done:** Verify 2.3.

### 5.3. Panel propagation (task 2.2)

- **Where:** `nostromo/panel.h`, `nostromo/panel.cc`.
- **Logic:** `PanelNoteOn(p, note, velocity)` computes `freq = engine::MidiNoteToFreq(note)` once, stores it in `p->freq` (OSC display reads `p->freq` at `panel.cc:631`), and forwards `(0, note, freq, velocity)`. `PanelNoteOff(p, note)` forwards `(0, note)`.
- **Edge cases:** velocity 0 (note-on-as-off) is handled by the transport, not the panel — `PanelNoteOn` is only called with velocity ≥ 1 (unchanged).
- **Test coverage:** no dedicated panel note test. This is a no-behaviour-change refactor — the transports previously computed `MidiNoteToFreq(note)` and passed the result in, so the value flowing into `EngineControl::NoteOn` is bit-identical. Engine-side note behaviour is already covered by `test_engine`/`test_mod_route` (Verify 3.11); the note's criterion 3 is the host-sim parity check. A panel-note unit test would exercise glue and exceed the note's stated test scope.
- **Dependencies:** requires phase 2.1; `panel.cc` gains `#include "midi.h"` (no CMake change — nostromo links engine PUBLIC).
- **Done:** Verify 2.3.

### 5.4. Transports (tasks 3.1, 3.2)

- **Where:** `host/midi_io.cc` (note cases in `Poll`), `target/zephyr/cm33/src/main.cc` (`HandleMidiWord`).
- **Logic:** replace `PanelNoteOn(panel, MidiNoteToFreq(n), vel)` with `PanelNoteOn(panel, n, vel)` and `PanelNoteOff(panel, MidiNoteToFreq(n))` with `PanelNoteOff(panel, n)`. Both files drop their now-unused `#include "midi.h"`.
- **Edge cases:** the `0x90` velocity-0 and `0x80` branches both collapse to `PanelNoteOff(panel, d1)` / `PanelNoteOff(panel, msg[1])`.
- **Dependencies:** requires phase 2.2.
- **Done:** Verify 3.11 (host builds + runs) and Verify 4.3 (cm33 builds).

### 5.5. Test updates (tasks 1.3, 3.3–3.6)

- **`test_allocator.cc` (1.3):** `Fill` gains a note base so each held note has a distinct note number; every `NoteOn(part, freq)` becomes `NoteOn(part, note, freq)` and every `NoteOff(part, freq)` becomes `NoteOff(part, note)`. `VoiceFreq` assertions are unchanged (freq is still stored). Add a same-note case: two `NoteOn(0, 60, 100.0f)` then two `NoteOff(0, 60)` release both voices (criterion 2).
- **`test_engine.cc` (3.3):** `NoteOn(0, 440.0f, vel)` → `NoteOn(0, 69, 440.0f, vel)`; distinct-freq fills use distinct note numbers (e.g. `60 + i`); same-freq fills (line 134) use a constant note (parity — same note number, same-pitch retrigger is out of scope).
- **`test_split.cc` (3.4):** the loop's `NoteOn(0, freq, 127)`/`NoteOff(0, freq)` use a constant note number (notes are strictly sequential — no overlap — so a constant note is safe).
- **`test_mod_route.cc` (3.5):** `RenderNote`'s `control.NoteOn(0, freq, velocity)` → `NoteOn(0, 69, freq, velocity)`.
- **`test_event_drops.cc` (3.6):** `control.NoteOn(0, 440.0f, 127)` → `control.NoteOn(0, 69, 440.0f, 127)` (the drop path is independent of note).
- **Dependencies:** 1.3 requires phase 1; 3.3–3.6 require phase 2.
- **Done:** Verify 1.4 (allocator) and 3.11 (full suite).

### 5.6. Tool updates (tasks 3.7–3.10)

- **`panel_shot.cc` (3.7):** `PanelNoteOn(p, 440.0f, 127)` → `PanelNoteOn(p, 69, 127)`.
- **`wav_render.cc` (3.8):** `control.NoteOn(0, 440.0f, 127)` → `NoteOn(0, 69, 440.0f, 127)`; `control.NoteOff(0, 440.0f)` → `NoteOff(0, 69)`.
- **`bench.cc` (3.9):** `control.NoteOn(0, 440.0f, 127)` → `NoteOn(0, 69, 440.0f, 127)` (two call sites).
- **`live_render.cc` (3.10):** same note-on/off conversion as `wav_render.cc`.
- **Done:** Verify 3.11 (all tool targets build).

### 5.7. Documentation (tasks 4.1, 4.2)

- **`spike_arch-design.md` (4.1):** lines 190–191 `PanelNoteOn(Panel*, float freq_hz, uint8_t velocity)` / `PanelNoteOff(Panel*, float freq_hz)` → note-based signatures.
- **`synth-routing_arch-design.md` (4.2):** the `EngineNoteOn`/`EngineNoteOff` signatures (`(part, float freq_hz, uint8_t velocity)` / `(part, float freq_hz)`) → include the note number. Line 107 ("the `Event` carries `freq`, not a note number") remains true — this plan does not change the event payload — so it is left as-is.
- **Done:** Verify 4.3.

## 6. Design Decisions

### 6.1. `MidiNoteToFreq` moves into `PanelNoteOn`, not `EngineControl`

- **Decision:** `PanelNoteOn` computes `freq = engine::MidiNoteToFreq(note)` and forwards `(0, note, freq, velocity)`; `EngineControl::NoteOn` keeps an explicit `freq_hz` parameter.
- **Considered:** the note leaves the placement open ("panel or engine control"). Computing it in `EngineControl` would make the panel compute it a *second* time — the panel already needs the frequency for its OSC display (`p->freq`, read at `panel.cc:631`). Computing it in the panel once feeds both the display and the event.
- **Why:** one `MidiNoteToFreq` call, bit-identical display (the transports previously passed `MidiNoteToFreq(note)` in, so `p->freq` is unchanged), and `EngineControl::NoteOn` stays aligned with the note's `Allocator::NoteOn(part, note, freq_hz)` outline.

### 6.2. `Owner` field order follows the note

- **Decision:** `{active, note, freq, part, serial}`.
- **Why:** `note` is the identity and sits beside `active`; `freq` stays a render parameter. `Owner` is private, control-side only — no cross-core layout concern (the shared `Event` struct is untouched).

### 6.3. No `VoiceNote` accessor

- **Decision:** do not add `VoiceNote(int voice)`.
- **Why:** the note's interface outline adds none, and matching is asserted behaviorally (`NoteOff` returns the voice). `VoiceFreq` remains to assert the stored render frequency. YAGNI until a consumer needs the stored note number.

## 7. Success Criteria

### Allocator
- [ ] `NoteOff(part, note)` releases the voice by note number; matching does not read `freq` — Verify 1.4
- [ ] Two same-note notes followed by two note-offs release two voices (parity) — Verify 1.4

### EngineControl + Panel
- [ ] `EngineControl::NoteOn`/`NoteOff` and `PanelNoteOn`/`PanelNoteOff` compile with the note-based signatures — Verify 2.3
- [ ] The pushed `Event` still carries `freq` (payload unchanged) — Verify 2.3 (code review of `NoteOn`)

### Transports
- [ ] `host/midi_io.cc` and `cm33 main.cc` pass the note number directly; no `MidiNoteToFreq` call remains at those sites — Verify 3.11 / 4.3

### Host sim parity (note criterion 3)
- [ ] `test_engine` and `test_mod_route` render the same output (structural change only) — Verify 3.11

### Target
- [ ] cm33 `west build` succeeds — Verify 4.3

### Docs
- [ ] `spike_arch-design.md` and `synth-routing_arch-design.md` show note-based signatures — Verify 4.3

## 8. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../design-notes/2026-09-18_allocator-note-identity_design-note.md` | No — it describes the target state. | None. |
| `../arch-designs/spike_arch-design.md` | Yes — `PanelNoteOn`/`PanelNoteOff` signatures (lines 190–191) change to note. | Task 4.1. |
| `../arch-designs/synth-routing_arch-design.md` | Yes — `EngineNoteOn`/`EngineNoteOff` signatures (§"EngineNoteOn / EngineNoteOff") change to include note. | Task 4.2. Line 107 stays true (event payload unchanged). |
| `../../plans/2026-09-10_spike_plan.md` | No — a superseded point-in-time plan; plans are not maintained post-ship. | None. |

## 9. Cleanup

None — no diagnostic instrumentation is added; the change is structural.

## Companion/codebase divergences (flagged)

1. The note's §6 approach lists the tools as `panel_shot.cc, bench.cc, live_render.cc` — it omits `tools/wav_render.cc`, which also calls `EngineControl::NoteOn`/`NoteOff` (added to this plan as task 3.8).
2. The note's §6 lists the tests as `test_allocator, test_engine, test_mod_route, test_split` — it omits `tests/test_event_drops.cc`, which also calls `NoteOn` (added as task 3.6).
3. The note's §2 says the note path lives in `main.cc` `DrainMidi`; the actual `PanelNoteOn/Off` calls are in `HandleMidiWord` (called by `DrainMidi`). The plan uses the correct symbol.
