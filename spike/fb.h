/// @file fb.h
/// @brief Framebuffer types and the five draw primitives.
///
/// The renderer's lowest layer: an RGB565 framebuffer, a clip rect, and the
/// primitive calls every higher layer (font, panel, chrome) composes.
/// Primitives are 1-bpp/no-blend — they never read the destination pixel —
/// and clip every write to `fb.clip` intersected with the framebuffer bounds.
#pragma once

#include <cstdint>

namespace spike {

/// @brief Axis-aligned rectangle.
struct Rect {
  int x;  ///< Left edge.
  int y;  ///< Top edge.
  int w;  ///< Width in pixels.
  int h;  ///< Height in pixels.
};

/// @brief A point in framebuffer space.
struct Point {
  int x;  ///< Horizontal coordinate.
  int y;  ///< Vertical coordinate.
};

/// @brief An RGB565 framebuffer plus the active clip.
struct FrameBuffer {
  std::uint16_t *px;  ///< Pixel storage, row-major.
  int w;              ///< Width in pixels.
  int h;              ///< Height in pixels.
  int stride;         ///< Pixels per row (>= w).
  Rect clip;          ///< Active clip (defaults to the full frame).
};

/// @brief An RGB565 pixel value.
using Color = std::uint16_t;

/// @brief Pack an 8-bit-per-channel RGB triple into RGB565.
constexpr Color Rgb565(int r, int g, int b) {
  return static_cast<Color>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/// @brief Fills the rectangle [x, x+w) x [y, y+h) with a color.
///
/// Writes two RGB565 pixels per 32-bit store; the destination is never read.
///
/// @param fb Framebuffer to draw into.
/// @param x Left edge.
/// @param y Top edge.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @param c Fill color.
void FillRect(FrameBuffer &fb, int x, int y, int w, int h, Color c);

/// @brief Draws a horizontal run of pixels.
///
/// @param fb Framebuffer to draw into.
/// @param x Left edge.
/// @param y Row coordinate.
/// @param w Run length in pixels.
/// @param c Line color.
void DrawHLine(FrameBuffer &fb, int x, int y, int w, Color c);

/// @brief Draws a vertical run of pixels.
///
/// @param fb Framebuffer to draw into.
/// @param x Column coordinate.
/// @param y Top edge.
/// @param h Run length in pixels.
/// @param c Line color.
void DrawVLine(FrameBuffer &fb, int x, int y, int h, Color c);

/// @brief Draws a polyline through the given points (Bresenham).
///
/// The only scattered-access primitive; used exclusively by the plots.
///
/// @param fb Framebuffer to draw into.
/// @param pts Array of at least `n` vertices.
/// @param n Number of vertices (n-1 segments).
/// @param c Line color.
void DrawPolyline(FrameBuffer &fb, const Point *pts, int n, Color c);

/// @brief Sets the active clip; subsequent writes are clipped to it.
///
/// @param fb Framebuffer whose clip is set.
/// @param x Left edge.
/// @param y Top edge.
/// @param w Width in pixels.
/// @param h Height in pixels.
void SetClip(FrameBuffer &fb, int x, int y, int w, int h);

}  // namespace spike
