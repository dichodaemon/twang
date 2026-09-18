/// @file sdram_map.h
/// @brief Fixed SDRAM addresses for the dual-core RA8D2 target (target-only).
///
/// The two cores (M85 audio / M33 control) are linked separately, so no linker
/// section can give both the same address for one shared object. These fixed
/// addresses are the rendezvous the two cores agree on. They are properties of
/// the target's memory map, not of any shared data structure, so they live here
/// — not in the portable struct headers (`ipc_shared.h`, `scope_tap.h`,
/// `midi_ring.h`, `loss_counters.h`).
///
/// Justified exception, not a pattern -- a fixed SDRAM address is the only
/// rendezvous for separately-linked cores. Included only by target sources and
/// the J-Link diagnostic script, never by desktop code.

#pragma once

#include <cstdint>

// SDRAM spans 0x68000000..0x6c000000 (64 MiB).

/// Shared control→audio IPC block (+4 MB, clear of the GLCDC framebuffers).
inline constexpr std::uintptr_t kSharedIpcAddr = 0x68400000UL;

/// Scope tap == the Panel base: the tap is the Panel's first member, so this
/// is where the audio core reaches the ring and where the cm33 placement-news
/// the Panel.
inline constexpr std::uintptr_t kScopeTapAddr = 0x68500000UL;

/// Transport loss counters. Sits in the clear gap after the Panel's ~160 KiB
/// draw scratch (base kScopeTapAddr, ending ~0x68528000) and before the MIDI
/// note ring. The old 0x68520000 sat inside the Panel's fft_im and got
/// clobbered; 0x68530000 is clear of it on both sides.
inline constexpr std::uintptr_t kLossCountersAddr = 0x68530000UL;

/// MIDI note ring (256 words, 1 KiB). Must NOT land inside the Panel's
/// placement-new'd draw scratch; 0x68580000 sits in the clear gap before the
/// GLCDC framebuffer at 0x68600000.
inline constexpr std::uintptr_t kNoteRingAddr = 0x68580000UL;

/// MIDI CC ring (1024 words, 4 KiB, immediately after the note ring).
inline constexpr std::uintptr_t kCcRingAddr = 0x68584000UL;

/// GLCDC framebuffers (double-buffered 1024x600 RGB565).
inline constexpr std::uintptr_t kFbAddr[2] = {0x68600000UL, 0x68800000UL};
