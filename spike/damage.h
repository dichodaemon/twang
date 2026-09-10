/// @file damage.h
/// @brief Fixed-capacity damage tracking with the two-frame repaint rule.
///
/// The panel records dirty regions each frame through Add(). Overlapping
/// regions are merged so the list stays compact; if more than kMaxRects
/// distinct regions are recorded, the damage collapses to a single whole-frame
/// rect. Repaint() returns damage[n] ∪ damage[n−1] — the two-frame rule, which
/// keeps a region alive for one extra frame so double buffering never shows a
/// stale back buffer.
#pragma once

#include "spike/fb.h"

namespace spike {

/// @brief Accumulates per-frame damage and computes repaint rects.
class Damage {
  public:
    /// @brief Maximum number of distinct damage regions per frame.
    static constexpr int kMaxRects = 16;

    /// @brief Constructs a tracker for a frame of the given size.
    ///
    /// @param w Frame width in pixels.
    /// @param h Frame height in pixels.
    Damage(int w, int h);

    /// @brief Records a damaged region, clipping it to the frame.
    ///
    /// Overlapping regions are merged into their union; if more than
    /// kMaxRects distinct regions accumulate, the damage collapses to a
    /// single whole-frame rect and further Add() calls become no-ops.
    ///
    /// @param r The damaged rectangle.
    void Add(const Rect &r);

    /// @brief Records a damaged region by coordinates.
    ///
    /// @param x Left edge.
    /// @param y Top edge.
    /// @param w Width in pixels.
    /// @param h Height in pixels.
    void Add(int x, int y, int w, int h);

    /// @brief Computes the repaint union and rolls the frames.
    ///
    /// Returns damage[n] ∪ damage[n−1], then makes the current frame's damage
    /// the previous frame's and clears the current frame. The rects returned
    /// by Rects() are valid until the next Add() or Repaint().
    ///
    /// @return The number of repaint rects (0 when nothing changed).
    int Repaint();

    /// @brief The repaint rects produced by the last Repaint() call.
    const Rect *Rects() const { return repaint_; }

    /// @brief The rect count produced by the last Repaint() call.
    int Count() const { return repaint_count_; }

  private:
    int w_;  ///< Frame width.
    int h_;  ///< Frame height.

    int count_ = 0;          ///< Current-frame damage count.
    Rect rects_[kMaxRects];  ///< Current-frame damage (merged).
    bool collapsed_ = false; ///< True once collapsed to the whole frame.

    int prev_count_ = 0;     ///< Previous-frame damage count.
    Rect prev_[kMaxRects];   ///< Previous-frame damage.

    Rect repaint_[2 * kMaxRects];  ///< Repaint output (cur ∪ prev).
    int repaint_count_ = 0;        ///< Repaint output count.
};

}  // namespace spike
