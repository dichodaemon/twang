// spike/fb.cc — implementation of the draw primitives.
//
// All primitives clip to the active clip rect intersected with the framebuffer
// bounds, write RGB565 without ever reading the destination, and allocate
// nothing. The only scattered-access primitive is DrawPolyline (Bresenham);
// fills and lines advance contiguously along a scanline so they can use
// 32-bit stores (two pixels per store) on aligned rows.

#include "fb.h"

#include <algorithm>
#include <cstdlib>

namespace spike {
namespace {

int Min(int a, int b) { return a < b ? a : b; }
int Max(int a, int b) { return a > b ? a : b; }

// Intersect (x, y, w, h) with the active clip and the framebuffer bounds.
// Returns false when the result is empty.
bool ClipToFrame(const FrameBuffer &fb, int x, int y, int w, int h, int *ox,
                 int *oy, int *ow, int *oh) {
  const int cx0 = Max(fb.clip.x, 0);
  const int cy0 = Max(fb.clip.y, 0);
  const int cx1 = Min(fb.clip.x + fb.clip.w, fb.w);
  const int cy1 = Min(fb.clip.y + fb.clip.h, fb.h);
  const int x0 = Max(x, cx0);
  const int y0 = Max(y, cy0);
  const int x1 = Min(x + w, cx1);
  const int y1 = Min(y + h, cy1);
  if (x1 <= x0 || y1 <= y0) return false;
  *ox = x0;
  *oy = y0;
  *ow = x1 - x0;
  *oh = y1 - y0;
  return true;
}

// Writes `w` pixels of color `c` starting at `dst`, two RGB565 pixels per
// 32-bit store. If the row start is only 2-byte aligned (odd pixel offset),
// the first pixel is written as a 16-bit store to reach 4-byte alignment.
void FillRow(std::uint16_t *dst, int w, std::uint16_t c) {
  const std::uint32_t pair = (static_cast<std::uint32_t>(c) << 16) | c;
  if ((reinterpret_cast<std::uintptr_t>(dst) & 3u) == 2u) {
    *dst++ = c;
    --w;
  }
  std::uint32_t *d = reinterpret_cast<std::uint32_t *>(dst);
  const int n = w >> 1;
  for (int i = 0; i < n; ++i) *d++ = pair;
  if (w & 1) *reinterpret_cast<std::uint16_t *>(d) = c;
}

// Single clipped pixel write (Bresenham walk only).
void PutPixel(FrameBuffer &fb, int x, int y, Color c) {
  if (x < fb.clip.x || x >= fb.clip.x + fb.clip.w) return;
  if (y < fb.clip.y || y >= fb.clip.y + fb.clip.h) return;
  if (x < 0 || y < 0 || x >= fb.w || y >= fb.h) return;
  fb.px[static_cast<std::size_t>(y) * fb.stride + x] = c;
}

}  // namespace

void FillRect(FrameBuffer &fb, int x, int y, int w, int h, Color c) {
  int ox, oy, ow, oh;
  if (!ClipToFrame(fb, x, y, w, h, &ox, &oy, &ow, &oh)) return;
  for (int r = 0; r < oh; ++r)
    FillRow(fb.px + static_cast<std::size_t>(oy + r) * fb.stride + ox, ow, c);
}

void DrawHLine(FrameBuffer &fb, int x, int y, int w, Color c) {
  int ox, oy, ow, oh;
  if (!ClipToFrame(fb, x, y, w, 1, &ox, &oy, &ow, &oh)) return;
  FillRow(fb.px + static_cast<std::size_t>(oy) * fb.stride + ox, ow, c);
}

void DrawVLine(FrameBuffer &fb, int x, int y, int h, Color c) {
  int ox, oy, ow, oh;
  if (!ClipToFrame(fb, x, y, 1, h, &ox, &oy, &ow, &oh)) return;
  for (int r = 0; r < oh; ++r)
    fb.px[static_cast<std::size_t>(oy + r) * fb.stride + ox] = c;
}

void SetClip(FrameBuffer &fb, int x, int y, int w, int h) {
  fb.clip = Rect{x, y, w, h};
}

void DrawPolyline(FrameBuffer &fb, const Point *pts, int n, Color c) {
  for (int i = 0; i + 1 < n; ++i) {
    int x0 = pts[i].x;
    int y0 = pts[i].y;
    const int x1 = pts[i + 1].x;
    const int y1 = pts[i + 1].y;
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      PutPixel(fb, x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
}

}  // namespace spike
