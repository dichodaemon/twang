// spike/font.cc — bitmap glyph blitting.

#include "font.h"

#include <algorithm>
#include <cstddef>

#include "font_data.h"

namespace spike {

namespace {

// Glyph index for a single byte, matching the mockup's gidx(): ASCII 0x20..0x7E
// maps to codepoint - 0x20, 0xB7 (middle dot) to the final index, and anything
// else to glyph 0 (space).
int GlyphIndex(int c) {
  if (c >= 0x20 && c <= 0x7E) return c - 0x20;
  if (c == 0xB7) return font_data::kGlyphCount - 1;
  return 0;
}

}  // namespace

const Font kPrimaryFont = {font_data::kTerU20nW, font_data::kTerU20nH,
                           font_data::kTerU20nBase, font_data::kTerU20nBits};
const Font kSecondaryFont = {font_data::kTerU14nW, font_data::kTerU14nH,
                             font_data::kTerU14nBase, font_data::kTerU14nBits};

void DrawGlyphRun(FrameBuffer &fb, int x, int y, const char *s, int n,
                  const Font &f, Color c, int tracking) {
  if (n <= 0) return;

  // Effective clip: fb.clip intersected with the framebuffer bounds.
  const int cx0 = std::max(fb.clip.x, 0);
  const int cy0 = std::max(fb.clip.y, 0);
  const int cx1 = std::min(fb.clip.x + fb.clip.w, fb.w);
  const int cy1 = std::min(fb.clip.y + fb.clip.h, fb.h);

  const int bpr = (f.w + 7) / 8;  // bytes per row
  const int advance = f.w + tracking;
  const int nb = bpr * 8;  // bits per packed row word

  for (int i = 0; i < n; ++i) {
    const int gx = x + i * advance;
    if (gx >= cx1 || gx + f.w <= cx0) continue;  // glyph off-clip horizontally

    const int gi = GlyphIndex(static_cast<unsigned char>(s[i]));
    const std::uint8_t *glyph =
        f.bitmap + static_cast<std::size_t>(gi) * f.h * bpr;

    for (int r = 0; r < f.h; ++r) {
      const int py = y + r;
      if (py < cy0 || py >= cy1) continue;

      std::uint32_t bits = 0;
      for (int k = 0; k < bpr; ++k) bits = (bits << 8) | glyph[r * bpr + k];
      if (!bits) continue;

      for (int b = 0; b < f.w; ++b) {
        const int px = gx + b;
        if (px < cx0) continue;
        if (px >= cx1) break;  // px increases with b; no later column fits
        if ((bits >> (nb - 1 - b)) & 1u)
          fb.px[static_cast<std::size_t>(py) * fb.stride + px] = c;
      }
    }
  }
}

}  // namespace spike
