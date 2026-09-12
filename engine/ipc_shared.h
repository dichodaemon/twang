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
};

// The meter is the first audio -> control signal: the audio core writes it
// (CAS-max, relaxed) and the control core reads-and-clears it. Require a
// lock-free float atomic so the relaxed 32-bit loads/stores stay plain
// ldrex/strex on the ARM targets (output-stage arch-design §5).
static_assert(std::atomic<float>::is_always_lock_free);

#ifdef TWANG_SHARED_IPC

/// Fixed SDRAM address of the shared block.
///
/// SDRAM spans 0x68000000..0x6c000000 (64 MiB). The GLCDC frame buffer (cm33
/// only, double-buffered 1024x600 RGB565 = ~2.4 MB) occupies the first region;
/// the shared block sits at +4 MB, clear of it and any .sdram static data.
inline constexpr std::uintptr_t kSharedIpcAddr = 0x68400000UL;

inline SharedIpc &Shared() {
    return *reinterpret_cast<SharedIpc *>(kSharedIpcAddr);
}

#else  // desktop / single process

inline SharedIpc &Shared() {
    static SharedIpc s{};
    return s;
}

#endif

}  // namespace engine
