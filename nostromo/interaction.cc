// interaction.cc — the interaction layer's stateful components.
//
// Sits between the input driver and the engine (arch-design §4): resolves what
// each physical control drives, applies gestures to that binding, and marks the
// affected slots dirty through the panel's MarkDirty — the sole invalidation
// entry point. Control-side only (the M33 input task on the target): no
// drawing, no allocation, no direct invalidation-state writes.

#include "interaction.h"

#include "engine.h"
#include "engine_control.h"
#include "feel.h"
#include "pages.h"
#include "panel.h"
#include "params.h"
#include "surface.h"

namespace nostromo {

namespace {

const PageDesc &PageOf(SubjectId s) { return k_pages[static_cast<int>(s)]; }

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Signed normalized delta for a parameter turn.
float ParamDelta(const engine::ParamDesc &desc, std::int8_t detents,
                 float rate_dps, bool fine, const FeelProfile &feel) {
  float step = kDetentStep;
  if (rate_dps > static_cast<float>(feel.accel_threshold_dps))
    step *= static_cast<float>(desc.accel_max);
  if (fine) step /= static_cast<float>(feel.fine_divisor);
  return static_cast<float>(detents) * step;
}

// Current amount of the (src, dst) route, or 0 if none.
float RouteAmount(engine::EngineControl &control, int part,
                  engine::ModSourceId src, engine::ParamRef dst) {
  engine::ModRoute r;
  for (int s = 0; s < engine::kModSlots; ++s)
    if (control.GetRoute(part, s, &r) && r.source == src &&
        r.dst.id == dst.id && r.dst.instance == dst.instance)
      return r.amount;
  return 0.0f;
}

// The next modulatable ParamId after `id` in enum order, wrapping. Modulatable
// membership is a descriptor flag (k_params[].modulatable), not an enum split,
// so cycling reads the table — a param promoted to modulatable joins the cycle
// without a code change.
engine::ParamId NextModulatable(engine::ParamId id, int dir) {
  const int count = static_cast<int>(engine::ParamId::kCount);
  for (int step = 1; step <= count; ++step) {
    int idx = (static_cast<int>(id) + dir * step) % count;
    if (idx < 0) idx += count;
    const auto pid = static_cast<engine::ParamId>(idx);
    if (engine::k_params[static_cast<std::size_t>(pid)].modulatable)
      return pid;
  }
  return id;  // unreachable: kCutoff/kAmp/kPitchCoarse/kDrive are modulatable
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

}  // namespace

// ---- private methods ----

void Interaction::MarkPlot(SlotIdx idx) {
  if (panel) MarkDirty(panel, idx);
}

void Interaction::MarkPage() {
  const std::int8_t slot = PageOf(nav.subject).dyn_slot;
  if (slot >= 0) MarkPlot(static_cast<SlotIdx>(slot));
}

void Interaction::MarkAll() {
  MarkPlot(kSlotOsc);
  MarkPlot(kSlotFilter);
  MarkPlot(kSlotEnv);
  MarkPlot(kSlotOut);
}

float Interaction::TurnRate(Control c, std::int8_t detents,
                            std::uint32_t t_ms) {
  const int ci = static_cast<int>(c);
  const std::uint32_t dt = t_ms - last_turn_ms[ci];
  last_turn_ms[ci] = t_ms;
  if (dt == 0) return 1000.0f;  // first turn or same-ms burst: treat as fast
  const int mag = detents < 0 ? -static_cast<int>(detents) : detents;
  return static_cast<float>(mag) * 1000.0f / static_cast<float>(dt);
}

// Applies one gesture/binding — the only side-effecting component.
void Interaction::Dispatcher(const InputEvent &ev, Gesture g,
                             const Binding &b) {
  switch (b.kind) {
    case BindKind::kParam: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        const engine::ParamDesc &desc =
            engine::k_params[static_cast<std::size_t>(b.param.id)];
        const float rate = TurnRate(ev.control, ev.detents, ev.t_ms);
        const float delta =
            ParamDelta(desc, ev.detents, rate, g == Gesture::kHoldTurn,
                       feel);
        const float cur = control->GetParam(nav.part, b.param);
        float next = Clamp01(cur + delta);
        // zero_notch: a bipolar parameter needs an extra detent to leave the
        // center (0.5), so a single detent across it lands exactly on it.
        if (desc.zero_notch &&
            ((cur < 0.5f && next >= 0.5f) || (cur > 0.5f && next <= 0.5f)))
          next = 0.5f;
        control->SetParam(nav.part, b.param, next);
        MarkPage();
      } else if (g == Gesture::kPressLong) {
        // Revert to the default exactly once, no intermediate write.
        const engine::ParamDesc &desc =
            engine::k_params[static_cast<std::size_t>(b.param.id)];
        control->SetParam(nav.part, b.param, desc.def);
        MarkPage();
      }
      // kPressShort on a continuous parameter: nothing to descend into.
      break;
    }
    case BindKind::kRouteAmount: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        const float rate = TurnRate(ev.control, ev.detents, ev.t_ms);
        float step = kRouteAmountStep;
        if (rate > static_cast<float>(feel.accel_threshold_dps))
          step *= kRouteAmountAccel;
        if (g == Gesture::kHoldTurn)
          step /= static_cast<float>(feel.fine_divisor);
        const float old =
            RouteAmount(*control, nav.part, nav.armed_source, b.param);
        float amt = old + static_cast<float>(ev.detents) * step;
        if (amt < -1.0f) amt = -1.0f;
        if (amt > 1.0f) amt = 1.0f;
        CreateRoute(nav.part, nav.armed_source, b.param, amt);
        arm_used = true;
        MarkPage();
      }
      break;
    }
    case BindKind::kNavSubject: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        int s = static_cast<int>(nav.subject) + (ev.detents > 0 ? 1 : -1);
        if (s < 0) s = static_cast<int>(SubjectId::kCount) - 1;
        if (s >= static_cast<int>(SubjectId::kCount)) s = 0;
        nav.subject = static_cast<SubjectId>(s);
        nav.group = 0;  // each subject starts at its hot set
        MarkPage();
      }
      break;
    }
    case BindKind::kNavItem: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        const int dir = ev.detents > 0 ? 1 : -1;
        if (nav.mode == ViewMode::kModArm) {
          // MOD held: NAV2 walks the source list (kNone skipped).
          int src = static_cast<int>(nav.armed_source) + dir;
          if (src <= static_cast<int>(engine::ModSourceId::kNone))
            src = kModSourceCount - 1;
          if (src >= kModSourceCount)
            src = static_cast<int>(engine::ModSourceId::kVelocity);
          nav.armed_source = static_cast<engine::ModSourceId>(src);
        } else {
          const int max = ItemCount(PageOf(nav.subject).item_axis);
          if (max > 0) {
            int cur = static_cast<int>(
                          nav.item[static_cast<int>(nav.subject)]) +
                      dir;
            if (cur < 0) cur = max - 1;
            if (cur >= max) cur = 0;
            nav.item[static_cast<int>(nav.subject)] =
                static_cast<std::uint8_t>(cur);
          }
        }
        MarkPage();
      }
      break;
    }
    case BindKind::kPartSelect: {
      if (g == Gesture::kPressShort) {
        nav.part = static_cast<std::uint8_t>(
            static_cast<int>(ev.control) - static_cast<int>(Control::kPart0));
        MarkPage();  // values only; subject/group/item/mode invariant
      }
      break;
    }
    case BindKind::kModeToggle: {
      if (ev.edge == Edge::kDown) {
        arm_used = false;
        mod_from_view = (nav.mode == ViewMode::kModView);
        nav.mode = ViewMode::kModArm;  // momentary
        MarkAll();
      } else if (ev.edge == Edge::kUp) {
        if (arm_used) {
          nav.mode = ViewMode::kEdit;  // a route was armed: plain return
          arm_used = false;
        } else if (g == Gesture::kPressShort) {
          nav.mode =
              mod_from_view ? ViewMode::kEdit : ViewMode::kModView;
        } else {
          nav.mode = ViewMode::kEdit;  // held past the threshold
        }
        MarkAll();
      }
      break;
    }
    case BindKind::kGroupCycle: {
      if (g == Gesture::kPressShort) {
        const int gc = GroupCount<>(PageOf(nav.subject));
        if (gc > 0) nav.group = (nav.group + 1) % gc;
        MarkPage();
      }
      break;
    }
    case BindKind::kOutToggle: {
      if (g == Gesture::kPressShort) {
        if (IsOut(nav.subject)) {
          nav.subject = nav.prev.subject;
          nav.group = nav.prev.group;
          nav.focus_col = nav.prev.focus_col;
        } else {
          nav.prev =
              NavPos{nav.subject, nav.group, nav.focus_col};
          nav.subject = SubjectId::kOutScope;
          nav.group = 0;
          nav.focus_col = -1;
        }
        MarkPage();
      }
      break;
    }
    case BindKind::kRouteField: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        const int slot = b.slot;
        if (slot < 0 || slot >= engine::kModSlots) break;
        engine::ModRoute r{};
        if (!control->GetRoute(nav.part, slot, &r)) {
          // Empty slot: seed a fresh route (kVelocity -> kCutoff, amount 0),
          // then advance the turned field from the seed.
          r.source = engine::ModSourceId::kVelocity;
          r.dst = engine::ParamRef{0, engine::ParamId::kCutoff};
          r.amount = 0.0f;
        }
        const int dir = ev.detents > 0 ? 1 : -1;
        switch (b.field) {
          case RouteField::kSource: {
            int src = static_cast<int>(r.source) + dir;
            if (src <= static_cast<int>(engine::ModSourceId::kNone))
              src = kModSourceCount - 1;
            if (src >= kModSourceCount)
              src = static_cast<int>(engine::ModSourceId::kVelocity);
            r.source = static_cast<engine::ModSourceId>(src);
            break;
          }
          case RouteField::kDest:
            r.dst.id = NextModulatable(r.dst.id, dir);
            break;
          case RouteField::kAmount: {
            const float rate = TurnRate(ev.control, ev.detents, ev.t_ms);
            float step = kRouteAmountStep;
            if (rate > static_cast<float>(feel.accel_threshold_dps))
              step *= kRouteAmountAccel;
            if (g == Gesture::kHoldTurn)
              step /= static_cast<float>(feel.fine_divisor);
            float amt = r.amount + static_cast<float>(ev.detents) * step;
            if (amt < -1.0f) amt = -1.0f;
            if (amt > 1.0f) amt = 1.0f;
            r.amount = amt;
            break;
          }
        }
        control->SetRoute(nav.part, slot, r.source, r.dst, r.amount);
        MarkPage();
      }
      break;
    }
    case BindKind::kPending:
    case BindKind::kNone:
    case BindKind::kViewCtl:
    default:
      break;  // pending is inert; view-control dispatch is later
  }
}

