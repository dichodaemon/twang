/// @file midi_ring.h
/// @brief Cross-core MIDI input rings: cm85 (USB MIDI rx) -> cm33 (control).
///
/// The audio core (M85) receives UMP packets on the composite MIDI 2.0
/// endpoint, classifies each first word with IsNoteWord, and pushes notes into
/// the note ring and everything else into the CC ring; the control core (M33)
/// drains the note ring first, then the CC ring. Single-word UMP (MIDI 1.0
/// channel voice) covers the X-Touch Compact; multi-word UMPs (SysEx / MIDI
/// 2.0) are a later extension and land in the CC ring.
///
/// Both rings live at fixed SDRAM addresses (like the scope tap) so both
/// separately-linked cores agree on them without a linker section.

#pragma once

#include <atomic>
#include <cstdint>

/// Lock-free single-producer / single-consumer ring of raw UMP words.
/// `Capacity` must be a power of two.
template <std::uint32_t Capacity>
struct MidiRing {
    static constexpr std::uint32_t kCapacity = Capacity;  // power of two

    std::atomic<std::uint32_t> write{0};  ///< monotonic producer index
    std::atomic<std::uint32_t> read{0};   ///< monotonic consumer index
    std::uint32_t buf[Capacity];

    /// Reset both indices (producer/consumer, once at boot). The fixed-address
    /// SDRAM backing is uninitialized until this runs.
    void Reset() {
        write.store(0, std::memory_order_relaxed);
        read.store(0, std::memory_order_relaxed);
    }

    /// @brief Append one UMP word.
    /// @return false if the ring is full (word dropped).
    bool Push(std::uint32_t word) {
        const std::uint32_t w = write.load(std::memory_order_relaxed);
        const std::uint32_t r = read.load(std::memory_order_acquire);
        if (w - r >= kCapacity) {
            return false;
        }
        buf[w & (kCapacity - 1)] = word;
        write.store(w + 1, std::memory_order_release);
        return true;
    }

    /// @brief Remove the oldest UMP word.
    /// @return false if the ring is empty.
    bool Pop(std::uint32_t *word) {
        const std::uint32_t r = read.load(std::memory_order_relaxed);
        const std::uint32_t w = write.load(std::memory_order_acquire);
        if (r == w) {
            return false;
        }
        *word = buf[r & (kCapacity - 1)];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
};

/// Note ring (256 words): note-on/note-off + CC 123 — never dropped.
using NoteRing = MidiRing<256>;
/// CC ring (1024 words): everything else (faders, SysEx, MIDI 2.0) — droppable.
using CcRing = MidiRing<1024>;

/// @return true if `word` is a MIDI 1.0 channel-voice note (0x80/0x90) or
///         CC 123 (All Notes Off) — the words that must never be dropped.
///         Everything else (other CCs, SysEx, MIDI 2.0) returns false.
///
/// UMP MIDI 1.0 channel-voice word layout (see Zephyr midi.h): MT in bits
/// 31-28, group in 27-24, status in 23-16, data1 in 15-8, data2 in 7-0.
inline bool IsNoteWord(std::uint32_t word) {
    if ((word >> 28) != 0x2) {  // UMP_MT_MIDI1_CHANNEL_VOICE
        return false;  // SysEx / MIDI 2.0 (multi-word) — not a note word
    }
    const std::uint32_t status = (word >> 16) & 0xFF;
    if (status == 0x80 || status == 0x90) {
        return true;  // note-off / note-on
    }
    if (status == 0xB0 && ((word >> 8) & 0x7F) == 123) {
        return true;  // CC 123 (All Notes Off)
    }
    return false;
}
