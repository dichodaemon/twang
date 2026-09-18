/// @file midi_ring.h
/// @brief Cross-core MIDI input ring: cm85 (USB MIDI rx) -> cm33 (control).
///
/// The audio core (M85) receives UMP packets on the composite MIDI 2.0
/// endpoint and pushes their first word into this ring; the control core (M33)
/// drains it, downconverts to MIDI 1.0, and feeds the interaction layer.
/// Single-word UMP (MIDI 1.0 channel voice) covers the X-Touch Compact;
/// multi-word UMPs (SysEx / MIDI 2.0) are a later extension.
///
/// Lives at a fixed SDRAM address (like the scope tap) so both separately-
/// linked cores agree on it without a linker section.

#pragma once

#include <atomic>
#include <cstdint>

/// Lock-free single-producer / single-consumer ring of raw UMP words.
struct MidiRing {
    static constexpr std::uint32_t kCapacity = 256;  // power of two

    std::atomic<std::uint32_t> write{0};  ///< monotonic producer index
    std::atomic<std::uint32_t> read{0};   ///< monotonic consumer index
    std::uint32_t buf[kCapacity];

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

/// Fixed SDRAM address of the MIDI ring.
///
/// Must NOT land inside the Panel's placement-new'd draw scratch (base
/// kScopeTapAddr = 0x68500000, ~160 KiB, ending ~0x68528000): the cm33 writes
/// fft_re/fft_im on every spectrum draw, and the old 0x68520000 sat inside
/// fft_im — clobbering the ring's indices and buf, which dropped/corrupted
/// note-offs (intermittent stuck notes). 0x68580000 sits in the clear gap
/// before the GLCDC framebuffer at 0x68600000.
inline constexpr std::uintptr_t kMidiRingAddr = 0x68580000UL;
