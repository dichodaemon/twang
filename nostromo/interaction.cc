// interaction.cc — the interaction layer's stateful components.
//
// Sits between the input driver and the engine (arch-design §4): resolves what
// each physical control drives, applies gestures to that binding, and marks the
// affected slots dirty through the panel's MarkDirty — the sole invalidation
// entry point. Control-side only (the M33 input task on the target): no
// drawing, no allocation, no direct invalidation-state writes.

#include "interaction.h"

#include "engine.h"
#include "feel.h"
#include "pages.h"
#include "panel.h"
#include "params.h"
#include "surface.h"

namespace nostromo {

namespace {

// Base normalized increment per detent (a §13 tunable, measured during
// implementation). One detent moves an ordinary parameter by this fraction of
// its [0,1] range before acceleration.
constexpr float kDetentStep = 0.004f;

// Route-amount step and acceleration cap. Amounts are route fields, not
// parameters, so the cap is a Dispatcher constant (arch-design §7.8's
// "3 = capped 3x"), not a ParamDesc field (Design Decisions).
constexpr float kRouteAmountStep = 0.008f;
constexpr float kRouteAmountAccel = 3.0f;

// The modulation-source list kModArm's NAV2 walks: kNone (0) is the empty
// sentinel and is skipped.
constexpr int kModSourceCount =
    static_cast<int>(engine::ModSourceId::kConstant) + 1;

// ---- singleton state (control-side; the layer is a singleton) ----
Panel *g_panel = nullptr;
NavState g_nav;
PressState g_press[static_cast<int>(Control::kCount)];
std::uint32_t g_last_turn_ms[static_cast<int>(Control::kCount)];
bool g_arm_used = false;

const PageDesc &PageOf(SubjectId s) {
  return g_pages[static_cast<int>(s)];
}

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void MarkPlot(SlotIdx idx) {
  if (g_panel) MarkDirty(g_panel, idx);
}

void MarkPage() {
  const std::int8_t slot = PageOf(g_nav.subject).dyn_slot;
  if (slot >= 0) MarkPlot(static_cast<SlotIdx>(slot));
}

void MarkAll() {
  MarkPlot(kSlotOsc);
  MarkPlot(kSlotFilter);
  MarkPlot(kSlotEnv);
  MarkPlot(kSlotOut);
}

// Turn rate in detents/second, tracked across events per control.
float TurnRate(Control c, std::int8_t detents, std::uint32_t t_ms) {
  const int ci = static_cast<int>(c);
  const std::uint32_t dt = t_ms - g_last_turn_ms[ci];
  g_last_turn_ms[ci] = t_ms;
  if (dt == 0) return 1000.0f;  // first turn or same-ms burst: treat as fast
  const int mag = detents < 0 ? -static_cast<int>(detents) : detents;
  return static_cast<float>(mag) * 1000.0f / static_cast<float>(dt);
}

// Signed normalized delta for a parameter turn.
float ParamDelta(const engine::ParamDesc &desc, std::int8_t detents,
                 float rate_dps, bool fine) {
  float step = kDetentStep;
  if (rate_dps > static_cast<float>(g_feel.accel_threshold_dps))
    step *= static_cast<float>(desc.accel_max);
  if (fine) step /= static_cast<float>(g_feel.fine_divisor);
  return static_cast<float>(detents) * step;
}

// Current amount of the (src, dst) route, or 0 if none.
float RouteAmount(int part, engine::ModSourceId src, engine::ParamRef dst) {
  engine::ModRoute r;
  for (int s = 0; s < engine::kModSlots; ++s)
    if (engine::EngineGetRoute(part, s, &r) && r.source == src &&
        r.dst.id == dst.id && r.dst.instance == dst.instance)
      return r.amount;
  return 0.0f;
}

bool IsOut(SubjectId s) {
  return s == SubjectId::kOutScope || s == SubjectId::kOutCycle ||
         s == SubjectId::kOutSpec;
}

// Item-axis length for NAV2. kNone axes are never walked (ResolveBinding
// returns kNone for NAV2 on them).
int ItemCount(ItemAxis axis) {
  switch (axis) {
    case ItemAxis::kSlots: return engine::kModSlots;
    case ItemAxis::kPatches: return 1;  // no patch storage yet
    case ItemAxis::kRoutes: return 1;   // no route list yet
    default: return 0;
  }
}

// Applies one gesture/binding — the only side-effecting component.
void Dispatcher(const InputEvent &ev, Gesture g, const Binding &b) {
  switch (b.kind) {
    case BindKind::kParam: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        const engine::ParamDesc &desc =
            engine::g_params[static_cast<std::size_t>(b.param.id)];
        const float rate = TurnRate(ev.control, ev.detents, ev.t_ms);
        const float delta =
            ParamDelta(desc, ev.detents, rate, g == Gesture::kHoldTurn);
        const float cur = engine::EngineGetParam(g_nav.part, b.param);
        float next = Clamp01(cur + delta);
        // zero_notch: a bipolar parameter needs an extra detent to leave the
        // center (0.5), so a single detent across it lands exactly on it.
        if (desc.zero_notch &&
            ((cur < 0.5f && next >= 0.5f) || (cur > 0.5f && next <= 0.5f)))
          next = 0.5f;
        engine::EngineSetParam(g_nav.part, b.param, next);
        MarkPage();
      } else if (g == Gesture::kPressLong) {
        // Revert to the default exactly once, no intermediate write.
        const engine::ParamDesc &desc =
            engine::g_params[static_cast<std::size_t>(b.param.id)];
        engine::EngineSetParam(g_nav.part, b.param, desc.def);
        MarkPage();
      }
      // kPressShort on a continuous parameter: nothing to descend into.
      break;
    }
    case BindKind::kRouteAmount: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        const float rate = TurnRate(ev.control, ev.detents, ev.t_ms);
        float step = kRouteAmountStep;
        if (rate > static_cast<float>(g_feel.accel_threshold_dps))
          step *= kRouteAmountAccel;
        if (g == Gesture::kHoldTurn)
          step /= static_cast<float>(g_feel.fine_divisor);
        const float old = RouteAmount(g_nav.part, g_nav.armed_source, b.param);
        float amt = old + static_cast<float>(ev.detents) * step;
        if (amt < -1.0f) amt = -1.0f;
        if (amt > 1.0f) amt = 1.0f;
        InteractionCreateRoute(g_nav.part, g_nav.armed_source, b.param, amt);
        g_arm_used = true;
        MarkPage();
      }
      break;
    }
    case BindKind::kNavSubject: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        int s = static_cast<int>(g_nav.subject) + (ev.detents > 0 ? 1 : -1);
        if (s < 0) s = static_cast<int>(SubjectId::kCount) - 1;
        if (s >= static_cast<int>(SubjectId::kCount)) s = 0;
        g_nav.subject = static_cast<SubjectId>(s);
        g_nav.group = 0;  // each subject starts at its hot set
        MarkPage();
      }
      break;
    }
    case BindKind::kNavItem: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        const int dir = ev.detents > 0 ? 1 : -1;
        if (g_nav.mode == ViewMode::kModArm) {
          // MOD held: NAV2 walks the source list (kNone skipped).
          int src = static_cast<int>(g_nav.armed_source) + dir;
          if (src <= static_cast<int>(engine::ModSourceId::kNone))
            src = kModSourceCount - 1;
          if (src >= kModSourceCount)
            src = static_cast<int>(engine::ModSourceId::kVelocity);
          g_nav.armed_source = static_cast<engine::ModSourceId>(src);
        } else {
          const int max = ItemCount(PageOf(g_nav.subject).item_axis);
          if (max > 0) {
            int cur = static_cast<int>(
                          g_nav.item[static_cast<int>(g_nav.subject)]) +
                      dir;
            if (cur < 0) cur = max - 1;
            if (cur >= max) cur = 0;
            g_nav.item[static_cast<int>(g_nav.subject)] =
                static_cast<std::uint8_t>(cur);
          }
        }
        MarkPage();
      }
      break;
    }
    case BindKind::kPartSelect: {
      if (g == Gesture::kPressShort) {
        g_nav.part = static_cast<std::uint8_t>(
            static_cast<int>(ev.control) - static_cast<int>(Control::kPart0));
        MarkPage();  // values only; subject/group/item/mode invariant
      }
      break;
    }
    case BindKind::kModeToggle: {
      if (ev.edge == Edge::kDown) {
        g_arm_used = false;
        g_nav.mode = ViewMode::kModArm;  // momentary
        MarkAll();
      } else if (ev.edge == Edge::kUp) {
        if (g_arm_used) {
          g_nav.mode = ViewMode::kEdit;  // a route was armed: plain return
          g_arm_used = false;
        } else if (g == Gesture::kPressShort) {
          g_nav.mode = (g_nav.mode == ViewMode::kModView) ? ViewMode::kEdit
                                                          : ViewMode::kModView;
        } else {
          g_nav.mode = ViewMode::kEdit;  // held past the threshold
        }
        MarkAll();
      }
      break;
    }
    case BindKind::kGroupCycle: {
      if (g == Gesture::kPressShort) {
        const int gc = GroupCount<>(PageOf(g_nav.subject));
        if (gc > 0) g_nav.group = (g_nav.group + 1) % gc;
        MarkPage();
      }
      break;
    }
    case BindKind::kOutToggle: {
      if (g == Gesture::kPressShort) {
        if (IsOut(g_nav.subject)) {
          g_nav.subject = g_nav.prev.subject;
          g_nav.group = g_nav.prev.group;
          g_nav.focus_col = g_nav.prev.focus_col;
        } else {
          g_nav.prev = NavPos{g_nav.subject, g_nav.group, g_nav.focus_col};
          g_nav.subject = SubjectId::kOutScope;
          g_nav.group = 0;
          g_nav.focus_col = -1;
        }
        MarkPage();
      }
      break;
    }
    case BindKind::kPending:
    case BindKind::kNone:
    case BindKind::kRouteField:
    case BindKind::kViewCtl:
    default:
      break;  // pending is inert; route-field/view-control dispatch is later
  }
}

}  // namespace

