---
title: Target Address Decoupling
status: draft
date: 2026-09-18
author: Dizan Vasquez
---

# Target Address Decoupling

## 1. Problem

The shared engine/controller/panel code references target-specific facts — fixed SDRAM addresses (`0x68400000`, `0x68500000`, …) and an SDRAM-versus-heap allocation strategy — gated behind `#ifdef TWANG_SHARED_IPC` and `#ifdef TWANG_UI_SDRAM`. Each `#ifdef` is a compile-time branch that couples the portable core to one target's memory map, and every additional target multiplies the preprocessor branches. This note decides how to remove the coupling: the transport drop counter moves into the injected transport, the fixed addresses consolidate into one target-only header, and the panel's storage is injected by the caller.

## 2. Context

The target is a dual-core RA8D2 (cm85 boot core + cm33 control core) whose two images are linked separately, so cross-core objects must live at fixed SDRAM addresses both cores agree on — no linker section can give both images the same address for one object. Today those addresses are scattered across four shared headers:

- `engine/ipc_shared.h` — `kSharedIpcAddr = 0x68400000`, behind `#ifdef TWANG_SHARED_IPC`.
- `controller/scope_tap.h` — `kScopeTapAddr = 0x68500000`.
- `controller/loss_counters.h` — `kLossCountersAddr = 0x68530000`.
- `controller/midi_ring.h` — `kNoteRingAddr = 0x68580000`, `kCcRingAddr = 0x68584000`.

(The GLCDC framebuffers `0x68600000`/`0x68800000` live in `target/zephyr/cm33/src/glcdc_backend.cc`, already target-only.)

Two `#ifdef` guards carry these facts into shared code:

- `TWANG_SHARED_IPC` (defined in both target `CMakeLists.txt`) wraps `kSharedIpcAddr` and `engine_control.cc`'s `CountEventDrop()`, which `reinterpret_cast`s `kLossCountersAddr` to bump the `ipc_event_drops` counter.
- `TWANG_UI_SDRAM` (defined in the cm33 `CMakeLists.txt`) wraps the panel's placement-new in `nostromo/panel.cc` `PanelCreate()`.

The drop counter is the only one bumped from shared code: `EngineControl::NoteOn/NoteOff/AllNotesOff` call `ipc_->events.Push(...)`, which returns `false` when the event ring is full. The other five loss counters (`note_ring_full`, `cc_ring_full`, `channel_reject`, `mt_reject`, `fifo_overflow_frames`) are bumped from target-only files (`usb_composite.cc`, `main.cc`) and never needed a guard.

The desktop host (`host/main.cc`) drives the same `EngineControl`, `Panel`, and `Interaction` in a single process; it has no SDRAM, so neither the addresses nor the SDRAM placement may be referenced by the shared build.

## 3. Decisions

### Decision 1: the event-drop counter lives in the transport, not a fixed-address block

**Decision:** Add `std::atomic<std::uint32_t> event_drops` to `SharedIpc` — the transport already injected into `EngineControl` via `Init(SharedIpc &)`. `EngineControl` bumps `ipc_->event_drops` when `Push` returns false. Remove `ipc_event_drops` from the aggregated `LossCounters` block (five counters remain).

**Rationale:** The drop is produced by `EventRing::Push` (it returns false when full), not by `EngineControl`. The engine already holds the transport pointer, so the counter belongs to the transport the engine observes — no fixed address, no `#ifdef`, no new injection surface. Because `SharedIpc` is a plain object on the desktop, "fill the ring → counter reads 1" becomes unit-testable, which the fixed-address version never was.

**Alternatives:**

- `#ifdef`-guarded `reinterpret_cast` of `kLossCountersAddr` (current) — couples the engine to the target memory map; rejected.
- A weak hook `EngineEventDropped()` mirroring the existing `EngineEventsPending()` — removes the `#ifdef` but keeps a per-target override indirection; rejected in favor of the counter living where the drop happens.

### Decision 2: consolidate the fixed SDRAM addresses into one target-only header

