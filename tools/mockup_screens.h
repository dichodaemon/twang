#pragma once

/// @file
/// The four panel screens as fixed-content renderers.
///
/// Defined in tools/mockup_screens.cc. Compile that translation unit with
/// TWANG_MOCKUP_NO_MAIN to link it into something other than the tool.

#include "fb.h"

namespace mockup {

/// @brief Draw the signal-flow screen. Chrome mirrors nostromo/panel.cc exactly;
/// plot content is representative rather than live.
void DrawSignal(spike::FrameBuffer &fb);

/// @brief Draw the modulation matrix screen.
void DrawMatrix(spike::FrameBuffer &fb);

/// @brief Draw the patch browser screen.
void DrawPatch(spike::FrameBuffer &fb);

/// @brief Draw the save dialogue screen.
void DrawSave(spike::FrameBuffer &fb);

}  // namespace mockup
