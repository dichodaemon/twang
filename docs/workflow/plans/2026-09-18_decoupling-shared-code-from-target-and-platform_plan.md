---
title: Decoupling Shared Code from Target and Platform -- Implementation Plan
status: issued
date: 2026-09-18
author: Dizan Vasquez
design-note: ../design-notes/2026-09-18_decoupling-shared-code-from-target-and-platform_design-note.md
---

# Decoupling Shared Code from Target and Platform -- Implementation Plan

Companion: `2026-09-18_decoupling-shared-code-from-target-and-platform_design-note.md` (the decisions, rationale, and acceptance criteria). This plan sequences the implementation; it does not repeat the note's investigation or rationale.

Scope: remove every target/platform backdoor from shared code — the `#ifdef TWANG_SHARED_IPC` / `#ifdef TWANG_UI_SDRAM` / `#if defined(__ZEPHYR__)` guards, the weak `EngineEventsPending` hook, and the fixed SDRAM address literals in `ipc_shared.h`, `scope_tap.h`, `midi_ring.h`, and `loss_counters.h` — replacing them with injection (counter + notifier + panel storage), one target-only address header (`sdram_map.h`), and a build-selected clock backend. The dual-core architecture is unchanged: the block still lives at the same fixed addresses, reached the same way; only *where shared code learns about them* changes.

## 2. Implementation Status

**Phases:**

1. **Engine transport** — `event_drops` counter + injected `EventNotify` notifier (Decisions 1 & 4; no deps).
2. **Panel storage + scope ring** — `PanelCreate`/`PanelCreateAt` split + `ScopeRing::Clear()` (Decision 3; no deps).
3. **Clock backend** — `clock.h` + host/zephyr backends (Decision 5; no deps).
4. **Memory map + target integration** — `sdram_map.h`, strip the four shared headers, rewire the target files, drop the two macros (Decision 2; depends on 1, 2, 3).
5. **Hardware verification** — flash both cores, re-run the board checks (depends on 4).