Gesture Recognize(const InputEvent &ev, PressState &st) {
  if (ev.edge == Edge::kDown) {
    st.pressed = true;
    st.press_t_ms = ev.t_ms;
    st.detent = false;
    return Gesture::kNone;
  }
  if (ev.edge == Edge::kUp) {
    if (!st.pressed) return Gesture::kNone;  // release without a press
    const std::uint32_t elapsed = ev.t_ms - st.press_t_ms;
    const bool absorbed = st.detent;
    st.pressed = false;
    st.detent = false;
    if (absorbed) return Gesture::kNone;  // detent absorption (§8)
    return (elapsed < g_feel.long_press_ms) ? Gesture::kPressShort
                                            : Gesture::kPressLong;
  }
  if (ev.detents != 0) {
    if (st.pressed) {
      st.detent = true;
      return Gesture::kHoldTurn;
    }
    return Gesture::kTurn;
  }
  return Gesture::kNone;
}

void InteractionInit(Panel *panel, const SurfaceProfile &surface) {
  g_panel = panel;
  SetSurface(surface);
  // g_feel keeps its defaults (persisted-settings load is deferred).
  g_nav = NavState{};
  g_nav.part = 0;
  g_nav.subject = SubjectId::kOutScope;
  g_nav.prev = NavPos{SubjectId::kFilt, 0, -1};
  g_nav.group = 0;
  g_nav.focus_col = -1;
  g_nav.mode = ViewMode::kEdit;
  for (auto &st : g_press) st = PressState{};
  for (auto &t : g_last_turn_ms) t = 0;
  g_arm_used = false;
  MarkAll();
  // A shortfall below kColumns is reported once here; the surplus (an encoder
  // bank wider than the column count) is simply unmapped in the profile.
  (void)surface.n_encoders;
}

