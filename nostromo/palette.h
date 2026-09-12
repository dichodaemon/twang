/// @file palette.h
/// @brief The Nostromo palette — the closed four-colour vocabulary.
///
/// One definition of the shipped colours, consumed by the panel, the mockups,
/// and any future Nostromo screen. The packing helper is spike-general
/// (`spike::Rgb565`); the specific colours are Nostromo vocabulary, so they
/// live here rather than in spike. A screen cannot introduce a new colour
/// without editing this file — which is the point (panel-ui-design-state.md §3).

#pragma once

#include "fb.h"  // spike::Color, spike::Rgb565

namespace nostromo {

/// Green phosphor, near the eye's photopic peak; the 6-bit green channel
/// leaves headroom for a fifth intensity if one is ever needed.
constexpr spike::Color kBg = spike::Rgb565(5, 10, 6);        ///< Background.
constexpr spike::Color kFaint = spike::Rgb565(13, 53, 32);   ///< Empty / unassigned.
constexpr spike::Color kDim = spike::Rgb565(27, 98, 56);     ///< Chrome, headers.
constexpr spike::Color kMid = spike::Rgb565(63, 191, 120);   ///< Labels.
constexpr spike::Color kBright = spike::Rgb565(124, 255, 176);  ///< Alerts, focus, active.

}  // namespace nostromo