| # | Task | Status |
|---|---|---|
| 1.1 | Add `std::atomic<std::uint32_t> event_drops{0};` to `SharedIpc` in `engine/ipc_shared.h` | Pending |
| 1.2 | Remove `ipc_event_drops` (field + `Reset()` line) from `LossCounters` in `controller/loss_counters.h` | Pending |
| 1.3 | Add `using EventNotify = void (*)();`, `notify_` member, `Init(SharedIpc &, EventNotify = nullptr)`; delete weak `EngineEventsPending()` decl in `engine/engine_control.h` | Pending |
| 1.4 | Rewrite `engine/engine_control.cc`: delete `#ifdef TWANG_SHARED_IPC` include, weak `EngineEventsPending` def, `CountEventDrop()` (both branches); inline `ipc_->event_drops.fetch_add(1, relaxed)` at the 3 drop sites; `if (notify_) notify_();` at the 3 notify sites | Pending |
| 1.5 | Write test: `tests/test_event_drops.cc` (fill ring to `kCapacity-1`, one `NoteOn` more, assert `event_drops == 1`); register `test_event_drops` + `event_drops` in `CMakeLists.txt` | Pending |
| 1.6 | Verify: `cmake -B build && cmake --build build && ctest --test-dir build` → 21/21 pass | Pending |
| 2.1 | `controller/scope_ring.h`: ctor → `ScopeRing() = default;` (drop `#if !defined(__ZEPHYR__)` zero-fill); add `void Clear();` | Pending |
| 2.2 | `controller/scope_ring.cc`: implement `ScopeRing::Clear()` (relaxed-store zero-fill of `buf_`) | Pending |
| 2.3 | `nostromo/panel.h`: add `Panel *PanelCreateAt(void *storage);` declaration | Pending |
| 2.4 | `nostromo/panel.cc`: split `PanelCreate()` into a shared init helper + heap `PanelCreate()` (calls `scope_tap.ring.Clear()`) + `PanelCreateAt()` (placement-new, no Clear); remove `#ifdef TWANG_UI_SDRAM`; preserve the `offsetof(Panel, scope_tap) == 0` static_assert | Pending |
| 2.5 | Verify: `cmake -B build && cmake --build build && ctest --test-dir build` → panel/interaction/panel_pages/bindings/gestures/surface green | Pending |
| 3.1 | Add `nostromo/clock.h`: `namespace nostromo { std::uint32_t NowMs(); }` | Pending |
| 3.2 | Add `nostromo/clock_host.cc` (`std::chrono::steady_clock`); add to `nostromo` lib sources in `CMakeLists.txt` | Pending |
| 3.3 | Add `nostromo/clock_zephyr.cc` (`k_uptime_get_32()` via `<zephyr/kernel.h>`) | Pending |
| 3.4 | `nostromo/panel.cc`: `#include "clock.h"`; delete local `NowMs()` + `#if defined(__ZEPHYR__)` include guard (and the `<chrono>` / `<zephyr/kernel.h>` includes) | Pending |
| 3.5 | `host/midi_io.cc`: `#include "clock.h"`; delete local `NowMs()` + `<chrono>` include; `ev.t_ms = NowMs()` → `ev.t_ms = nostromo::NowMs()` | Pending |
| 3.6 | Verify: `cmake -B build && cmake --build build && ctest --test-dir build` | Pending |
| 4.1 | Add `controller/sdram_map.h` (all six addresses + "justified exception, not a pattern" comment) | Pending |
| 4.2 | `engine/ipc_shared.h`: delete `#ifdef TWANG_SHARED_IPC` block + `kSharedIpcAddr` | Pending |
| 4.3 | `controller/scope_tap.h`: delete `kScopeTapAddr` (+ move its rationale comment) | Pending |
| 4.4 | `controller/midi_ring.h`: delete `kNoteRingAddr` + `kCcRingAddr` (+ clean address-ref comment text) | Pending |
| 4.5 | `controller/loss_counters.h`: delete `kLossCountersAddr` (+ clean comment) | Pending |
| 4.6 | `target/zephyr/cm33/src/glcdc_backend.cc`: delete local `kFbAddr`; `#include "sdram_map.h"` | Pending |
| 4.7 | `target/zephyr/cm33/src/main.cc`: `#include "sdram_map.h"` + `"clock.h"`; replace `namespace engine { EngineEventsPending }` with file-static `SignalAudioCore()`; `Init(..., SignalAudioCore)`; `PanelCreateAt(reinterpret_cast<void *>(kScopeTapAddr))`; `ev.t_ms = nostromo::NowMs()`; `engine::kSharedIpcAddr` → `kSharedIpcAddr` | Pending |
| 4.8 | `target/zephyr/cm85/src/main.cc`: `#include "sdram_map.h"`; `engine::kSharedIpcAddr` → `kSharedIpcAddr` | Pending |
| 4.9 | `target/zephyr/cm85/src/usb_composite.cc`: `#include "sdram_map.h"` | Pending |
| 4.10 | `target/zephyr/cm33/CMakeLists.txt`: delete `target_compile_definitions(app PRIVATE TWANG_SHARED_IPC TWANG_UI_SDRAM)`; add `${NOSTROMO_DIR}/clock_zephyr.cc` to `target_sources` | Pending |
| 4.11 | `target/zephyr/cm85/CMakeLists.txt`: delete `target_compile_definitions(app PRIVATE TWANG_SHARED_IPC)` | Pending |
| 4.12 | Verify: `west build` cm33 + cm85 (out-of-tree `/tmp/twang-cm33-build`, `/tmp/twang-cm85-build`); append size rows to `docs/references/memory-budget_reference.md` | Pending |
| 4.13 | Verify: desktop `ctest --test-dir build` still green (shared headers changed) | Pending |
| 4.14 | Update `spike_arch-design.md` §8 Panel contract: add `Panel *PanelCreateAt(void *storage);` | Pending |
| 5.1 | Flash cm85 + cm33 via J-Link (no-reset attach pattern; `loadfile` both ELFs) | Pending |
| 5.2 | Re-establish `aconnect 24:0 28:1` | Pending |
| 5.3 | Verify: note-on→note-off releases (no stuck note); CC 123 releases; 400-CC flood leaves `note_ring_full` at 0 | Pending |
| 5.4 | Verify: J-Link reads `event_drops` at `0x68400000 + offsetof(SharedIpc, event_drops)` and the five loss counters at `0x68530000` | Pending |

