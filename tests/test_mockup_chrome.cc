// test_mockup_chrome.cc — the signal-flow mockup must keep predicting the panel.
//
// tools/mockup_screens.cc redraws the signal-flow screen with fixed content.
// Its value is entirely that the chrome is identical to what nostromo/panel.cc
// produces: it is the reference the other three screens are judged against,
// and it is what an opcode/descriptor encoding would be validated against.
//
// Nothing enforces that by construction -- the two are separate code. This
// test does: it renders both and asserts the chrome bands match pixel for
// pixel, while allowing the content bands to differ (the mockup draws
// representative curves, the panel draws live engine state).
//
// If this fails, one of the two has drifted. Fix the drift; do not widen the
// bands.

#include <cstdio>
#include <vector>

#include "engine.h"
#include "fb.h"
#include "mockup_screens.h"
#include "palette.h"
#include "panel.h"
#include "params.h"

namespace {

constexpr int kW = 1024, kH = 600;

int g_failures = 0;

void Check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++g_failures;
  }
}

/// Rows that carry chrome only. Everything outside these bands is plot
/// content, mode lines or readouts, which differ by design.
struct Band { int y0, y1; const char *name; };
constexpr Band kChrome[] = {
    {0, 84, "title bar and margin"},
    {84, 110, "module headers"},
    {128, 142, "module separator rule"},
    {374, 393, "plot bottom edge + separator"},
    {406, 415, "readout gap"},
    {428, 432, "bottom corner brackets"},
    {432, 512, "keyboard band"},
    {512, 600, "nav bar and encoder legend"},
};

int DiffRows(const std::vector<std::uint16_t> &a,
             const std::vector<std::uint16_t> &b, int y0, int y1) {
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = 0; x < kW; ++x)
      if (a[static_cast<std::size_t>(y) * kW + x] !=
          b[static_cast<std::size_t>(y) * kW + x])
        ++n;
  return n;
}

}  // namespace

int main() {
  std::vector<std::uint16_t> mock(static_cast<std::size_t>(kW) * kH, nostromo::kBg);
  std::vector<std::uint16_t> live0(static_cast<std::size_t>(kW) * kH, nostromo::kBg);
  std::vector<std::uint16_t> live1(static_cast<std::size_t>(kW) * kH, nostromo::kBg);

  {
    spike::FrameBuffer fb{mock.data(), kW, kH, kW, spike::Rect{0, 0, kW, kH}};
    mockup::DrawSignal(fb);
  }

  // The panel must be in its default state before the first render: it polls
  // the engine each frame, and an uninitialised engine reads as all zeros.
  engine::EngineInit();
  nostromo::Panel *p = nostromo::PanelCreate();
  spike::FrameBuffer f0{live0.data(), kW, kH, kW, spike::Rect{0, 0, kW, kH}};
  spike::FrameBuffer f1{live1.data(), kW, kH, kW, spike::Rect{0, 0, kW, kH}};
  nostromo::PanelDraw(p, f0, 0);
  nostromo::PanelDraw(p, f1, 1);

  for (const Band &b : kChrome) {
    const int d = DiffRows(mock, live0, b.y0, b.y1);
    if (d != 0)
      std::printf("  %-28s rows %3d-%3d: %d differing pixels\n", b.name, b.y0,
                  b.y1, d);
    Check(d == 0, b.name);
  }

  // Sanity: the content bands SHOULD differ. If they do not, the mockup has
  // probably been replaced by a copy of the live render and stopped being an
  // independent check.
  const int content = DiffRows(mock, live0, 142, 374);
  Check(content > 0, "plot content differs (mockup is not a copy of the panel)");

  // Both buffers converge, so the chrome comparison is not buffer-specific.
  Check(DiffRows(live0, live1, 0, kH) == 0, "panel buffers agree");

  if (g_failures == 0) std::printf("all mockup chrome checks passed\n");
  return g_failures ? 1 : 0;
}