**Decision:** A new `controller/sdram_map.h` declares every fixed address — `kSharedIpcAddr`, `kScopeTapAddr`, `kLossCountersAddr`, `kNoteRingAddr`, `kCcRingAddr`, and the GLCDC framebuffers — as the single source of truth. The address constants are removed from `ipc_shared.h`, `scope_tap.h`, `midi_ring.h`, and `loss_counters.h`, leaving those headers as pure portable structs (`SharedIpc`, `ScopeTap`, `MidiRing<C>`, `LossCounters`). Target files include `sdram_map.h` for the addresses.

**Rationale:** The fixed addresses are a property of the dual-core target's memory map, not of the shared data structures. Scattering them across four shared headers forces `#ifdef` guards in shared code and gives the memory map no single home. One target-only header makes the shared headers portable (no `#ifdef`, no addresses) and makes the map readable in one place — for the target sources and for the J-Link diagnostic script alike.

**Alternatives:**

- Keep each address beside its struct with a `#ifdef` — the current pattern; tangles preprocessor decisions as targets multiply; rejected.
- Move the whole structs (`SharedIpc`, …) into `controller/` beside their addresses — makes `controller/` depend on `engine/` (`EventRing`, `ParamBlock`), inverting the layering; rejected.

### Decision 3: inject the panel's storage; the caller chooses where it lives

**Decision:** Split `PanelCreate()` into `PanelCreate()` (heap) and `PanelCreateAt(void *storage)` (placement-new into caller-provided storage), sharing one init helper. The cm33 passes `reinterpret_cast<void *>(kScopeTapAddr)`; the desktop host keeps calling `PanelCreate()` unchanged. `panel.cc` no longer references any fixed address.

**Rationale:** Where the ~160 KB panel lives is a target-versus-desktop resource decision — SDRAM at a fixed address on the target, heap in the sim — not a property of the panel's logic. Injecting the storage moves that decision to `main`, where the memory map belongs. The scope-tap-is-first-member invariant is unchanged: it is structural (the audio core reaches the tap at the panel's base address), not a compile-time branch.

**Alternatives:**

- `#ifdef TWANG_UI_SDRAM` placement (current) — a compile-time branch in shared code; rejected.
- Expose `PanelBytes()` and have the caller own a raw buffer — works, but leaks the panel's size/alignment into every caller; the heap `PanelCreate()` plus `PanelCreateAt()` keeps the size encapsulated in `panel.cc`.

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
Panel *PanelCreate();                 // heap (desktop/sim)
Panel *PanelCreateAt(void *storage);  // placement-new (target, fixed SDRAM address)
```

### Engine drop sites (engine/engine_control.cc)

```cpp
if (!ipc_->events.Push({...})) {
  ipc_->event_drops.fetch_add(1, std::memory_order_relaxed);
}
```

`CountEventDrop()` and both `#ifdef`s are deleted. `LossCounters` drops `ipc_event_drops`, leaving five counters.

## 5. Acceptance Criteria

- [ ] The shared sources (`engine/`, `nostromo/`, and the `controller/` struct headers) contain no `#ifdef TWANG_SHARED_IPC`, no `#ifdef TWANG_UI_SDRAM`, and no fixed SDRAM address literal.
- [ ] `TWANG_SHARED_IPC` and `TWANG_UI_SDRAM` are removed from both target `CMakeLists.txt`.
- [ ] Desktop `ctest` passes (20 tests), including a new test: filling `SharedIpc::events` to capacity and pushing once more increments `event_drops` to 1.
- [ ] Both target images build; the cm33 and cm85 flash and re-enumerate USB.
- [ ] J-Link reads the drop counter at `0x68400000 + offsetof(SharedIpc, event_drops)` and the five loss counters at `0x68530000`.
- [ ] Hardware: note-on → note-off releases the voice (no stuck note); CC 123 releases; a 400-CC flood leaves `note_ring_full` at 0.

## 6. Approach

1. Move `event_drops` into `SharedIpc`; delete `engine_control.cc`'s `#ifdef`/`CountEventDrop()`; delete `ipc_event_drops` from `LossCounters`.
2. Add `controller/sdram_map.h`; strip the address constants from the four shared headers.
3. Split `PanelCreate`; point the cm33 at `PanelCreateAt(kScopeTapAddr)`.
4. Remove the two macros from both `CMakeLists.txt`; add the `event_drops` unit test.
5. Build (desktop + both targets), re-flash, re-run the hardware checks.