Phases 1–3 are mutually independent (disjoint files except `panel.cc`, which Phases 2 and 3 touch at different sites, sequentially). Phase 4 depends on all of 1–3: it strips the addresses that Phases 1 and 2 stop consuming, and it rewires the target files to the APIs those phases introduce. Phase 5 is the board gate.

## 3. Architecture

### 3.1. Directory Layout

| File | Change |
|---|---|
| `engine/ipc_shared.h` | Add `event_drops` field; remove `kSharedIpcAddr` + `#ifdef TWANG_SHARED_IPC` |
| `engine/engine_control.h` | Add `EventNotify` + `notify_` + `Init(..., notify)`; remove weak `EngineEventsPending` decl |
| `engine/engine_control.cc` | Remove `#ifdef` include + `CountEventDrop` + weak def; inline drop/notify sites |
| `controller/loss_counters.h` | Remove `ipc_event_drops` field + `Reset()` line; remove `kLossCountersAddr` |
| `controller/scope_tap.h` | Remove `kScopeTapAddr` |
| `controller/midi_ring.h` | Remove `kNoteRingAddr` + `kCcRingAddr` |
| `controller/scope_ring.h` | Ctor → `= default`; add `Clear()` decl |
| `controller/scope_ring.cc` | Implement `Clear()` |
| `controller/sdram_map.h` | **New** — the six fixed addresses, target-only |
| `nostromo/panel.h` | Add `PanelCreateAt` decl |
| `nostromo/panel.cc` | Split create; remove `#ifdef TWANG_UI_SDRAM`; remove `NowMs()` + `__ZEPHYR__` include guard |
| `nostromo/clock.h` | **New** — `nostromo::NowMs()` declaration |
| `nostromo/clock_host.cc` | **New** — desktop backend (`std::chrono`) |
| `nostromo/clock_zephyr.cc` | **New** — Zephyr backend (`k_uptime_get_32`) |
| `host/midi_io.cc` | Use `nostromo::NowMs()`; drop local `NowMs()` + `<chrono>` |
| `target/zephyr/cm33/src/main.cc` | `sdram_map.h` + `clock.h`; `SignalAudioCore`; `PanelCreateAt`; `NowMs()`; address renames |
| `target/zephyr/cm33/src/glcdc_backend.cc` | `sdram_map.h`; drop local `kFbAddr` |
| `target/zephyr/cm85/src/main.cc` | `sdram_map.h`; `kSharedIpcAddr` rename |
| `target/zephyr/cm85/src/usb_composite.cc` | `sdram_map.h` |
| `target/zephyr/cm33/CMakeLists.txt` | Drop both macros; add `clock_zephyr.cc` |
| `target/zephyr/cm85/CMakeLists.txt` | Drop `TWANG_SHARED_IPC` |
| `CMakeLists.txt` | Add `clock_host.cc` to `nostromo` lib; register `test_event_drops` |
| `tests/test_event_drops.cc` | **New** — transport drop-counter test |
| `docs/references/memory-budget_reference.md` | Append size rows (tasks 4.12) |
| `docs/workflow/arch-designs/spike_arch-design.md` | Add `PanelCreateAt` to the Panel contract (task 4.14) |

### 3.2. Dependency Graph

No new *desktop* inter-package edges: `clock_host.cc` joins the existing `nostromo` library (which `host` and the tests already link); the `host` executable and `panel.cc`/`midi_io.cc` consume `nostromo::NowMs()` from it. The only new cross-build edge is target-only: `clock_zephyr.cc` compiles into the cm33 image (`nostromo` → `zephyr/kernel.h`), and `sdram_map.h` is included solely by `target/` files. Layering is unchanged — `controller/` struct headers become pure POD with no `engine/` or target coupling.

