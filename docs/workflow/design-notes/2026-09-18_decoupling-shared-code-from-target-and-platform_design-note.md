---
title: Decoupling Shared Code from Target and Platform
status: accepted
date: 2026-09-18
author: Dizan Vasquez
---

# Decoupling Shared Code from Target and Platform

## 1. Problem

The shared engine/controller/panel code reaches target- and platform-specific facts through implicit backdoors — `#ifdef` guards (`TWANG_SHARED_IPC`, `TWANG_UI_SDRAM`, `__ZEPHYR__`), a weak-symbol hook, and fixed SDRAM address literals. Each backdoor couples the portable core to one target's memory map or one platform's runtime, and every additional target or platform multiplies the preprocessor branches. This note decides how to remove them: target-specific addresses consolidate into one target-only header, target behaviors and storage are injected by the caller, and the one genuine platform difference (the clock) becomes a build-selected backend, mirroring the `audio/` abstraction.

## 2. Context

The target is a dual-core RA8D2 (cm85 boot core + cm33 control core) whose two images are linked separately, so cross-core objects live at fixed SDRAM addresses both cores agree on. The desktop host (`host/main.cc`) drives the same `EngineControl`, `Panel`, `Interaction`, and scope code in a single process with no SDRAM. The backdoors in shared code, in full:

| Mechanism | Where | What it reaches |
|---|---|---|
| `#ifdef TWANG_SHARED_IPC` | `engine/engine_control.cc` | `kLossCountersAddr` bump of `ipc_event_drops` |
| `#ifdef TWANG_SHARED_IPC` | `engine/ipc_shared.h` | `kSharedIpcAddr` |
| `#ifdef TWANG_UI_SDRAM` | `nostromo/panel.cc` `PanelCreate()` | placement-new at `kScopeTapAddr` |
| weak `EngineEventsPending` | `engine/engine_control.{h,cc}` | target override signals the audio core over mbox |
| `#if defined(__ZEPHYR__)` | `nostromo/panel.cc` `NowMs()` | `k_uptime_get_32()` vs `std::chrono` |
| `#if !defined(__ZEPHYR__)` | `controller/scope_ring.h` ctor | host-only zero-fill of the scope buffer |
| address literals | `scope_tap.h`, `midi_ring.h`, `loss_counters.h` | `kScopeTapAddr`, `kNoteRingAddr`, `kCcRingAddr`, `kLossCountersAddr` |

The fixed addresses: `kSharedIpcAddr` 0x68400000, `kScopeTapAddr` 0x68500000, `kLossCountersAddr` 0x68530000, `kNoteRingAddr` 0x68580000, `kCcRingAddr` 0x68584000, and the GLCDC framebuffers 0x68600000/0x68800000 (already target-only in `glcdc_backend.cc`).

The drop counter is the only one bumped from shared code: `EngineControl::NoteOn/NoteOff/AllNotesOff` call `ipc_->events.Push(...)`, which returns false when the event ring is full; the other five loss counters are bumped from target-only files (`usb_composite.cc`, `main.cc`).

`TWANG_SHARED_IPC` and `TWANG_UI_SDRAM` are twang-defined macros (set in the target `CMakeLists.txt`); `__ZEPHYR__` is defined by the Zephyr toolchain and marks "a Zephyr embedded build" rather than "the RA8D2 target."

## 3. Decisions

### Decision 1: the event-drop counter lives in the transport, not a fixed-address block

**Decision:** Add `std::atomic<std::uint32_t> event_drops` to `SharedIpc` — the transport already injected into `EngineControl` via `Init(SharedIpc &)`. `EngineControl` bumps `ipc_->event_drops` when `Push` returns false. Remove `ipc_event_drops` from the aggregated `LossCounters` block (five counters remain).

**Rationale:** The drop is produced by `EventRing::Push` (returns false when full), not by `EngineControl`. The engine already holds the transport pointer, so the counter belongs to the transport the engine observes — no fixed address, no `#ifdef`, no new injection surface. Because `SharedIpc` is a plain object on the desktop, "fill the ring → counter reads 1" becomes unit-testable, which the fixed-address version never was.

**Alternatives:**

- `#ifdef`-guarded `reinterpret_cast` of `kLossCountersAddr` (current) — couples the engine to the target memory map; rejected.
- A weak hook `EngineEventDropped()` mirroring the existing `EngineEventsPending()` — removes the `#ifdef` but keeps a per-target override indirection; rejected in favor of the counter living where the drop happens.

### Decision 2: consolidate the fixed SDRAM addresses into one target-only header

**Decision:** A new `controller/sdram_map.h` declares every fixed address — `kSharedIpcAddr`, `kScopeTapAddr`, `kLossCountersAddr`, `kNoteRingAddr`, `kCcRingAddr`, and the GLCDC framebuffers — as the single source of truth. The address constants are removed from `ipc_shared.h`, `scope_tap.h`, `midi_ring.h`, and `loss_counters.h`, leaving those headers as pure portable structs (`SharedIpc`, `ScopeTap`, `MidiRing<C>`, `LossCounters`). Target files include `sdram_map.h` for the addresses.

