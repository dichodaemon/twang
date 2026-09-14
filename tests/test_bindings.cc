// test_bindings.cc — exhaustive binding resolution and page-table validation.
//
// ResolveBinding is pure and its domain is finite, so this sweeps it (arch-
// design §10). Three things matter: totality (every (nav, control) resolves
// without out-of-bounds access), invariant 2 (a kPending column resolves
// kPending, never kParam), and the page table staying coherent at every E
// because grouping is derived, not authored.

#include <cstdio>
#include <utility>

#include "interaction.h"
#include "pages.h"

using namespace nostromo;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

// Sweep one E. The template parameter must be a compile-time constant, so the
// E ∈ {4,5,6,7} sweep is a fold over an integer_sequence, not a runtime loop.
template <int E>
static void SweepE() {
    for (int s = 0; s < static_cast<int>(SubjectId::kCount); ++s) {
        const PageDesc &p = g_pages[s];
        const int gc = (p.n_cols + E - 1) / E;
        Check(GroupCount<E>(p) == gc, "GroupCount == ceil(n/E)");
        for (int g = 0; g < gc; ++g) {
            for (int c = 0; c < E; ++c) {
                const ColumnSpec cs = Column<E>(p, g, c);
                const int i = g * E + c;
                if (i < p.n_cols)
                    Check(cs.kind != ColumnKind::kNone,
                          "in-range column is not kNone");
                else
                    Check(cs.kind == ColumnKind::kNone,
                          "past-end column is kNone");
            }
        }
    }
}

template <int... Es>
static void SweepAll(std::integer_sequence<int, Es...>) {
    (SweepE<Es>(), ...);
}

