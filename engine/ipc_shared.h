/// @file ipc_shared.h
/// @brief Cross-core shared IPC: the event ring and param block the control
/// core (M33) produces and the audio core (M85) consumes.
///
/// On the target the two cores are linked separately, so no linker section can
/// give both the same address for a shared object. Instead the single block
/// lives at a fixed SDRAM address both cores agree on. On the desktop (single
/// process) the same accessor returns a plain static object, so the engine
/// compiles unchanged for the simulator.
///
/// The structures are plain old data over `std::atomic`; on the ARM targets
/// the atomics are trivially-constructible (native ldrex/strex, no locks), so
/// reinterpret-casting the fixed address and then `Reset()`-ing is sufficient.

#pragma once

#include <cstdint>

#include "ipc.h"

namespace engine {

/// Shared control -> audio IPC state (single instance).
struct SharedIpc {
    EventRing events;   ///< control produces, audio consumes (SPSC)
    ParamBlock params;  ///< control writes, audio snapshots at block boundary
    std::atomic<float> meter;  ///< audio writes, control reads-and-clears (reverse direction)
    /// Control-side diagnostic: how many note events were dropped because the
    /// event ring was full. Bumped by EngineControl when events.Push() returns
    /// false; a plain object on the desktop, so unit-testable.
    std::atomic<std::uint32_t> event_drops{0};
};

// The meter is the first audio -> control signal: the audio core writes it
// (CAS-max, relaxed) and the control core reads-and-clears it. Require a
// lock-free float atomic so the relaxed 32-bit loads/stores stay plain
// ldrex/strex on the ARM targets (output-stage arch-design §5).
static_assert(std::atomic<float>::is_always_lock_free);

}  // namespace engine