void InteractionOnInput(const InputEvent &ev) {
  if (static_cast<int>(ev.control) >= static_cast<int>(Control::kCount)) return;
  Gesture g = Recognize(ev, g_press[static_cast<int>(ev.control)]);
  const Binding b = ResolveBinding(g_nav, ev.control);
  // MOD is modal: its down edge arms even though the recognizer emits nothing
  // for a press start.
  if (g == Gesture::kNone &&
      !(ev.edge == Edge::kDown && b.kind == BindKind::kModeToggle))
    return;
  Dispatcher(ev, g, b);
}

bool InteractionCreateRoute(std::uint8_t part, engine::ModSourceId src,
                            engine::ParamRef dst, float amount) {
  int free = -1;
  engine::ModRoute r;
  for (int s = 0; s < engine::kModSlots; ++s) {
    if (engine::EngineGetRoute(part, s, &r)) {
      if (r.source == src && r.dst.id == dst.id &&
          r.dst.instance == dst.instance)
        return engine::EngineSetRoute(part, s, src, dst, amount);
    } else if (free < 0) {
      free = s;
    }
  }
  if (free < 0) return false;
  return engine::EngineSetRoute(part, free, src, dst, amount);
}

const NavState &InteractionNavState() { return g_nav; }

}  // namespace nostromo