**Rationale:** The fixed addresses are a property of the dual-core target's memory map, not of the shared data structures. Scattering them across four shared headers forces `#ifdef` guards in shared code and gives the memory map no single home. One target-only header makes the shared headers portable (no `#ifdef`, no addresses) and makes the map readable in one place — for the target sources and for the J-Link diagnostic script alike.

**Alternatives:**

- Keep each address beside its struct with a `#ifdef` — the current pattern; tangles preprocessor decisions as targets multiply; rejected.
- Move the whole structs (`SharedIpc`, …) into `controller/` beside their addresses — makes `controller/` depend on `engine/` (`EventRing`, `ParamBlock`), inverting the layering; rejected.

### Decision 3: inject the panel's storage and scope-ring init; the caller chooses

**Decision:** Split `PanelCreate()` into `PanelCreate()` (heap) and `PanelCreateAt(void *storage)` (placement-new into caller-provided storage), sharing one init helper. The cm33 passes `reinterpret_cast<void *>(kScopeTapAddr)`; the desktop host keeps calling `PanelCreate()` unchanged. `panel.cc` no longer references any fixed address.

The scope-ring zero-fill becomes an explicit method, not a conditional constructor: `ScopeRing()` is `= default`, a new `Clear()` zero-fills the buffer, and the heap `PanelCreate()` calls `scope_tap.ring.Clear()` while the placement `PanelCreateAt()` does not.

**Rationale:** Where the ~160 KB panel lives, and whether its scope ring needs a clean first frame, are host-versus-target initialization concerns — not properties of the panel's logic. Injecting the storage and making the zero-fill an explicit host-side step move both decisions to `main` and the construction path, where they belong. The scope-tap-is-first-member invariant is unchanged: it is structural, not a compile-time branch. The target must not zero-fill the scope ring — the audio core is already writing every slot at the shared address, and zeroing races it.

**Alternatives:**

- `#ifdef TWANG_UI_SDRAM` placement + `#if !defined(__ZEPHYR__)` zero-fill (current) — compile-time branches in shared code; rejected.
- Expose `PanelBytes()` and have the caller own a raw buffer — works, but leaks the panel's size/alignment into every caller; the heap `PanelCreate()` plus `PanelCreateAt()` keeps the size encapsulated in `panel.cc`.

### Decision 4: inject the event notification; the caller decides how to wake the consumer

**Decision:** Replace the weak `EngineEventsPending()` with an injected callback. `EngineControl::Init(SharedIpc &, EventNotify notify = nullptr)` stores a `void (*)()` notifier; `NoteOn`/`NoteOff`/`AllNotesOff` call `if (notify_) notify_();` where they called `EngineEventsPending()`. The cm33 passes a `SignalAudioCore` free function that does the mbox send; the desktop and tests pass nothing (default no-op). The weak hook declaration and definition are deleted.

**Rationale:** "Signal the consumer after queueing a note event" is target behavior, not engine logic — the desktop's audio thread drains on its own schedule and needs no signal. The weak symbol is a linker backdoor, the same shape as the `#ifdef` backdoor with a different mechanism; an injected callback makes the coupling explicit and the no-op the portable default.

**Alternatives:**

- Weak `EngineEventsPending()` (current) — a global no-op the target silently overrides; rejected.
- Move the notifier into `SharedIpc` — the notification is a producer-side (control-core) concern, not a shared-block field; rejected.

### Decision 5: make the clock a build-selected backend, mirroring `audio/`

**Decision:** A new `nostromo/clock.h` declares `std::uint32_t NowMs()` with no `#ifdef`. Two backends implement it, selected by the build: `nostromo/clock_host.cc` (`std::chrono::steady_clock`) compiled into the desktop `nostromo` library, and `nostromo/clock_zephyr.cc` (`k_uptime_get_32()`) compiled into the cm33 target. `panel.cc` includes `clock.h` and drops its local `NowMs()` and the `#if defined(__ZEPHYR__)` include guard; `host/midi_io.cc` and the cm33's bare `k_uptime_get()` callers reuse the same function.

**Rationale:** A monotonic millisecond clock is platform-specific — Zephyr's `k_uptime_get_32()`, FreeRTOS's tick count, bare-metal SysTick, and hosted `std::chrono` are all different sources, so "embedded vs hosted" is not a stable two-way split. A backend per platform keeps the shared header open to change: a new target adds `clock_<target>.cc` and one line in its build, never an `#elif` re-editing shared code — the same "add a file, not a branch" property that justifies `sdram_map.h`, storage injection, and notifier injection. It mirrors the existing `audio/` abstraction (interface header + build-selected backend), applied to a stateless primitive.

