/// @file scope_tap.h
/// @brief Cross-core audio→UI scope tap: the ring plus a dirty flag.
///
/// The audio core (M85 on the target) writes rendered samples into the ring and
/// raises the dirty flag; the UI core (M33) reads the ring to draw the scope
/// and clears the flag. The host's audio thread drives the same tap through
/// PanelAudioTap.
///
/// On the target the tap is the Panel's first member, so it sits at the
/// Panel's SDRAM base — a rendezvous both cores agree on without knowing the
/// Panel's internal layout (the fixed address lives in controller/sdram_map.h).
/// The audio core accesses it by reinterpret-casting that address; the UI core
/// uses the Panel's embedded copy. The two are the same object: the Panel is
/// placement-new'd at that address.
#pragma once

#include <atomic>
#include <cstdint>

#include "scope_ring.h"

struct ScopeTap {
  ScopeRing ring;                       ///< Rendered samples (audio writes, UI reads).
  std::atomic<bool> dirty{false};       ///< New samples since the last draw.

  /// @brief Reset the tap (producer, once at boot, before the first Write).
  ///
  /// The fixed-address SDRAM backing is uninitialized until this runs.
  void Reset() {
    ring.Reset();
    dirty.store(false, std::memory_order_relaxed);
  }
};
