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
    0x38326A95u,  // 0 PART
    0x6E36BF58u,  // 1 OSC1
    0x32D5B4A4u,  // 2 OSC2
    0x0E2ACCC0u,  // 3 OSC3
    0xE9B073ACu,  // 4 OSC4
    0x87F52279u,  // 5 FILT
    0x8BA4E60Au,  // 6 AMP
    0xBD8B1B19u,  // 7 ENV1
    0x23AA97E2u,  // 8 ENV2
    0x5CF40B16u,  // 9 ENV3
    0xD7F53542u,  // 10 LFO1
    0xEDAC3EDAu,  // 11 LFO2
    0x915118E2u,  // 12 LFO3
    0xAB7C7BC9u,  // 13 MOD
    0x013F6CFDu,  // 14 FX
    0x0AF4C243u,  // 15 PATCH
    0xC6A7302Fu,  // 16 CONF
};

// The AMP page in MOD view: route lists replace the plot, LEVEL shows its two
// default inbound routes. Locks the route-list + plot-suppression render.
static constexpr std::uint32_t kModViewGolden = 0xCC027CF9u;

// The AMP page in MOD arm: the pane shows the source list, the columns the
// arm overlay. Locks the source-pane + arm-overlay render.
static constexpr std::uint32_t kModArmGolden = 0x59267AB5u;

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