Gesture Recognize(const InputEvent &ev, PressState &st,
                  const FeelProfile &feel) {
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
    return (elapsed < feel.long_press_ms) ? Gesture::kPressShort
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

void Interaction::Init(Panel *panel, const SurfaceProfile &surface,
                       engine::EngineControl *control) {
  this->panel = panel;
  this->surface = &surface;
  this->control = control;
  PanelSetInteraction(panel, this);  // the panel reads navigation through us
  PanelSetEngine(panel, control);    // the panel drives notes/params through us
  // feel keeps its DefaultFeel() defaults (persisted-settings load deferred).
  nav = NavState{};
  nav.part = 0;
  nav.subject = SubjectId::kOutScope;
  nav.prev = NavPos{SubjectId::kFilt, 0, -1};
  nav.group = 0;
  nav.focus_col = -1;
  nav.mode = ViewMode::kEdit;
  for (auto &st : press) st = PressState{};
  for (auto &t : last_turn_ms) t = 0;
  arm_used = false;
  MarkAll();
  // A shortfall below kColumns is reported once here; the surplus (an encoder
  // bank wider than the column count) is simply unmapped in the profile.
  (void)surface.n_encoders;
}

void Interaction::OnInput(const InputEvent &ev) {
  if (static_cast<int>(ev.control) >= static_cast<int>(Control::kCount)) return;
  Gesture g = Recognize(ev, press[static_cast<int>(ev.control)], feel);
  const Binding b = ResolveBinding(nav, ev.control);
  // MOD is modal: its down edge arms even though the recognizer emits nothing
  // for a press start.
  if (g == Gesture::kNone &&
      !(ev.edge == Edge::kDown && b.kind == BindKind::kModeToggle))
    return;
  Dispatcher(ev, g, b);
}

bool Interaction::CreateRoute(std::uint8_t part, engine::ModSourceId src,
                              engine::ParamRef dst, float amount) {
  int free = -1;
  engine::ModRoute r;
  for (int s = 0; s < engine::kModSlots; ++s) {
    if (control->GetRoute(part, s, &r)) {
      if (r.source == src && r.dst.id == dst.id &&
          r.dst.instance == dst.instance)
        return control->SetRoute(part, s, src, dst, amount);
    } else if (free < 0) {
      free = s;
    }
  }
  if (free < 0) return false;
  return control->SetRoute(part, free, src, dst, amount);
}

}  // namespace nostromo
