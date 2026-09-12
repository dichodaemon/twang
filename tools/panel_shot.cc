// panel_shot.cc — render the panel offscreen and write a PNG.
//
// No display, no SDL, no zlib: draws into a plain RGB565 buffer exactly as the
// GLCDC would scan it out, then writes a PNG using stored (uncompressed)
// deflate blocks.
//
// Builds as the `panel_shot` CMake target (links `spike`), or standalone:
//   g++ -std=c++17 -O1 -Ispike -Iengine -Icontroller \
//       tools/panel_shot.cc spike/*.cc engine/*.cc controller/*.cc \
//       -o panel_shot -lm
//
// Usage:
//   ./panel_shot out.png [scale]
//
// Useful as a golden-image fixture alongside the golden-hash check in
// test_panel.cc: a hash says something changed, an image says what.

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine.h"
#include "panel.h"
#include "png.h"

namespace {

constexpr int kW = 1024;
constexpr int kH = 600;

}  // namespace

int main(int argc, char **argv) {
  const char *path = (argc > 1) ? argv[1] : "panel.png";
  const int scale = (argc > 2) ? std::atoi(argv[2]) : 1;

  engine::EngineInit();
  nostromo::Panel *p = nostromo::PanelCreate();

  std::vector<std::uint16_t> buf0(kW * kH), buf1(kW * kH);
  spike::FrameBuffer fb0{buf0.data(), kW, kH, kW, spike::Rect{0, 0, kW, kH}};
  spike::FrameBuffer fb1{buf1.data(), kW, kH, kW, spike::Rect{0, 0, kW, kH}};

  // Two draws fill both buffers with chrome; extra pairs settle the plots.
  nostromo::PanelDraw(p, fb0, 0);
  nostromo::PanelDraw(p, fb1, 1);
  nostromo::PanelNoteOn(p, 440.0f, 127);
  for (int i = 0; i < 3; ++i) {
    nostromo::PanelDraw(p, fb0, 0);
    nostromo::PanelDraw(p, fb1, 1);
  }

  const int ow = kW * scale, oh = kH * scale;
  if (!spike::WriteFramePng(fb0, path, scale)) {
    std::fprintf(stderr, "panel_shot: cannot write %s\n", path);
    return 1;
  }
  std::printf("panel_shot: wrote %s (%dx%d)\n", path, ow, oh);
  return 0;
}