int main() {
    // 1. Page-table sweep: at E in {4,5,6,7}, GroupCount == ceil(n/E) and every
    //    (group, col) index resolves — kNone only past n_cols. This turns "does
    //    E = 5 work" into a build rather than an argument.
    SweepAll(std::integer_sequence<int, 4, 5, 6, 7>{});

    // 2. Kind preservation in kEdit: a column encoder resolves the same kind it
    //    declares (the tag copy), so a kPending column never leaks as kParam.
    //    Every control also resolves without out-of-bounds access.
    {
        for (int s = 0; s < static_cast<int>(SubjectId::kCount); ++s) {
            const PageDesc &p = g_pages[s];
            NavState nav{};
            nav.subject = static_cast<SubjectId>(s);
            nav.group = 0;
            for (int c = 0; c < geom::kColumns; ++c) {
                const ColumnSpec cs = Column<>(p, 0, c);
                const Binding b = ResolveBinding(nav, Enc(c));
                switch (cs.kind) {
                    case ColumnKind::kParam:
                        Check(b.kind == BindKind::kParam, "kParam -> kParam");
                        break;
                    case ColumnKind::kPending:
                        Check(b.kind == BindKind::kPending,
                              "kPending -> kPending");
                        break;
                    case ColumnKind::kRouteField:
                        Check(b.kind == BindKind::kRouteField,
                              "kRouteField -> kRouteField");
                        break;
                    case ColumnKind::kViewCtl:
                        Check(b.kind == BindKind::kViewCtl,
                              "kViewCtl -> kViewCtl");
                        break;
                    case ColumnKind::kNone:
                        Check(b.kind == BindKind::kNone, "kNone -> kNone");
                        break;
                }
            }
            for (int c = 0; c < static_cast<int>(Control::kCount); ++c)
                (void)ResolveBinding(nav, static_cast<Control>(c));
        }
    }

    // 3. A kParam binding carries the full ParamRef: the instance comes from
    //    the subject, the id from the column.
    {
        NavState nav{};
        nav.subject = SubjectId::kOsc1;
        nav.group = 0;
        Binding coarse = ResolveBinding(nav, Enc(1));
        Check(coarse.kind == BindKind::kParam, "osc coarse is kParam");
        Check(coarse.param.id == engine::ParamId::kPitchCoarse, "coarse id");
        Check(coarse.param.instance == 0, "osc1 -> instance 0");

        nav.subject = SubjectId::kOsc3;
        Binding c3 = ResolveBinding(nav, Enc(1));
        Check(c3.kind == BindKind::kParam && c3.param.instance == 2,
              "osc3 coarse -> instance 2");

        nav.subject = SubjectId::kFilt;
        Check(ResolveBinding(nav, Enc(0)).param.id == engine::ParamId::kCutoff,
              "filt cutoff id");
        Check(ResolveBinding(nav, Enc(1)).param.id == engine::ParamId::kResonance,
              "filt resonance id");
        Check(ResolveBinding(nav, Enc(3)).param.id == engine::ParamId::kDrive,
              "filt drive id");
        Check(ResolveBinding(nav, Enc(4)).param.id == engine::ParamId::kKeyFollowDepth,
              "filt keytrack id");
        Check(ResolveBinding(nav, Enc(2)).kind == BindKind::kPending,
              "filt env amt (pending) -> kPending");
    }

    // 4. Buttons and navigation.
    {
        NavState nav{};
        nav.subject = SubjectId::kFilt;  // item_axis kNone
        Check(ResolveBinding(nav, Control::kNav1).kind == BindKind::kNavSubject,
              "nav1 -> kNavSubject");
        Check(ResolveBinding(nav, Control::kNav2).kind == BindKind::kNone,
              "nav2 on a kNone page");
        Check(ResolveBinding(nav, Control::kPart0).kind == BindKind::kPartSelect,
              "part0 -> kPartSelect");
        Check(ResolveBinding(nav, Control::kMod).kind == BindKind::kModeToggle,
              "mod -> kModeToggle");
        Check(ResolveBinding(nav, Control::kGroup).kind == BindKind::kGroupCycle,
              "group -> kGroupCycle");
        Check(ResolveBinding(nav, Control::kOut).kind == BindKind::kOutToggle,
              "out -> kOutToggle");
        Check(ResolveBinding(nav, Control::kPerf).kind == BindKind::kNone,
              "perf reserved -> kNone");

        nav.subject = SubjectId::kMod;  // item_axis kSlots
        Check(ResolveBinding(nav, Control::kNav2).kind == BindKind::kNavItem,
              "nav2 on a slots page");
        nav.subject = SubjectId::kPatch;  // item_axis kPatches
        Check(ResolveBinding(nav, Control::kNav2).kind == BindKind::kNavItem,
              "nav2 on a patches page");
    }

    // 5. A kRouteField binding carries the slot from item[subject].
    {
        NavState nav{};
        nav.subject = SubjectId::kMod;
        nav.group = 0;
        nav.item[static_cast<int>(SubjectId::kMod)] = 3;
        Binding src = ResolveBinding(nav, Enc(0));
        Check(src.kind == BindKind::kRouteField, "mod source -> kRouteField");
        Check(src.field == RouteField::kSource, "mod source field");
        Check(src.slot == 3, "route-field binding carries the slot");
        Check(ResolveBinding(nav, Enc(1)).field == RouteField::kDest, "mod dest field");
        Check(ResolveBinding(nav, Enc(2)).field == RouteField::kAmount,
              "mod amount field");
        Check(ResolveBinding(nav, Enc(3)).kind == BindKind::kPending,
              "mod curve pending");
    }

    // 6. Mode overlay: MOD held turns a parameter column into a route
    //    destination; NAV2 walks the source list; kPerform reserves columns.
    {
        NavState nav{};
        nav.subject = SubjectId::kFilt;
        nav.group = 0;
        nav.mode = ViewMode::kModArm;
        Binding b = ResolveBinding(nav, Enc(0));
        Check(b.kind == BindKind::kRouteAmount, "mod-arm cutoff -> kRouteAmount");
        Check(b.param.id == engine::ParamId::kCutoff, "mod-arm cutoff dst");
        Check(ResolveBinding(nav, Control::kNav2).kind == BindKind::kNavItem,
              "mod-arm nav2 walks sources");

        nav.subject = SubjectId::kOsc1;
        Check(ResolveBinding(nav, Enc(0)).kind == BindKind::kPending,
              "mod-arm pending stays pending");

        nav.mode = ViewMode::kPerform;
        Check(ResolveBinding(nav, Enc(1)).kind == BindKind::kNone,
              "perform reserves columns");
    }

    // 7. PATCH/CONF view-control columns; FX page has no columns.
    {
        NavState nav{};
        nav.subject = SubjectId::kPatch;
        nav.group = 0;
        Binding cat = ResolveBinding(nav, Enc(0));
        Check(cat.kind == BindKind::kViewCtl && cat.ctl == ViewCtl::kCategory,
              "patch category -> kViewCtl");
        nav.subject = SubjectId::kConf;
        Check(ResolveBinding(nav, Enc(0)).ctl == ViewCtl::kDetents,
              "conf detents ctl");
        nav.subject = SubjectId::kFx;
        Check(ResolveBinding(nav, Enc(0)).kind == BindKind::kNone,
              "fx page has no columns");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: bindings\n");
    return 0;
}
