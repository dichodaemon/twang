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
#include "pages.h"
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

    // Power-on page is kFilt with scope_mode = kScope (filter curve + embedded
    // scope). Cycle OUT to kOff so the filter plot is the active plot for the
    // rest of the test.
    Check(it.Nav().subject == SubjectId::kFilt, "power-on subject is the filter");
    Check(it.Nav().scope_mode == ScopeMode::kScope,
          "power-on scope_mode is kScope");
    Tap(it, Control::kOut);  // kScope -> kCycle
    Check(it.Nav().scope_mode == ScopeMode::kCycle, "OUT cycles scope -> cycle");
    Tap(it, Control::kOut);  // kCycle -> kSpectrum
    Check(it.Nav().scope_mode == ScopeMode::kSpectrum,
          "OUT cycles cycle -> spectrum");
    Tap(it, Control::kOut);  // kSpectrum -> kOff
    Check(it.Nav().scope_mode == ScopeMode::kOff, "OUT cycles spectrum -> off");

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

    // 4. A mode change marks the affected plot dirty (observed via
    //    PanelPlotDraws). A MOD toggle changes no engine parameter, so the
    //    redraw is the layer's own MarkDirty, not the panel's engine sync.
    //    The plot is suppressed in kModView (route lists replace it), so the
    //    redraw is observed on the round trip back to kEdit.
    {
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const int before = PanelPlotDraws(p, 1);  // filter plot
        Tap(it, Control::kMod);  // kEdit -> kModView: MarkAll, plot suppressed
        Tap(it, Control::kMod);  // kModView -> kEdit: plot dirty, redraws
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        Check(PanelPlotDraws(p, 1) > before,
              "a mode change marks the plots dirty");
    }

    // 5. A MOD tap is a toggle, not a one-way door: tap -> kModView, tap again
    //    -> kEdit. Regression for the kDown-clobbers-mode bug (the toggle must
    //    snapshot the pre-press mode, not re-read it after kDown sets kModArm).
    {
        Check(it.Nav().mode == ViewMode::kEdit,
              "section 4 returned to kEdit");
        Tap(it, Control::kMod);
        Check(it.Nav().mode == ViewMode::kModView,
              "MOD tap enters kModView");
        Tap(it, Control::kMod);
        Check(it.Nav().mode == ViewMode::kEdit,
              "second MOD tap exits kModView to kEdit");
        Tap(it, Control::kMod);
        Check(it.Nav().mode == ViewMode::kModView,
              "third MOD tap re-enters kModView");
    }

    // 6. Route-field dispatch: the MOD page's source/dest/amount columns write
    //    the selected slot's route (turn -> EngineSetRoute).
    {
        if (it.Nav().mode == ViewMode::kModView) Tap(it, Control::kMod);
        for (int i = 0; i < 8; ++i) Turn(it, Control::kNav1, 1);  // kFilt -> kMod
        Check(it.Nav().subject == SubjectId::kMod, "NAV1 reaches the MOD page");

        // NAV2 walks the slot list; select slot 5 (past the 5 default routes).
        for (int i = 0; i < 5; ++i) Turn(it, Control::kNav2, 1);
        Check(it.Nav().item[static_cast<int>(SubjectId::kMod)] == 5,
              "NAV2 selects slot 5");

        const int part = static_cast<int>(it.Nav().part);
        engine::ModRoute r;

        // Source (Enc 0): an empty slot seeds kVelocity, then advances +1.
        Turn(it, Enc(0), 1);
        Check(control.GetRoute(part, 5, &r) &&
                  r.source == engine::ModSourceId::kNote &&
                  r.dst.id == engine::ParamId::kCutoff,
              "source turn creates kNote -> cutoff");

        // Dest (Enc 1): cycles cutoff -> resonance (the next modulatable id).
        Turn(it, Enc(1), 1);
        Check(control.GetRoute(part, 5, &r) &&
                  r.dst.id == engine::ParamId::kResonance &&
                  r.source == engine::ModSourceId::kNote,
              "dest turn cycles cutoff -> resonance");

        // Amount (Enc 2): a positive turn raises the amount.
        Turn(it, Enc(2), 1);
        Check(control.GetRoute(part, 5, &r) && r.amount > 0.0f,
              "amount turn raises the amount");
    }

    // 7. CONF feel editing: an encoder turn over a CONF feel column edits the
    //    runtime feel; a long press reverts the control to its default.
    {
        for (int i = 0; i < 3; ++i) Turn(it, Control::kNav1, 1);  // kMod -> kConf
        Check(it.Nav().subject == SubjectId::kConf,
              "NAV1 reaches the CONF page");

        const std::uint8_t det = it.feel.detents_per_rev;
        Turn(it, Enc(0), 1);  // DETENTS
        Check(it.feel.detents_per_rev == det + 1,
              "DETENTS turn raises detents_per_rev");

        const std::uint8_t accel = it.feel.accel_max_default;
        Turn(it, Enc(1), 1);  // ACCEL
        Check(it.feel.accel_max_default == accel + 1,
              "ACCEL turn raises accel_max_default");

        const std::uint16_t thresh = it.feel.accel_threshold_dps;
        Turn(it, Enc(2), 1);  // THRESH
        Check(it.feel.accel_threshold_dps == thresh + 1,
              "THRESH turn raises accel_threshold_dps");

        const std::uint32_t press = it.feel.long_press_ms;
        Turn(it, Enc(3), 1);  // PRESS
        Check(it.feel.long_press_ms == press + 50,
              "PRESS turn raises long_press_ms by 50 ms");

        const std::uint8_t fine = it.feel.fine_divisor;
        Turn(it, Enc(4), 1);  // FINE
        Check(it.feel.fine_divisor == fine + 1,
              "FINE turn raises fine_divisor");

        // A long press (held past long_press_ms) reverts the control exactly
        // once, back to the default.
        g_t += 10;
        it.OnInput(InputEvent{Enc(3), 0, Edge::kDown, g_t});
        g_t += 700;  // clearly past long_press_ms (now 550)
        it.OnInput(InputEvent{Enc(3), 0, Edge::kUp, g_t});
        Check(it.feel.long_press_ms == 500,
              "long press reverts PRESS to its default");
    }

    // 8. Route-table-full alert: arming a route on a full table drops the
    //    write and raises nav.route_full.
    {
        for (int i = 0; i < 6; ++i) Turn(it, Control::kNav1, 1);  // kConf -> kFilt
        Check(it.Nav().subject == SubjectId::kFilt,
              "NAV1 returns to the filter page");

        // Fill all 16 slots with distinct routes (sources 1..15 -> amp, plus
        // velocity -> cutoff), so no slot is free and kNote -> cutoff is absent.
        const int part = static_cast<int>(it.Nav().part);
        const engine::ParamRef amp{0, engine::ParamId::kAmp};
        for (int s = 0; s < 15; ++s)
            control.SetRoute(part, s, static_cast<engine::ModSourceId>(1 + s),
                             amp, 0.5f);
        control.SetRoute(part, 15, engine::ModSourceId::kVelocity,
                         engine::ParamRef{0, engine::ParamId::kCutoff}, 0.5f);

        // Arm kNote; turning the cutoff column writes kNote -> cutoff, which
        // has no match and no free slot. armed_source persists from section 2
        // as kVelocity, so one NAV2 step reaches kNote.
        g_t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kDown, g_t});
        Turn(it, Control::kNav2, 1);  // kVelocity -> kNote
        Check(it.Nav().armed_source == engine::ModSourceId::kNote,
              "NAV2 arms kNote");
        Turn(it, Enc(0), 1);  // write kNote -> cutoff: table full
        Check(it.Nav().route_full,
              "a route write on a full table raises the alert");
    }

    // 9. Invariant 12: feel never changes shape. Editing the CONF feel fields
    //    changes the values, never which parameter a control drives
    //    (ResolveBinding is pure over NavState; feel is not one of its inputs).
    {
        g_t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kUp, g_t});  // release MOD
        Check(it.Nav().mode == ViewMode::kEdit, "MOD release returns to edit");

        // Record the FILT-page binding for every encoder before feel edits.
        Binding before[geom::kColumns] = {};
        for (int c = 0; c < geom::kColumns; ++c)
            before[c] = ResolveBinding(it.Nav(), Enc(c));

        // Edit every CONF feel field through the dispatcher, then return.
        for (int i = 0; i < 11; ++i) Turn(it, Control::kNav1, 1);  // kFilt -> kConf
        Check(it.Nav().subject == SubjectId::kConf,
              "NAV1 reaches the CONF page");
        for (int c = 0; c < geom::kColumns; ++c) Turn(it, Enc(c), 1);
        for (int i = 0; i < 6; ++i) Turn(it, Control::kNav1, 1);  // kConf -> kFilt

        bool same = true;
        for (int c = 0; c < geom::kColumns; ++c) {
            const Binding after = ResolveBinding(it.Nav(), Enc(c));
            same = same && (before[c].kind == after.kind);
            if (before[c].kind == BindKind::kParam)
                same = same && before[c].param.id == after.param.id &&
                       before[c].param.instance == after.param.instance;
        }
        Check(same, "feel editing leaves bindings unchanged (invariant 12)");
    }

    // 10. OUT tap cycles scope_mode (off -> scope -> cycle -> spectrum -> off),
    //     leaving subject/group/focus_col unchanged.
    {
        // Focus column 0 in MOD view so focus_col is non-default.
        Tap(it, Control::kMod);  // kEdit -> kModView
        Tap(it, Enc(0));         // focus column 0
        Check(it.Nav().focus_col == 0, "MOD-view short press focuses column 0");
        Tap(it, Control::kMod);  // kModView -> kEdit (focus_col persists)

        const SubjectId subj = it.Nav().subject;
        const std::uint8_t group = it.Nav().group;
        const std::int8_t focus = it.Nav().focus_col;

        Tap(it, Control::kOut);  // kOff -> kScope
        Check(it.Nav().scope_mode == ScopeMode::kScope, "OUT cycles off -> scope");
        Tap(it, Control::kOut);  // kScope -> kCycle
        Check(it.Nav().scope_mode == ScopeMode::kCycle, "OUT cycles scope -> cycle");
        Tap(it, Control::kOut);  // kCycle -> kSpectrum
        Check(it.Nav().scope_mode == ScopeMode::kSpectrum,
              "OUT cycles cycle -> spectrum");
        Tap(it, Control::kOut);  // kSpectrum -> kOff
        Check(it.Nav().scope_mode == ScopeMode::kOff,
              "OUT cycles spectrum -> off");

        Check(it.Nav().subject == subj && it.Nav().group == group &&
                  it.Nav().focus_col == focus,
              "OUT cycling leaves subject/group/focus_col unchanged");
    }

    // 10b. OUT hold toggles kOutView; entering from kOff forces kScope.
    {
        g_t += 10;
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kDown, g_t});
        g_t += 700;  // past long_press_ms -> kPressLong on release
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kUp, g_t});
        Check(it.Nav().mode == ViewMode::kOutView, "OUT hold enters kOutView");
        Check(it.Nav().scope_mode == ScopeMode::kScope,
              "kOutView entry from kOff forces kScope");

        g_t += 10;
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kDown, g_t});
        g_t += 700;
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kUp, g_t});
        Check(it.Nav().mode == ViewMode::kEdit,
              "second OUT hold exits kOutView to kEdit");
    }

    // 11. Invariant 13: one event causes bounded work. A turn on a column
    //     invalidates the plot once, independent of route-table size — the
    //     invalidation cost never scales with state.
    {
        // Cost of one cutoff turn with the default five routes.
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const int before = PanelPlotDraws(p, 1);  // filter plot
        Turn(it, Enc(0), 1);
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const int delta_default = PanelPlotDraws(p, 1) - before;

        // Fill every route slot (grow the state), then measure the same turn.
        const int part = static_cast<int>(it.Nav().part);
        for (int s = 0; s < engine::kModSlots; ++s)
            control.SetRoute(part, s,
                             static_cast<engine::ModSourceId>(1 + (s % 15)),
                             engine::ParamRef{0, engine::ParamId::kAmp}, 0.5f);

        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const int before2 = PanelPlotDraws(p, 1);
        Turn(it, Enc(0), 1);
        PanelDraw(p, fb0, 0);
        PanelDraw(p, fb1, 1);
        const int delta_full = PanelPlotDraws(p, 1) - before2;

        Check(delta_default == delta_full,
              "work per event is bounded (invariant 13)");
    }

    // 12. Non-modulatable destinations are inert in arm mode (regression: a
    //     SetRoute rejection was mislabelled "route full"). Even with a full
    //     table, a non-modulatable turn raises no alert; a modulatable one
    //     does; and the alert clears on MOD release.
    {
        g_t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kDown, g_t});  // arm
        Turn(it, Enc(4), 1);  // KEY FOLLOW: not modulatable -> inert
        Check(!it.Nav().route_full,
              "non-modulatable destination does not raise route-full");
        Turn(it, Enc(3), 1);  // DRIVE: modulatable, table full -> alert
        Check(it.Nav().route_full,
              "modulatable destination on a full table raises route-full");
        g_t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kUp, g_t});  // release
        Check(!it.Nav().route_full,
              "route-full clears on MOD release");
    }

    // 13. Route removal: long-press clears a slot on the MOD page and the
    //     armed route in arm mode.
    {
        const int part = static_cast<int>(it.Nav().part);
        engine::ModRoute r;

        // MOD page: long-press the SOURCE encoder clears the selected slot.
        for (int i = 0; i < 8; ++i) Turn(it, Control::kNav1, 1);  // kFilt -> kMod
        Check(it.Nav().subject == SubjectId::kMod, "NAV1 reaches the MOD page");
        const int slot = it.Nav().item[static_cast<int>(SubjectId::kMod)];
        Check(control.GetRoute(part, slot, &r), "selected slot occupied");
        g_t += 10;
        it.OnInput(InputEvent{Enc(0), 0, Edge::kDown, g_t});
        g_t += 700;  // past long_press_ms
        it.OnInput(InputEvent{Enc(0), 0, Edge::kUp, g_t});
        Check(!control.GetRoute(part, slot, &r),
              "long-press SOURCE clears the slot");

        // Arm mode: long-press a destination clears the armed route to it.
        for (int i = 0; i < 8; ++i) Turn(it, Control::kNav1, -1);  // kMod -> kFilt
        g_t += 10;
        it.OnInput(InputEvent{Control::kMod, 0, Edge::kDown, g_t});  // arm kNote
        Turn(it, Enc(0), 1);  // write kNote -> cutoff (slot now free)
        bool has = false;
        for (int s = 0; s < engine::kModSlots; ++s)
            if (control.GetRoute(part, s, &r) &&
                r.source == engine::ModSourceId::kNote &&
                r.dst.id == engine::ParamId::kCutoff) { has = true; break; }
        Check(has, "arm writes kNote -> cutoff");
        g_t += 10;
        it.OnInput(InputEvent{Enc(0), 0, Edge::kDown, g_t});
        g_t += 700;
        it.OnInput(InputEvent{Enc(0), 0, Edge::kUp, g_t});
        has = false;
        for (int s = 0; s < engine::kModSlots; ++s)
            if (control.GetRoute(part, s, &r) &&
                r.source == engine::ModSourceId::kNote &&
                r.dst.id == engine::ParamId::kCutoff) { has = true; break; }
        Check(!has, "long-press cutoff clears the armed route");
    }

    // 14. Output settings dispatch: kOutView turns edit TIMEBASE/CYCLES; a
    //     long press reverts the control to its default.
    {
        g_t += 10;
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kDown, g_t});
        g_t += 700;  // kPressLong
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kUp, g_t});
        Check(it.Nav().mode == ViewMode::kOutView, "OUT hold enters kOutView");

        // TIMEBASE (scope mode, Enc 0): a backward turn steps 341 -> 200.
        Check(it.out.timebase_ms == 341, "timebase defaults to 341");
        Turn(it, Enc(0), -1);
        Check(it.out.timebase_ms == 200, "TIMEBASE turn steps 341 -> 200");

        // CYCLES (cycle mode, Enc 0): OUT tap switches the table, +1 -> 4.
        Tap(it, Control::kOut);  // kScope -> kCycle
        Check(it.Nav().scope_mode == ScopeMode::kCycle, "OUT taps to cycle");
        Turn(it, Enc(0), 1);
        Check(it.out.cycles == 4, "CYCLES turn steps 3 -> 4");

        // Long-press reverts CYCLES to its default.
        g_t += 10;
        it.OnInput(InputEvent{Enc(0), 0, Edge::kDown, g_t});
        g_t += 700;
        it.OnInput(InputEvent{Enc(0), 0, Edge::kUp, g_t});
        Check(it.out.cycles == 3, "long press reverts CYCLES to 3");

        // Back to scope; long-press reverts TIMEBASE to 341.
        Tap(it, Control::kOut);  // kCycle -> kSpectrum
        Tap(it, Control::kOut);  // kSpectrum -> kOff
        Tap(it, Control::kOut);  // kOff -> kScope
        Check(it.Nav().scope_mode == ScopeMode::kScope, "OUT back to scope");
        g_t += 10;
        it.OnInput(InputEvent{Enc(0), 0, Edge::kDown, g_t});
        g_t += 700;
        it.OnInput(InputEvent{Enc(0), 0, Edge::kUp, g_t});
        Check(it.out.timebase_ms == 341, "long press reverts TIMEBASE to 341");

        // Exit kOutView back to edit.
        g_t += 10;
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kDown, g_t});
        g_t += 700;
        it.OnInput(InputEvent{Control::kOut, 0, Edge::kUp, g_t});
        Check(it.Nav().mode == ViewMode::kEdit, "OUT hold exits kOutView");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: interaction\n");
    return 0;
}
