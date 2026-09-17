// test_panel_pages.cc — golden hash per live page.
//
// Renders every subject's page (NAV1 up through the pane) and checks each
// against a baked hash, so a chrome regression on any page — not just the
// power-on scope — fails the suite.
//
// The panel is the single chrome implementation (the mockup tool was removed),
// so these hashes are the authoritative render of the live engine state.

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

// Golden hash per subject, indexed by SubjectId (pane order). Baked from the
// offscreen render at power-on state (no notes, no audio tap).
static constexpr std::uint32_t kGolden[static_cast<int>(SubjectId::kCount)] = {
    0x41F0CE04u,  // 0 PART
    0xA6D5B39Du,  // 1 OSC1
    0xF7F52331u,  // 2 OSC2
    0x14324B55u,  // 3 OSC3
    0x633B8F89u,  // 4 OSC4
    0x116AFAA4u,  // 5 FILT
    0x60923433u,  // 6 AMP
    0xC9C52BB0u,  // 7 ENV1
    0x6E36BC9Bu,  // 8 ENV2
    0xB678EAE7u,  // 9 ENV3
    0x6CCA2D43u,  // 10 LFO1
    0xD03E3123u,  // 11 LFO2
    0x513B818Bu,  // 12 LFO3
    0xFD3C9A50u,  // 13 MOD
    0xA1CB68BCu,  // 14 FX
    0x987B3456u,  // 15 PATCH
    0x0E4C249Au,  // 16 CONF
};

// The AMP page in MOD view: route lists replace the plot, LEVEL shows its two
// default inbound routes. Locks the route-list + plot-suppression render.
static constexpr std::uint32_t kModViewGolden = 0xCC027CF9u;

// The AMP page in MOD arm: the pane shows the source list, the columns the
// arm overlay. Locks the source-pane + arm-overlay render.
static constexpr std::uint32_t kModArmGolden = 0x0E4F4328u;

int main() {
    engine::SharedIpc ipc;
    engine::EngineControl control;
    control.Init(ipc);
    Panel *p = PanelCreate();
    Interaction it;
    it.Init(p, Surface(), &control);  // power-on: kFilt + scope_mode = kScope

    static std::uint16_t buf0[kW * kH];
    static std::uint16_t buf1[kW * kH];
    FrameBuffer fb0 = {buf0, kW, kH, kW, Rect{0, 0, kW, kH}};
    FrameBuffer fb1 = {buf1, kW, kH, kW, Rect{0, 0, kW, kH}};
    PanelDraw(p, fb0, 0);
    PanelDraw(p, fb1, 1);

    std::uint32_t t = 0;
    for (int step = 0; step < static_cast<int>(SubjectId::kCount); ++step) {
        const int subj = static_cast<int>(it.Nav().subject);
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const std::uint32_t h = Hash(buf0, kW * kH);
        if (h != kGolden[subj]) {
            std::printf("FAIL: subject %d hash 0x%08X != golden 0x%08X\n",
                        subj, h, kGolden[subj]);
            ++g_failures;
        }
        t += 100;
        it.OnInput(InputEvent{Control::kNav1, 1, Edge::kNone, t});
    }

    // MOD view: to AMP (subject 6), MOD tapped latches kModView.
    {
        for (int i = 0; i < 1; ++i) {  // kFilt -> kAmp (NAV1 forward)
            t += 100;
            it.OnInput(InputEvent{Control::kNav1, 1, Edge::kNone, t});
        }
        t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kDown, t});
        t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kUp, t});
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const std::uint32_t h = Hash(buf0, kW * kH);
        if (h != kModViewGolden) {
            std::printf("FAIL: mod-view hash 0x%08X != golden 0x%08X\n",
                        h, kModViewGolden);
            ++g_failures;
        }
    }

    // MOD arm: MOD held (from kModView) shows the source pane + arm overlay.
    {
        t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kDown, t});
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const std::uint32_t h = Hash(buf0, kW * kH);
        if (h != kModArmGolden) {
            std::printf("FAIL: mod-arm hash 0x%08X != golden 0x%08X\n",
                        h, kModArmGolden);
            ++g_failures;
        }
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: panel pages\n");
    return 0;
}
