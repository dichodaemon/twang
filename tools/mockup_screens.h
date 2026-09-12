#pragma once

/// @file
/// The four panel screens as fixed-content renderers.
///
/// Defined in tools/mockup_screens.cc. Compile that translation unit with
/// TWANG_MOCKUP_NO_MAIN to link it into something other than the tool.

#include "fb.h"

namespace mockup {

/// Background colour the screens are drawn over. Callers must pre-fill the
/// frame buffer with this: the screens draw chrome and content, not the
/// backdrop (spike/panel.cc's DrawChrome fills it separately).
constexpr spike::Color kBg = static_cast<spike::Color>(
    ((5 >> 3) << 11) | ((10 >> 2) << 5) | (6 >> 3));

/// @brief Draw the signal-flow screen. Chrome mirrors spike/panel.cc exactly;
/// plot content is representative rather than live.
void DrawSignal(spike::FrameBuffer &fb);

/// @brief Draw the modulation matrix screen.
void DrawMatrix(spike::FrameBuffer &fb);

/// @brief Draw the patch browser screen.
void DrawPatch(spike::FrameBuffer &fb);

/// @brief Draw the save dialogue screen.
void DrawSave(spike::FrameBuffer &fb);

}  // namespace mockup
