// test_panel.cc — golden image, note-on playhead, and invalidation gating,
// exercised against the double-buffered panel (two framebuffers, swapped).

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

static std::uint32_t Hash(const std::uint16_t *px, int n) {
    std::uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}

// Plot regions (from the mockup layout: X+6, 84+58, 230, 232).
// filter = module 1 (x=266), envelope = module 2 (x=516).
static constexpr int kFilterX = 272, kEnvX = 522, kPlotY = 142;
static constexpr int kPlotW = 230, kPlotH = 232;

static std::uint32_t RegionHash(const std::uint16_t *buf, int x, int y,
                                int w, int hgt) {
    std::uint32_t acc = 2166136261u;
    for (int r = 0; r < hgt; ++r) {
        const std::uint16_t *row = buf + (y + r) * kW + x;
        for (int c = 0; c < w; ++c) {
            acc ^= row[c];
            acc *= 16777619u;
        }
    }
    return acc;
}

// Golden-image hash of the initial (deterministic) full render.
static constexpr std::uint32_t kExpectedHash = 0xB85EE087;

int main() {
    Panel *p = PanelCreate();

    // Two framebuffers — the panel is buffer-agnostic and draws each once.
    static std::uint16_t buf0[kW * kH];
    static std::uint16_t buf1[kW * kH];
    FrameBuffer fb0 = {buf0, kW, kH, kW, Rect{0, 0, kW, kH}};
    FrameBuffer fb1 = {buf1, kW, kH, kW, Rect{0, 0, kW, kH}};

    // Frame 1 (buffer 0) + frame 2 (buffer 1): both full renders.
    PanelDraw(p, fb0);
    PanelDraw(p, fb1);

    const std::uint32_t h0 = Hash(buf0, kW * kH);
    const std::uint32_t h1 = Hash(buf1, kW * kH);
    std::printf("buffer0 hash: 0x%08X  buffer1 hash: 0x%08X\n", h0, h1);
    Check(h0 != 0, "golden image: something rendered");
    Check(h0 == h1, "double buffering: both buffers render identically");
    Check(h0 == kExpectedHash, "golden image matches the reference hash");

    // Note-on drives the envelope playhead into BOTH buffers.
    const std::uint32_t env_before = RegionHash(buf0, kEnvX, kPlotY, kPlotW, kPlotH);
    PanelNoteOn(p, 440.0f);
    PanelDraw(p, fb0);  // frame 3, buffer 0 — redraws the dirty plots
    PanelDraw(p, fb1);  // frame 4, buffer 1 — redraws the dirty plots
    const std::uint32_t env_after0 = RegionHash(buf0, kEnvX, kPlotY, kPlotW, kPlotH);
    const std::uint32_t env_after1 = RegionHash(buf1, kEnvX, kPlotY, kPlotW, kPlotH);
    Check(env_before != env_after0, "note-on drives the playhead (buffer 0)");
    Check(env_before != env_after1, "note-on drives the playhead (buffer 1)");

    // Invalidation: note-on redrew the dirty osc/envelope/output plots (two
    // buffers each) but left the clean filter plot untouched.
    Check(PanelPlotDraws(p, 1) == 2, "clean filter plot not redrawn");
    Check(PanelPlotDraws(p, 2) == 4, "dirty envelope plot redrawn (both buffers)");

    // Steady state: a no-change frame redraws nothing.
    const int osc_before = PanelPlotDraws(p, 0);
    PanelDraw(p, fb0);
    Check(PanelPlotDraws(p, 0) == osc_before, "steady state: no redraw");

    // Scope animation: an audio tap invalidates the output plot — the one
    // plot that animates in steady state — and redraws it into both buffers.
    const int out_before = PanelPlotDraws(p, 3);
    const int env_before_tap = PanelPlotDraws(p, 2);
    float samples[64];
    for (int i = 0; i < 64; ++i) samples[i] = (i % 8) / 8.0f;
    PanelAudioTap(p, samples, 64);
    PanelDraw(p, fb1);  // fb_index is 1 after the steady-state draw of fb0
    PanelDraw(p, fb0);
    Check(PanelPlotDraws(p, 3) == out_before + 2,
          "audio tap redraws the scope into both buffers");
    Check(PanelPlotDraws(p, 2) == env_before_tap,
          "audio tap leaves other plots clean");

    // Filter cursor drag: moves the cutoff cursor (exercising the cursor
    // erase) and redraws the filter plot into both buffers.
    const int filter_before = PanelPlotDraws(p, 1);
    PanelPointer(p, PointerEvent{PointerKind::kPress, kFilterX + 100, kPlotY + 100});
    PanelPointer(p, PointerEvent{PointerKind::kMove, kFilterX + 130, kPlotY + 80});
    PanelPointer(p, PointerEvent{PointerKind::kRelease, kFilterX + 130, kPlotY + 80});
    PanelDraw(p, fb1);
    PanelDraw(p, fb0);
    Check(PanelPlotDraws(p, 1) == filter_before + 2,
          "filter drag redraws the filter plot into both buffers");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: panel\n");
    return 0;
}
