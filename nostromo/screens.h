/// @file screens.h
/// @brief The four screens as static-chrome descriptors, plus shared DYN hooks.
///
/// One description, two consumers (panel-ui-design-state.md §9): the panel and
/// the mockup tool both interpret the same byte stream, so their chrome cannot
/// drift. The stream encodes *where* the static chrome sits (§5.1); the
/// variable content — plots, matrix cells, split wells, cursors, the mode
/// buttons — is drawn by C DYN hooks with either live state (panel) or canned
/// state (mockup).
#pragma once

#include <cstdint>

#include "descriptor.h"
#include "fb.h"

namespace nostromo {

/// Palette indices for the descriptor's `c:u8` (see DescriptorPalette()).
enum ColorIdx : int { kC_Bg = 0, kC_Faint, kC_Dim, kC_Mid, kC_Bright };

/// Font indices for the descriptor's `font:u8` (TEXT op).
enum FontIdx : int { kF_Primary = 0, kF_Secondary = 1 };

/// DYN slot indices for the signal-flow screen.
enum SlotIdx : int {
  kSlotOsc = 0,   ///< Oscillator waveform plot.
  kSlotFilter,    ///< Filter response plot (XY pad).
  kSlotEnv,       ///< Envelope plot (draggable handles).
  kSlotOut,       ///< Output plot (scope / cycle / spectrum).
  kSlotMode,      ///< Output-module mode buttons (SCOPE / CYCLE / SPEC).
  kNumSlots,
};

/// The five-entry descriptor palette, resolved to RGB565 (kBg..kBright).
/// Index order is the ColorIdx enum; a descriptor `c:u8` names an entry here.
const spike::Color *DescriptorPalette();

/// The static-chrome descriptor byte stream for each screen (built once at
/// static init into a fixed-size buffer; interpreted until the END op).
const std::uint8_t *SignalScreen();
const std::uint8_t *MatrixScreen();
const std::uint8_t *PatchScreen();
const std::uint8_t *SaveScreen();

/// A ready interpreter context: palette + the two Terminus atlases + no
/// templates + the caller's DYN slot array. `slots`/`n_slots` are the caller's;
/// the palette and fonts are the Nostromo defaults.
spike::DescriptorCtx MakeCtx(spike::DynSlot *slots, int n_slots);

/// Draws the output module's SCOPE / CYCLE / SPEC buttons into DYN rect `r`,
/// highlighting `mode` (0 = scope, 1 = cycle, 2 = spectrum). Shared by the
/// panel (live scope_mode) and the mockup (canned SCOPE).
void DrawModeButtons(spike::FrameBuffer &fb, const spike::Rect &r, int mode);

}  // namespace nostromo
