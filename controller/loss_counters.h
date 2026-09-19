/// @file loss_counters.h
/// @brief Cross-core transport loss counters.
///
/// The transport has silent-drop paths — note/CC ring full, channel filter,
/// MT filter, UAC2 FIFO overflow — any of which can drop a note-off and stick
/// a note. These counters make each drop observable over J-Link without
/// perturbing the high-rate paths the way logging would. Both cores increment
/// their own counters through this shared block, which lives at a fixed SDRAM
/// address (see controller/sdram_map.h) so both separately-linked cores agree
/// on it without a linker section.
///
/// The IPC event-ring-full drop is counted in SharedIpc::event_drops (the
/// transport the engine observes), not here.

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
    std::atomic<std::uint32_t> mt_reject{0};            ///< cm33: UMP MT != MIDI 1.0 channel voice dropped
    std::atomic<std::uint32_t> group_reject{0};         ///< cm33: UMP group != 0 dropped
    std::atomic<std::uint32_t> fifo_overflow_frames{0}; ///< cm85: UAC2 FIFO truncation frames

    /// Producer→consumer boot handshake: cm85 stores kBootMagic after it has
    /// reset the MIDI rings; cm33's control thread waits on it before the
    /// first drain. NOT cleared by Reset() — it is the ready signal, written
    /// once by cm85 after Reset().
    std::atomic<std::uint32_t> boot_magic{0};

    /// Last UMP word rejected by the MT filter (diagnostic: captures the
    /// non-MIDI-1.0-channel-voice word that bumped mt_reject).
    std::atomic<std::uint32_t> last_reject_word{0};

    /// Diagnostic note traffic (temporary): MT=2 note-ons/offs processed, and
    /// the note number of the last note-off.
    std::atomic<std::uint32_t> note_on_count{0};
    std::atomic<std::uint32_t> note_off_count{0};
    std::atomic<std::uint32_t> last_note_off_note{0};

    /// Reset the counters (cm85, the sole owner, calls this once before MIDI
    /// flows — the fixed-address SDRAM backing is uninitialized until then).
    void Reset() {
        note_ring_full.store(0, std::memory_order_relaxed);
        cc_ring_full.store(0, std::memory_order_relaxed);
        channel_reject.store(0, std::memory_order_relaxed);
        mt_reject.store(0, std::memory_order_relaxed);
        group_reject.store(0, std::memory_order_relaxed);
        fifo_overflow_frames.store(0, std::memory_order_relaxed);
        last_reject_word.store(0, std::memory_order_relaxed);
        note_on_count.store(0, std::memory_order_relaxed);
        note_off_count.store(0, std::memory_order_relaxed);
        last_note_off_note.store(0, std::memory_order_relaxed);
    }
};

/// Boot magic (ASCII "TWAN"): written by cm85 after the MIDI rings are reset,
/// read by cm33 before its first drain.
inline constexpr std::uint32_t kBootMagic = 0x5457414Eu;