## 4. Interface Changes

### `SharedIpc` (`engine/ipc_shared.h`)

```cpp
struct SharedIpc {
  EventRing events;                       // control → audio (SPSC)
  ParamBlock params;                      // control → audio (double-buffered)
  std::atomic<float> meter;               // audio → control
  std::atomic<std::uint32_t> event_drops{0};  // events.Push() dropped an event
};
```

`event_drops` is `std::atomic<std::uint32_t>` (lock-free on ARM, trivially constructible — same shape as the existing transport fields). The `static_assert(std::atomic<float>::is_always_lock_free)` stays. `kSharedIpcAddr` and its `#ifdef TWANG_SHARED_IPC` move to `sdram_map.h`.

### `LossCounters` (`controller/loss_counters.h`)

Removed field: `std::atomic<std::uint32_t> ipc_event_drops` (and its `Reset()` line). Five counters remain: `note_ring_full`, `cc_ring_full`, `channel_reject`, `mt_reject`, `fifo_overflow_frames`. No target file bumps `ipc_event_drops` (verified: the only producer was `EngineControl::CountEventDrop`, deleted in task 1.4).

### `EngineControl` (`engine/engine_control.h`)

```cpp
using EventNotify = void (*)();                      // added, namespace scope
void Init(SharedIpc &ipc, EventNotify notify = nullptr);  // was Init(SharedIpc &)
// private: EventNotify notify_ = nullptr;            // added
```

Removed: `void EngineEventsPending();` (weak-hook declaration). All existing `Init(ipc)` callers — `host/main.cc`, `tests/test_engine.cc`, `tests/test_interaction.cc`, `tests/test_mod_route.cc`, `tests/test_panel.cc`, `tests/test_panel_pages.cc`, `tests/test_param_atomic.cc`, `tests/test_split.cc`, and `tools/{bench,live_render,panel_shot,wav_render}.cc` — pass one argument and are unaffected by the defaulted second parameter; only the cm33 (`target/zephyr/cm33/src/main.cc`) adds a notifier (task 4.7). The weak definition in `engine_control.cc` is deleted; `NoteOn`/`NoteOff` call `if (notify_) notify_();` where they called `EngineEventsPending()`, and `AllNotesOff` does the same inside its `if (released)` block.

### `sdram_map.h` (`controller/`, new — target-only)

```cpp
inline constexpr std::uintptr_t kSharedIpcAddr    = 0x68400000UL;
inline constexpr std::uintptr_t kScopeTapAddr     = 0x68500000UL;
inline constexpr std::uintptr_t kLossCountersAddr = 0x68530000UL;
inline constexpr std::uintptr_t kNoteRingAddr     = 0x68580000UL;
inline constexpr std::uintptr_t kCcRingAddr       = 0x68584000UL;
inline constexpr std::uintptr_t kFbAddr[2]        = {0x68600000UL, 0x68800000UL};
```

Carries the "Justified exception, not a pattern" rationale (two separately-linked cores rendezvous at fixed SDRAM addresses; no linker section can give both cores the same object). The address-rationale comments move here from the four shared headers.

### `Panel` construction (`nostromo/panel.h`)

```cpp
Panel *PanelCreate();                 // heap (desktop/sim); Clear()s the scope ring
Panel *PanelCreateAt(void *storage);  // placement-new (target); no Clear()
```

`PanelCreate()` becomes a thin wrapper over `PanelCreateAt` + `scope_tap.ring.Clear()`. The cm33 calls `PanelCreateAt(reinterpret_cast<void *>(kScopeTapAddr))` (task 4.7). `panel.cc` no longer names any fixed address, including in comments.

### `ScopeRing` (`controller/scope_ring.h`)

```cpp
ScopeRing() = default;   // portable; no zero-fill
void Clear();            // zero-fill the buffer (host init; the target must NOT call)
```

The `#if !defined(__ZEPHYR__)` zero-fill leaves the constructor and becomes `Clear()` (implemented in `scope_ring.cc`).

### Clock (`nostromo/clock.h`, new)

