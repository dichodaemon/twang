/// @file loss_counters.h
/// @brief Cross-core transport loss counters at a fixed SDRAM address.
///
/// The transport has silent-drop paths — note/CC ring full, channel filter,
/// MT filter, UAC2 FIFO overflow, IPC event-ring full — any of which can drop
/// a note-off and stick a note. These counters make each drop observable over
/// J-Link without perturbing the high-rate paths the way logging would. Both
/// cores increment their own counters through this shared block; it lives at a
/// fixed SDRAM address (like the scope tap and MIDI ring) so both separately-
/// linked cores agree on it without a linker section.

#pragma once

#include <atomic>
#include <cstdint>

/// Counters for the transport's silent-drop paths. Plain atomic scalars — each
/// field is written by exactly one producer core, so there is no tearing and no
/// lock.
struct LossCounters {
    std::atomic<std::uint32_t> note_ring_full{0};       ///< cm85: note ring Push() returned false
    std::atomic<std::uint32_t> cc_ring_full{0};         ///< cm85: CC ring Push() returned false
    std::atomic<std::uint32_t> channel_reject{0};       ///< cm33: MIDI 1.0 channel != 0 dropped
    std::atomic<std::uint32_t> mt_reject{0};            ///< cm33: UMP MT != 2 dropped
    std::atomic<std::uint32_t> fifo_overflow_frames{0}; ///< cm85: UAC2 FIFO truncation frames
    std::atomic<std::uint32_t> ipc_event_drops{0};      ///< cm33: ipc events.Push() returned false

    /// Reset all counters (idempotent; call once at boot before MIDI flows —
    /// the fixed-address SDRAM backing is uninitialized until this runs).
    void Reset() {
        note_ring_full.store(0, std::memory_order_relaxed);
        cc_ring_full.store(0, std::memory_order_relaxed);
        channel_reject.store(0, std::memory_order_relaxed);
        mt_reject.store(0, std::memory_order_relaxed);
        fifo_overflow_frames.store(0, std::memory_order_relaxed);
        ipc_event_drops.store(0, std::memory_order_relaxed);
    }
};

/// Fixed SDRAM address of the loss counters.
///
/// 0x68530000 sits in the clear gap after the Panel's ~160 KiB draw scratch
/// (base kScopeTapAddr = 0x68500000, ending ~0x68528000) and before the MIDI
/// note ring at 0x68580000 (and the GLCDC framebuffer at 0x68600000). The old
/// 0x68520000 was inside the Panel's fft_im and got clobbered; 0x68530000 is
/// comfortably clear of it on both sides.
inline constexpr std::uintptr_t kLossCountersAddr = 0x68530000UL;
