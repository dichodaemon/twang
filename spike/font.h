/// @file font.h
/// @brief Monospace 1-bpp bitmap font and text rendering.
///
/// A Font is a fixed-cell bitmap atlas: a cell (w x h), a baseline, and rows
/// packed 1-bit-per-pixel, MSB = leftmost pixel. The glyph layout is fixed by
/// tools/bdf_to_c.py — index = codepoint - 0x20 for ASCII 0x20..0x7E, with
/// U+00B7 (middle dot) at the final index and anything else mapped to glyph 0.
/// DrawGlyphRun is 1-bpp/no-blend: it writes only lit pixels, never reads the
/// destination.
#pragma once

#include <cstdint>

#include "fb.h"

namespace spike {

/// @brief A monospace 1-bpp bitmap atlas.
struct Font {
  int w;                      ///< Cell width in pixels.
  int h;                      ///< Cell height in pixels.
  int base;                   ///< Baseline offset from the top of the cell.
  const std::uint8_t *bitmap;  ///< 1-bpp rows, (w+7)/8 bytes per row.
};

/// @brief The primary 10x20 Terminus atlas.
extern const Font kPrimaryFont;

/// @brief The secondary 8x14 Terminus atlas.
extern const Font kSecondaryFont;

/// @brief Draws a run of glyphs left-to-right, advancing by w + tracking.
///
/// Each character is taken as a single byte; bytes 0x20..0x7E map to their
/// ASCII glyph, 0xB7 maps to the middle dot, and any other byte maps to
/// glyph 0 (space).
///
/// @param fb Framebuffer to draw into.
/// @param x Left edge of the first glyph cell.
/// @param y Top edge of the glyph cell row.
/// @param s Text bytes to render.
/// @param n Number of characters.
/// @param f Font to render.
/// @param c Text color.
/// @param tracking Extra pixels between glyph cells.
void DrawGlyphRun(FrameBuffer &fb, int x, int y, const char *s, int n,
                  const Font &f, Color c, int tracking);

}  // namespace spike