**Alternatives:**

- `#if defined(__ZEPHYR__)` in a platform header — fine for the two targets that exist today, but closed to a non-Zephyr target (one `#elif` per embedded clock source re-edits shared code); rejected for the same reason the `TWANG_*` guards are.
- Inject the clock as a callback into the panel — also open to targets, but plums a primitive through the API when a build-selected backend keeps the call site a plain `NowMs()`; rejected as more plumbing for no gain.

## 4. Interface & Type Outline

### `SharedIpc` (engine/ipc_shared.h)

```cpp
struct SharedIpc {
  EventRing events;                       // control → audio (SPSC)
  ParamBlock params;                      // control → audio (double-buffered)
  std::atomic<float> meter;               // audio → control
  std::atomic<std::uint32_t> event_drops{0};  // events.Push() dropped an event
};
```

`ipc_shared.h` keeps only the struct; `kSharedIpcAddr` and its `#ifdef` move out.

### `sdram_map.h` (controller, new — target-only)

```cpp
inline constexpr std::uintptr_t kSharedIpcAddr    = 0x68400000UL;
inline constexpr std::uintptr_t kScopeTapAddr     = 0x68500000UL;
inline constexpr std::uintptr_t kLossCountersAddr = 0x68530000UL;
inline constexpr std::uintptr_t kNoteRingAddr     = 0x68580000UL;
inline constexpr std::uintptr_t kCcRingAddr       = 0x68584000UL;
inline constexpr std::uintptr_t kFbAddr[2]        = {0x68600000UL, 0x68800000UL};
```

### Panel construction (nostromo/panel.h)

```cpp
Panel *PanelCreate();                 // heap (desktop/sim); Clear()s the scope ring
Panel *PanelCreateAt(void *storage);  // placement-new (target); no Clear()
```

### `ScopeRing` (controller/scope_ring.h)

```cpp
ScopeRing() = default;   // portable; no zero-fill
void Clear();            // zero-fill the buffer (host init; target must not call)
```

### Notification (engine/engine_control.h)

```cpp
using EventNotify = void (*)();
void Init(SharedIpc &ipc, EventNotify notify = nullptr);
```

Engine drop/notify sites: `if (!ipc_->events.Push({...})) ipc_->event_drops.fetch_add(1, relaxed);` and `if (notify_) notify_();`. `CountEventDrop()`, `EngineEventsPending()`, and both `#ifdef`s are deleted. `LossCounters` drops `ipc_event_drops`, leaving five counters.

### Clock (nostromo, new)

```cpp
// clock.h — declaration only
std::uint32_t NowMs();
// clock_host.cc (desktop build) / clock_zephyr.cc (cm33 build) — one impl per platform
```

## 5. Acceptance Criteria

- [ ] The shared sources (`engine/`, `nostromo/`, `spike/`, and the `controller/` struct headers) contain no `#ifdef TWANG_SHARED_IPC`, no `#ifdef TWANG_UI_SDRAM`, no weak `EngineEventsPending`, no `__ZEPHYR__` branch, and no fixed SDRAM address literal; platform-specific code lives in build-selected backend `.cc` files (`clock_host.cc`/`clock_zephyr.cc`), never behind a preprocessor branch.
- [ ] `TWANG_SHARED_IPC` and `TWANG_UI_SDRAM` are removed from both target `CMakeLists.txt`.
- [ ] Desktop `ctest` passes (20 tests), including a new test: filling `SharedIpc::events` to capacity and pushing once more increments `event_drops` to 1.
- [ ] Both target images build; the cm33 and cm85 flash and re-enumerate USB.
- [ ] J-Link reads the drop counter at `0x68400000 + offsetof(SharedIpc, event_drops)` and the five loss counters at `0x68530000`.
- [ ] Hardware: note-on → note-off releases the voice (no stuck note); CC 123 releases; a 400-CC flood leaves `note_ring_full` at 0.

## 6. Approach

1. Move `event_drops` into `SharedIpc`; delete `engine_control.cc`'s `#ifdef`/`CountEventDrop()`; delete `ipc_event_drops` from `LossCounters`.
2. Add `controller/sdram_map.h`; strip the address constants from the four shared headers.
3. Split `PanelCreate`/`PanelCreateAt`; add `ScopeRing::Clear()`; point the cm33 at `PanelCreateAt(kScopeTapAddr)`.
4. Inject the `EventNotify` notifier; delete the weak `EngineEventsPending`.
5. Add `nostromo/clock.h` + `clock_host.cc` + `clock_zephyr.cc`, wired into the desktop and cm33 builds respectively; remove `panel.cc`'s `NowMs()` and `__ZEPHYR__` include guard.
6. Remove the two macros from both `CMakeLists.txt`; add the `event_drops` unit test.
7. Build (desktop + both targets), re-flash, re-run the hardware checks.