```cpp
// clock.h — declaration only
namespace nostromo {
std::uint32_t NowMs();
}
// clock_host.cc (desktop build) / clock_zephyr.cc (cm33 build) — one impl per platform
```

`NowMs()` is in `namespace nostromo` (mirrors the `audio::Output` abstraction's namespace). `panel.cc`'s internal callers resolve it unqualified; `host/midi_io.cc` calls `nostromo::NowMs()`.

## 5. Solution Breakdown

### 5.1. Transport counter (tasks 1.1, 1.2, 1.4)

**Where:** `SharedIpc::event_drops` + `EngineControl` drop sites.

`EventRing::Push` returns `false` when the ring is full (max occupancy `kCapacity - 1 = 31`, per `test_ring.cc`). The engine already holds the transport pointer, so the counter lives on the transport and is bumped inline: `if (!ipc_->events.Push({...})) ipc_->event_drops.fetch_add(1, std::memory_order_relaxed);` at the three drop sites (`NoteOn`, `NoteOff`, `AllNotesOff`). The `#ifdef TWANG_SHARED_IPC` include and the `CountEventDrop()` helper (both branches) are deleted; `LossCounters` loses `ipc_event_drops`.

**Edge cases:** `NoteOn` returns early when the allocator has no voice (`d.voice < 0`) — no push, no bump, unchanged. `AllNotesOff` bumps once per released voice whose push fails.

**Dependencies:** produces `SharedIpc::event_drops` (consumed by task 1.5's test); requires nothing.

**Done condition:** task 1.6 — desktop build + `ctest` green, and the new test (1.5) asserts the bump.

### 5.2. Notifier injection (tasks 1.3, 1.4)

**Where:** `EngineControl::Init` + the three notify sites.

`Init` stores the `EventNotify` function pointer; `NoteOn`/`NoteOff` call `if (notify_) notify_();` after queueing, and `AllNotesOff` does the same inside `if (released)`. The weak `EngineEventsPending` (declaration, definition, and the cm33 override) is deleted.

**Edge cases:** the default `nullptr` makes the desktop and every test a no-op; a null check guards the call so `Init` callers need not pass anything.

**Dependencies:** produces the `Init(SharedIpc &, EventNotify)` signature consumed by task 4.7.

**Done condition:** task 1.6 — existing `test_engine` (AllNotesOff), `test_interaction`, and the other `control.Init(ipc)` tests still pass with the default notifier.

### 5.3. event_drops test (task 1.5)

**Where:** `tests/test_event_drops.cc` (new), registered in `CMakeLists.txt`.

```cpp
SharedIpc ipc;
EngineControl control;
control.Init(ipc);                       // default null notifier; resets the rings
Check(ipc.event_drops.load() == 0, "starts at zero");
const int n = static_cast<int>(EventRing::kCapacity) - 1;   // 31 = max occupancy
for (int i = 0; i < n; ++i)
    ipc.events.Push({Event::Type::kNoteOn, 0, 0, 127, 100.0f + i});
Check(ipc.event_drops.load() == 0, "no drop under capacity");
control.NoteOn(0, 440.0f, 127);          // allocator grants voice 0; Push fails
Check(ipc.event_drops.load() == 1, "one drop counted");
```

The test drives the real path — `EngineControl::NoteOn` → `Push` fails → counter bumps — rather than hand-bumping the field. It fills the ring directly (bypassing the allocator) so the `NoteOn` is guaranteed to hit a full ring with a free voice.

**Edge cases:** the direct `Push` fill must stop at `kCapacity - 1` (not `kCapacity`); `EventRing` reserves one slot.

**Dependencies:** requires task 1.1–1.4 (field + inline bump).

**Done condition:** task 1.6 — `ctest -R event_drops` passes; suite total is 21.

### 5.4. ScopeRing Clear (tasks 2.1, 2.2)

**Where:** `controller/scope_ring.{h,cc}`.

The constructor becomes `= default` (no zero-fill); a new `Clear()` zero-fills `buf_` with relaxed stores. The rationale for *not* zero-filling on the target stays as a comment: the ring lives at a fixed SDRAM address the audio core writes concurrently, and zeroing it races the producer.

**Edge cases:** `Clear()` must not touch `write_` (only the buffer); `Reset()` remains the producer-side index reset.

**Dependencies:** produces `Clear()`, consumed by task 2.4 (heap `PanelCreate`).

**Done condition:** task 2.5 — desktop build + panel tests green (heap create now clears via `Clear()`, preserving the host behavior the old ctor provided).

### 5.5. Panel storage split (tasks 2.3, 2.4)

**Where:** `nostromo/panel.{h,cc}`.

`PanelCreate()` becomes a wrapper: heap-allocate (`new Panel`), then `scope_tap.ring.Clear()`, then the shared init (traces memset + `dyn[]` slots). `PanelCreateAt(void *storage)` placement-news into `storage` and runs the shared init *without* `Clear()`. The `#ifdef TWANG_UI_SDRAM` branch is deleted; `panel.cc` no longer names `kScopeTapAddr` (the `offsetof(Panel, scope_tap) == 0` static_assert and its structural rationale are preserved, reworded without the address literal).

**Edge cases:** the target must not zero-fill the scope ring (it races the audio core); the heap path must, so desktop reads-before-first-write stay in range.

**Dependencies:** requires `Clear()` (5.4); produces `PanelCreateAt`, consumed by task 4.7.

**Done condition:** task 2.5 — panel/interaction/panel_pages/bindings/gestures/surface tests green on the heap path.

### 5.6. Clock backend (tasks 3.1–3.5)

**Where:** `nostromo/clock.h` + `clock_host.cc` + `clock_zephyr.cc`; `panel.cc`; `host/midi_io.cc`.

`nostromo::NowMs()` is declared once; `clock_host.cc` implements it with `std::chrono::steady_clock` (compiled into the desktop `nostromo` lib), `clock_zephyr.cc` with `k_uptime_get_32()` (compiled into the cm33 image). `panel.cc` drops its local `NowMs()` and the `#if defined(__ZEPHYR__)` include guard; `host/midi_io.cc` drops its own `NowMs()` and `<chrono>`, calling `nostromo::NowMs()`.

**Edge cases:** `k_uptime_get_32()` (32-bit, ~49-day wrap) is equivalent to the current `static_cast<std::uint32_t>(k_uptime_get())` at the cm33 `ev.t_ms` site; `t_ms` is `std::uint32_t`, so the low-32-bit semantics are unchanged.

**Dependencies:** requires nothing; produces `nostromo::NowMs()`, consumed by `panel.cc`, `host/midi_io.cc`, and task 4.7.

**Done condition:** task 3.6 — desktop build + `ctest` green (panel + midi_io paths).

### 5.7. sdram_map consolidation (tasks 4.1–4.6)

**Where:** `controller/sdram_map.h` (new) + the four shared headers + `glcdc_backend.cc`.

All six fixed addresses move into one header. The four shared headers become pure structs with no `#ifdef` and no address literal; their address-rationale comments follow the constants into `sdram_map.h`. `glcdc_backend.cc`'s file-local `kFbAddr` is deleted in favor of the shared map.

**Edge cases:** `sdram_map.h` is header-only `inline constexpr` (no `.cc`, no linker section); it is included only by `target/` files, never by desktop code. The `kFbAddr` move is name-compatible — `glcdc_backend.cc` keeps its unqualified `kFbAddr[...]` uses, now resolving to the global constant.

**Dependencies:** requires Phase 1 (engine no longer reads `kLossCountersAddr`) and Phase 2 (panel no longer reads `kScopeTapAddr`). Produces the single address source consumed by tasks 4.7–4.9.

**Done condition:** tasks 4.12–4.13 — both target images build; desktop `ctest` still green.

### 5.8. Target rewiring (tasks 4.7–4.11)

**Where:** cm33/cm85 `main.cc`, `usb_composite.cc`, both target `CMakeLists.txt`.

cm33 `main.cc`: the `namespace engine { EngineEventsPending }` override becomes a file-static `SignalAudioCore()` (mbox send on channel 0), passed as the second `Init` argument; `PanelCreate()` → `PanelCreateAt(reinterpret_cast<void *>(kScopeTapAddr))`; `ev.t_ms = static_cast<std::uint32_t>(k_uptime_get())` → `ev.t_ms = nostromo::NowMs()`; `engine::kSharedIpcAddr` → `kSharedIpcAddr`. cm85 `main.cc` and `usb_composite.cc` add the `sdram_map.h` include and drop the `engine::` qualifier on `kSharedIpcAddr`. Both `CMakeLists.txt` drop their `target_compile_definitions` lines; cm33 adds `${NOSTROMO_DIR}/clock_zephyr.cc` (and *not* `clock_host.cc`).

**Edge cases:** `SignalAudioCore` is file-static (only `main.cc` uses it); its `void()` signature matches `EventNotify`. The cm33 still includes `<zephyr/kernel.h>` for the msgq/thread calls, so `clock.h` is the only added include for the clock.

**Dependencies:** requires Phases 1–3 (`Init` signature, `PanelCreateAt`, `NowMs`) and task 4.1 (`sdram_map.h`).

**Done condition:** tasks 4.12–4.13 — both images build; desktop unaffected.

### 5.9. Hardware verification (tasks 5.1–5.4)

**Where:** board (J-Link + ALSA MIDI + USB audio).

Flash both ELFs (cm85 first, then cm33) via the no-reset J-Link pattern; re-establish `aconnect 24:0 28:1`; then re-run the note's hardware criteria: note-on→off release, CC 123 release, 400-CC flood leaving `note_ring_full` at 0, and read `event_drops` at `0x68400000 + offsetof(SharedIpc, event_drops)` plus the five loss counters at `0x68530000`.

**Dependencies:** requires Phase 4.

**Done condition:** tasks 5.3–5.4 — the note's acceptance criteria 4–6 hold on hardware.

## 6. Design Decisions

### 6.1. `NowMs()` lives in `namespace nostromo`

**Decision:** `nostromo/clock.h` declares `nostromo::NowMs()`.

**Considered:** a global `::NowMs()` (like the current file-local one); a per-build `#if` in a header.

**Why:** mirrors the `audio::Output` abstraction (interface header + build-selected backend under a namespace). `panel.cc`'s callers already sit inside `namespace nostromo`, so unqualified calls resolve cleanly; `host/midi_io.cc` uses the explicit `nostromo::` prefix like its other nostromo references.

### 6.2. `sdram_map.h` is header-only `inline constexpr`

**Decision:** the six addresses are `inline constexpr std::uintptr_t` in a single header, no `.cc`.

**Considered:** `extern const` + a `.cc`; scattering per-struct again.

**Why:** matches the existing `k*Addr` idiom (they were `inline constexpr` in their respective headers); a compile-time constant needs no definition unit, and a single header keeps the memory map readable in one place for target sources and the J-Link script alike. It is included only by `target/` files, so it never enters the desktop build graph.

### 6.3. `SignalAudioCore` is file-static in cm33 `main.cc`

**Decision:** replace the `namespace engine { EngineEventsPending }` weak-symbol override with an anonymous-namespace `SignalAudioCore()` passed to `Init`.

**Considered:** a named `engine` function; a lambda; keeping a global hook.

**Why:** the signal is a single-use, producer-side concern owned by `main`; file-static scope makes that explicit and removes the global symbol entirely. The `void()` signature matches `EventNotify` directly.

### 6.4. The event_drops test drives `EngineControl::NoteOn`, not a hand-bump

**Decision:** the test fills the ring directly, then exercises the real `NoteOn` → `Push`-fails → counter-bump path.

**Considered:** bumping `ipc.event_drops` manually (tautological); filling via 32 `NoteOn` calls (allocator voice-steal muddies the count).

**Why:** the counter is only meaningful if the production wiring bumps it on a real failed push; the direct-fill + one-`NoteOn` shape isolates that single behavior. Filling via `NoteOn` would interleave allocator decisions and can't guarantee a fixed number of pushes.

### 6.5. `event_drops` uses a defaulted atomic member, not a `Reset()` entry

**Decision:** `std::atomic<std::uint32_t> event_drops{0}`; no change to any `Reset()`.

**Considered:** adding `event_drops` to a `SharedIpc::Reset()` (none exists) or to `EventRing::Reset()`.

**Why:** the counter is diagnostic and self-initializing via the `{0}` default member initializer; `EngineControl::Init` already resets the rings without touching the counter, and a boot-time counter reset has no observable value (the SDRAM backing is zeroed at init on the target by the same path that resets the other transport fields). Keeping it out of `Reset()` avoids a semantic decision the note didn't make.

## 7. Success Criteria

### Engine transport (Phase 1)
- [ ] `SharedIpc` compiles with `event_drops`; `LossCounters` has five counters, no `ipc_event_drops` (task 1.6).
- [ ] `test_event_drops` passes: `event_drops` reads 1 after one push into a full ring (task 1.6).
- [ ] `control.Init(ipc)` (no notifier) still passes `test_engine`/`test_interaction` (task 1.6).

### Panel + scope ring (Phase 2)
- [ ] `ScopeRing` default-constructs without zero-fill; `Clear()` zero-fills (task 2.5).
- [ ] Heap `PanelCreate()` still clears the scope ring; panel tests green (task 2.5).

### Clock (Phase 3)
- [ ] `nostromo::NowMs()` resolves on the desktop build; panel + midi_io tests green (task 3.6).

### Memory map + target (Phase 4)
- [ ] Shared sources contain no `#ifdef TWANG_SHARED_IPC`, no `#ifdef TWANG_UI_SDRAM`, no weak `EngineEventsPending`, no `__ZEPHYR__` branch, and no fixed SDRAM address literal (note criterion 1; task 4.13).
- [ ] Both `CMakeLists.txt` no longer define `TWANG_SHARED_IPC` / `TWANG_UI_SDRAM` (note criterion 2; task 4.12).
- [ ] Both target images build; cm33 + cm85 link (note criterion 3; task 4.12).
- [ ] Desktop `ctest` passes 21 tests (note criterion 3; task 4.13).

### Hardware (Phase 5)
- [ ] Both images flash and USB re-enumerates (note criterion 4; task 5.1).
- [ ] `event_drops` readable at `0x68400000 + offsetof(SharedIpc, event_drops)`; five loss counters readable at `0x68530000` (note criterion 5; task 5.4).
- [ ] note-on→off releases; CC 123 releases; 400-CC flood leaves `note_ring_full` at 0 (note criterion 6; task 5.3).

## 8. Document Staleness Audit

### Documents reviewed

| Document | Invalidated? | Action |
|---|---|---|
| `../design-notes/2026-09-18_decoupling-shared-code-from-target-and-platform_design-note.md` | No — it is the contract and already describes the post-change state. | None. |
| `../arch-designs/spike_arch-design.md` | Yes — §8 Panel contract lists `PanelCreate()` but not `PanelCreateAt(void *)`. | Task 4.14: add the new entry. |
| `../arch-designs/output-stage_arch-design.md` | No — describes the meter/IPC *concept*; the block address and how it is reached are unchanged. | None. |
| `../arch-designs/nostromo-interaction_arch-design.md` | No — no reference to the removed symbols or mechanisms. | None. |
| `../arch-designs/engine-parameter-surface_arch-design.md`, `synth-routing_arch-design.md` | No — no reference to the removed symbols. | None. |
| `docs/references/memory-budget_reference.md` | No — gains new size rows (standing convention). | Task 4.12 appends rows. |
| Prior plans (`2026-09-18_audio-midi-transport-hardening_plan.md`, `2026-09-12_output-stage_plan.md`) | No — point-in-time, superseded by execution; not maintained. | None. |

## 9. Cleanup

No temporary diagnostics are added. The removed `CountEventDrop()`, the weak `EngineEventsPending`, the `#ifdef` guards, and the address literals are deleted outright (not downgraded). No cleanup tasks remain beyond the deletions already listed in the status table.
