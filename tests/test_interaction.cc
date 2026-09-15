// test_interaction.cc — end-to-end interaction-layer behaviour.
//
// Drives the layer through Interaction::OnInput and checks the engine and the
// panel react: a turn writes the right parameter and no other; MOD held + an
// armed source + a turn creates a route; a part change leaves the navigation
// state invariant; and a change marks the affected plot dirty (observed via
// PanelPlotDraws).

#include <cstdint>
#include <cstdio>

#include "engine.h"
#include "engine_control.h"
#include "fb.h"
#include "interaction.h"
#include "panel.h"
#include "params.h"
#include "surface.h"

using namespace nostromo;

static constexpr int kW = 1024;
static constexpr int kH = 600;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static std::uint32_t g_t = 0;

static void Turn(Interaction &it, Control c, std::int8_t detents) {
    g_t += 100;
    it.OnInput(InputEvent{c, detents, Edge::kNone, g_t});
}

static void Tap(Interaction &it, Control c) {
    g_t += 10;
    it.OnInput(InputEvent{c, 0, Edge::kDown, g_t});
    g_t += 10;
    it.OnInput(InputEvent{c, 0, Edge::kUp, g_t});
}

int main() {
    engine::SharedIpc ipc;
    engine::EngineControl control;
    control.Init(ipc);
    Panel *p = PanelCreate();
    static std::uint16_t buf0[kW * kH];
    static std::uint16_t buf1[kW * kH];
    FrameBuffer fb0 = {buf0, kW, kH, kW, Rect{0, 0, kW, kH}};
    FrameBuffer fb1 = {buf1, kW, kH, kW, Rect{0, 0, kW, kH}};

    Interaction it;
    it.Init(p, Surface(), &control);

    PanelDraw(p, fb0, 0);
    PanelDraw(p, fb1, 1);

    // Power-on page is kOutScope (index 14); walk NAV1 back 9 to kFilt (5).
    for (int i = 0; i < 9; ++i) Turn(it, Control::kNav1, -1);
    Check(it.Nav().subject == SubjectId::kFilt,
          "NAV1 walks to the filter page");

    // 1. A turn on the cutoff column writes cutoff and no other parameter.
    {
        const float reso = control.GetParam(0, {0, engine::ParamId::kResonance});
        const float atk = control.GetParam(0, {0, engine::ParamId::kAttack});
        const float cutoff = control.GetParam(0, {0, engine::ParamId::kCutoff});
        Turn(it, Enc(0), -1);  // cutoff (default 1.0) lowers on a backward turn
        Check(control.GetParam(0, {0, engine::ParamId::kCutoff}) < cutoff,
              "cutoff turn lowers cutoff");
        Check(control.GetParam(0, {0, engine::ParamId::kResonance}) == reso,
              "resonance unchanged");
        Check(control.GetParam(0, {0, engine::ParamId::kAttack}) == atk,
              "attack unchanged");
    }

    // 2. MOD held + an armed source + a turn creates a route; MOD up returns
    //    to the prior mode.
    {
        g_t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kDown, g_t});
        Check(it.Nav().mode == ViewMode::kModArm,
              "MOD down arms");
        Turn(it, Control::kNav2, 1);  // arm a source: kNone -> kVelocity
        Check(it.Nav().armed_source == engine::ModSourceId::kVelocity,
              "NAV2 arms velocity");
        Turn(it, Enc(0), 1);  // write velocity -> cutoff
        g_t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kUp, g_t});
        Check(it.Nav().mode == ViewMode::kEdit,
              "MOD up returns to edit after a route");

        engine::ModRoute r;
        Check(control.GetRoute(0, 5, &r) &&
                  r.source == engine::ModSourceId::kVelocity &&
                  r.dst.id == engine::ParamId::kCutoff,
              "route velocity -> cutoff created");
    }

    // 3. A part change sets part and leaves subject/group/item/mode invariant.
    {
        const NavState &nav = it.Nav();
        const SubjectId subj = nav.subject;
        const std::uint8_t group = nav.group;
        const std::uint8_t item = nav.item[static_cast<int>(subj)];
        const ViewMode mode = nav.mode;
        Tap(it, Control::kPart2);
        const NavState &n2 = it.Nav();
        Check(n2.part == 2, "part changed to 2");
        Check(n2.subject == subj && n2.group == group && n2.mode == mode,
              "subject/group/mode invariant across a part change");
        Check(n2.item[static_cast<int>(subj)] == item, "item invariant");
    }

    // 4. A change marks the affected plot dirty (observed via PanelPlotDraws).
    //    A MOD toggle changes no engine parameter, so the redraw is the layer's
    //    own MarkDirty, not the panel's engine sync.
    {
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const int before = PanelPlotDraws(p, 1);  // filter plot
        Tap(it, Control::kMod);  // toggle kModView -> MarkAll
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        Check(PanelPlotDraws(p, 1) > before,
              "a mode change marks the plots dirty");
    }

    // 5. A MOD tap is a toggle, not a one-way door: tap -> kModView, tap again
    //    -> kEdit. Regression for the kDown-clobbers-mode bug (the toggle must
    //    snapshot the pre-press mode, not re-read it after kDown sets kModArm).
    {
        Check(it.Nav().mode == ViewMode::kModView,
              "section 4 left the layer in kModView");
        Tap(it, Control::kMod);
        Check(it.Nav().mode == ViewMode::kEdit,
              "second MOD tap exits kModView to kEdit");
        Tap(it, Control::kMod);
        Check(it.Nav().mode == ViewMode::kModView,
              "third MOD tap re-enters kModView");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: interaction\n");
    return 0;
}
