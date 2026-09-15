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
    0xDB0E2AA2u,  // 0 PART
    0x5DFBAC32u,  // 1 OSC1
    0xCEEF924Eu,  // 2 OSC2
    0x8570CDFAu,  // 3 OSC3
    0xBD4E72B6u,  // 4 OSC4
    0x348E89E3u,  // 5 FILT
    0xFB73FAC0u,  // 6 AMP
    0xABDCCFA9u,  // 7 ENV1
    0xB6FC3C8Eu,  // 8 ENV2
    0x83CB754Au,  // 9 ENV3
    0xE24AC19Eu,  // 10 LFO1
    0x92A59D6Eu,  // 11 LFO2
    0x1637844Eu,  // 12 LFO3
    0xC2266A3Du,  // 13 MOD
    0x263EB13Eu,  // 14 OUT SCOPE
    0xF39EE727u,  // 15 OUT CYCLE
    0x32D99117u,  // 16 OUT SPECTRUM
    0xA037A982u,  // 17 FX
    0x0B118F50u,  // 18 PATCH
    0x1DCAF6F2u,  // 19 CONF
};

int main() {
    engine::SharedIpc ipc;
    engine::EngineControl control;
    control.Init(ipc);
    Panel *p = PanelCreate();
    Interaction it;
    it.Init(p, Surface(), &control);  // power-on: kOutScope

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

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: panel pages\n");
    return 0;
}
