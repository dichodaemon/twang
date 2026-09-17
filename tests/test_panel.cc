// test_panel.cc — golden image, double buffering, and active-plot invalidation
// gating for the migrated edit screen (pane + columns + one 900x404 plot band).

#include <cstdint>
#include <cstdio>

#include "engine.h"
#include "engine_control.h"
#include "fb.h"
#include "interaction.h"
#include "panel.h"
#include "surface.h"

using namespace spike;
using namespace nostromo;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static constexpr int kW = 1024;
static constexpr int kH = 600;

static std::uint32_t Hash(const std::uint16_t *px, int n) {
    std::uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}

// Golden-image hash of the power-on page (kFilt + scope_mode=kScope: filter
// half 0 + embedded scope half 1) full render.
static constexpr std::uint32_t kExpectedHash = 0x116AFAA4;

int main() {
    engine::SharedIpc ipc;
    engine::EngineControl control;
    control.Init(ipc);
    Panel *p = PanelCreate();
    Interaction it;
    it.Init(p, Surface(), &control);  // power-on page: kFilt + kScope → output

    static std::uint16_t buf0[kW * kH];
    static std::uint16_t buf1[kW * kH];
    FrameBuffer fb0 = {buf0, kW, kH, kW, Rect{0, 0, kW, kH}};
    FrameBuffer fb1 = {buf1, kW, kH, kW, Rect{0, 0, kW, kH}};

    PanelDraw(p, fb0, 0);
    PanelDraw(p, fb1, 1);

    const std::uint32_t h0 = Hash(buf0, kW * kH);
    const std::uint32_t h1 = Hash(buf1, kW * kH);
    std::printf("buffer0 hash: 0x%08X  buffer1 hash: 0x%08X\n", h0, h1);
    Check(h0 != 0, "golden image: something rendered");
    Check(h0 == h1, "double buffering: both buffers render identically");
    Check(h0 == kExpectedHash, "golden image matches the reference hash");

    // Invalidation gating: MarkDirty redraws the ACTIVE plot into both buffers.
    const int out_before = PanelPlotDraws(p, 3);
    MarkDirty(p, kSlotOut);
    PanelDraw(p, fb0, 0);
    PanelDraw(p, fb1, 1);
    Check(PanelPlotDraws(p, 3) == out_before + 2,
          "MarkDirty redraws the active plot into both buffers");

    // A non-active plot is not drawn even when marked dirty (the four slots
    // share the band; at power-on kFilt+kScope the osc plot is not active).
    const int osc_before = PanelPlotDraws(p, 0);  // osc plot (non-active)
    MarkDirty(p, kSlotOsc);
    PanelDraw(p, fb0, 0);
    PanelDraw(p, fb1, 1);
    Check(PanelPlotDraws(p, 0) == osc_before,
          "a non-active plot is not drawn");

    // Steady state: a no-change frame redraws nothing.
    const int out_steady = PanelPlotDraws(p, 3);
    PanelDraw(p, fb0, 0);
    PanelDraw(p, fb1, 1);
    Check(PanelPlotDraws(p, 3) == out_steady, "steady state: no redraw");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: panel\n");
    return 0;
}
