// test_panel.cc — golden image, note-on playhead, and invalidation gating.

#include <cstdio>

#include "fb.h"
#include "panel.h"

using namespace spike;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static constexpr int kW = 1024;
static constexpr int kH = 600;
static std::uint16_t g_buf[kW * kH];
static FrameBuffer g_fb = {g_buf, kW, kH, kW, Rect{0, 0, kW, kH}};

static std::uint32_t Hash(const std::uint16_t *px, int n) {
    std::uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}

static std::uint32_t RegionHash(int x, int y, int w, int hgt) {
    std::uint32_t acc = 2166136261u;
    for (int r = 0; r < hgt; ++r) {
        const std::uint16_t *row = g_buf + (y + r) * kW + x;
        for (int c = 0; c < w; ++c) {
            acc ^= row[c];
            acc *= 16777619u;
        }
    }
    return acc;
}

// Plot regions (from the mockup layout: X+6, 84+58, 230, 232).
// filter = module 1 (x=266), envelope = module 2 (x=516).
static constexpr int kFilterX = 272, kEnvX = 522, kPlotY = 142;
static constexpr int kPlotW = 230, kPlotH = 232;

// Golden-image hash of the initial (deterministic) render.
static constexpr std::uint32_t kExpectedHash = 0x7E3A8171;

int main() {
    Panel *p = PanelCreate();

    // 1. Golden image: full render of the initial state is deterministic.
    PanelDraw(p, g_fb);
    const std::uint32_t full = Hash(g_buf, kW * kH);
    std::printf("full hash: 0x%08X\n", full);
    Check(full != 0, "golden image: something rendered");
    Check(full == kExpectedHash, "golden image matches the reference hash");

    // 2. Note-on drives the envelope playhead.
    const std::uint32_t env_before = RegionHash(kEnvX, kPlotY, kPlotW, kPlotH);
    PanelNoteOn(p, 440.0f);
    PanelDraw(p, g_fb);
    const std::uint32_t env_after = RegionHash(kEnvX, kPlotY, kPlotW, kPlotH);
    Check(env_before != env_after, "note-on drives the envelope playhead");

    // 3. Invalidation: note-on redrew the dirty envelope/osc/output plots but
    // left the clean filter plot untouched.
    Check(PanelPlotDraws(p, 1) == 1, "clean filter plot not redrawn");
    Check(PanelPlotDraws(p, 2) >= 2, "dirty envelope plot redrawn");

    // 4. Steady state: a no-change frame redraws nothing.
    const int osc_before = PanelPlotDraws(p, 0);
    PanelDraw(p, g_fb);
    Check(PanelPlotDraws(p, 0) == osc_before, "steady state: no redraw");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: panel\n");
    return 0;
}
