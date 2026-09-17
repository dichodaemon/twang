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
#include "panel_internal.h"
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

// Remove route(s) from `src` to `dst`; kNone src removes every route to `dst`.
// A route is deleted by writing its source back to the kNone sentinel (the
// empty-slot marker the engine's GetRoute treats as unset).
void ClearRoutes(engine::EngineControl &control, int part,
                 engine::ModSourceId src, engine::ParamRef dst) {
  engine::ModRoute r;
  for (int s = 0; s < engine::kModSlots; ++s) {
    if (!control.GetRoute(part, s, &r)) continue;
    if (r.dst.id != dst.id || r.dst.instance != dst.instance) continue;
    if (src != engine::ModSourceId::kNone && r.source != src) continue;
    control.SetRoute(part, s, engine::ModSourceId::kNone, dst, 0.0f);
  }
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
  return id;  // unreachable: kCutoff/kResonance/kAmp/kPitchCoarse/kDrive are modulatable
}

// The next embedded-output mode, wrapping (arch-design §7.3: one global value
// cycled by the OUT tap). kOff -> kScope -> kCycle -> kSpectrum -> kOff.
ScopeMode NextScopeMode(ScopeMode m) {
  switch (m) {
    case ScopeMode::kOff: return ScopeMode::kScope;
    case ScopeMode::kScope: return ScopeMode::kCycle;
    case ScopeMode::kCycle: return ScopeMode::kSpectrum;
    case ScopeMode::kSpectrum: return ScopeMode::kOff;
  }
  return ScopeMode::kOff;  // unreachable
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

// Integer step with clamping to [lo, hi]. detents may be negative.
std::uint32_t StepInt(std::uint32_t v, std::int8_t detents, std::uint32_t lo,
                      std::uint32_t hi, std::uint32_t step) {
  const long next = static_cast<long>(v) + static_cast<long>(detents) *
                                               static_cast<long>(step);
  if (next < static_cast<long>(lo)) return lo;
  if (next > static_cast<long>(hi)) return hi;
  return static_cast<std::uint32_t>(next);
}

// Advance one CONF feel field by `detents` detents (clamped to its edit range).
// Hold-turn refines the one continuous field (long-press ms) to 10 ms steps;
// the rest are already single-unit counts. PATCH controls (kCategory..kAction)
// are inert here until patch storage lands (twang-9d7z).
void TurnFeel(ViewCtl ctl, std::int8_t detents, bool fine, FeelProfile &feel) {
  switch (ctl) {
    case ViewCtl::kDetents:
      feel.detents_per_rev = static_cast<std::uint8_t>(
          StepInt(feel.detents_per_rev, detents, 1, 64, 1));
      break;
    case ViewCtl::kAccelMax:
      feel.accel_max_default = static_cast<std::uint8_t>(
          StepInt(feel.accel_max_default, detents, 1, 16, 1));
      break;
    case ViewCtl::kAccelThresh:
      feel.accel_threshold_dps = static_cast<std::uint16_t>(
          StepInt(feel.accel_threshold_dps, detents, 1, 64, 1));
      break;
    case ViewCtl::kLongPress:
      feel.long_press_ms =
          StepInt(feel.long_press_ms, detents, 100, 2000, fine ? 10 : 50);
      break;
    case ViewCtl::kFineDiv:
      feel.fine_divisor = static_cast<std::uint8_t>(
          StepInt(feel.fine_divisor, detents, 1, 64, 1));
      break;
    case ViewCtl::kScopeRefresh:
      feel.scope_interval_ms = StepInt(feel.scope_interval_ms, detents, 16, 250, 1);
      break;
    default:
      break;  // PATCH controls are inert until patch storage
  }
}

// Revert one CONF feel field to its default (a no-op for PATCH controls).
void RevertFeel(ViewCtl ctl, FeelProfile &feel) {
  const FeelProfile def = DefaultFeel();
  switch (ctl) {
    case ViewCtl::kDetents: feel.detents_per_rev = def.detents_per_rev; break;
    case ViewCtl::kAccelMax:
      feel.accel_max_default = def.accel_max_default;
      break;
    case ViewCtl::kAccelThresh:
      feel.accel_threshold_dps = def.accel_threshold_dps;
      break;
    case ViewCtl::kLongPress: feel.long_press_ms = def.long_press_ms; break;
    case ViewCtl::kFineDiv: feel.fine_divisor = def.fine_divisor; break;
    case ViewCtl::kScopeRefresh:
      feel.scope_interval_ms = def.scope_interval_ms;
      break;
    default: break;  // PATCH controls are inert until patch storage
  }
}

// Advance one kOutView output setting by `detents` detents (clamped).
// TIMEBASE steps the 1-2-5 window sequence {20, 50, 100, 200, 341} ms; CYCLES
// steps 1..8. The remaining kOutView columns are kPending and never dispatch
// here. `fine` refines nothing — both fields are coarse counts.
void TurnOut(ViewCtl ctl, std::int8_t detents, bool fine, OutputSettings &out) {
  (void)fine;
  switch (ctl) {
    case ViewCtl::kTimebase: {
      static constexpr std::uint32_t kSteps[] = {20, 50, 100, 200, 341};
      int idx = 4;  // the default (341 ms); an unknown value lands here
      for (int i = 0; i < 5; ++i)
        if (out.timebase_ms == kSteps[i]) { idx = i; break; }
      idx += static_cast<int>(detents);
      if (idx < 0) idx = 0;
      if (idx > 4) idx = 4;
      out.timebase_ms = kSteps[idx];
      break;
    }
    case ViewCtl::kCycles:
      out.cycles = static_cast<std::uint8_t>(
          StepInt(out.cycles, detents, 1, 8, 1));
      break;
    default: break;  // other kOutView columns are kPending; inert
  }
}

// Revert one kOutView output setting to its default.
void RevertOut(ViewCtl ctl, OutputSettings &out) {
  const OutputSettings def = DefaultOut();
  switch (ctl) {
    case ViewCtl::kTimebase: out.timebase_ms = def.timebase_ms; break;
    case ViewCtl::kCycles: out.cycles = def.cycles; break;
    default: break;
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
        const engine::ParamDesc &desc =
            engine::k_params[static_cast<std::size_t>(b.param.id)];
        if (nav.mode == ViewMode::kModView && nav.focus_col == b.column &&
            desc.modulatable) {
          // MOD view, focused column: long-press clears every route into it.
          ClearRoutes(*control, nav.part, engine::ModSourceId::kNone, b.param);
        } else {
          // Revert to the default exactly once, no intermediate write.
          control->SetParam(nav.part, b.param, desc.def);
        }
        MarkPage();
      } else if (g == Gesture::kPressShort && nav.mode == ViewMode::kModView) {
        // MOD view: a short press focuses the column — a row cursor over its
        // route list (arch-design §5). Pressing the focused column releases.
        nav.focus_col = (nav.focus_col == b.column) ? -1 : b.column;
        MarkPage();
      }
      // kPressShort outside MOD view: nothing to descend into.
      break;
    }
    case BindKind::kRouteAmount: {
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        // A non-modulatable destination is inert in arm mode (the overlay
        // shows "--"); skip it so SetRoute never rejects and the write is
        // never mislabelled "route full".
        const engine::ParamDesc &desc =
            engine::k_params[static_cast<std::size_t>(b.param.id)];
        if (!desc.modulatable) break;
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
        // CreateRoute returns false only when the table is full and no route
        // matches: the write is dropped, so raise the route-full alert.
        nav.route_full = !CreateRoute(nav.part, nav.armed_source, b.param, amt);
        arm_used = true;
        MarkPage();
      } else if (g == Gesture::kPressLong) {
        // Long-press on a destination clears the armed source's route to it.
        ClearRoutes(*control, nav.part, nav.armed_source, b.param);
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
        nav.route_full = false;  // fresh arming clears the alert
        mod_from_view = (nav.mode == ViewMode::kModView);
        nav.mode = ViewMode::kModArm;  // momentary
        MarkAll();
      } else if (ev.edge == Edge::kUp) {
        nav.route_full = false;  // leaving arm mode clears the alert
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
        // Cycle the embedded output view. Global, so every plot page's render
        // changes even though only the active slots repaint.
        nav.scope_mode = NextScopeMode(nav.scope_mode);
        MarkAll();
      } else if (g == Gesture::kPressLong) {
        // Toggle the full-screen output view. Entering from kOff forces kScope
        // so the first tap after entry visibly advances (Design Decision 2).
        if (nav.mode == ViewMode::kOutView) {
          nav.mode = ViewMode::kEdit;
        } else {
          nav.mode = ViewMode::kOutView;
          if (nav.scope_mode == ScopeMode::kOff)
            nav.scope_mode = ScopeMode::kScope;
        }
        MarkAll();
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
      } else if (g == Gesture::kPressLong) {
        // Long-press clears the slot (kSource/kDest) or reverts the amount
        // (arch-design §7.7: "clear the slot, else revert").
        const int slot = b.slot;
        if (slot < 0 || slot >= engine::kModSlots) break;
        if (b.field == RouteField::kSource || b.field == RouteField::kDest) {
          control->SetRoute(nav.part, slot, engine::ModSourceId::kNone,
                            engine::ParamRef{0, engine::ParamId::kCutoff},
                            0.0f);
        } else {
          engine::ModRoute r;
          if (control->GetRoute(nav.part, slot, &r))
            control->SetRoute(nav.part, slot, r.source, r.dst, 0.0f);
        }
        MarkPage();
      }
      break;
    }
    case BindKind::kViewCtl: {
      // kTimebase/kCycles edit the output settings (and repaint the output
      // plot); the CONF controls edit the runtime feel; PATCH controls are
      // inert until patch storage lands (twang-9d7z).
      const bool out_ctl = b.ctl == ViewCtl::kTimebase ||
                           b.ctl == ViewCtl::kCycles;
      if (g == Gesture::kTurn || g == Gesture::kHoldTurn) {
        if (out_ctl) {
          TurnOut(b.ctl, ev.detents, g == Gesture::kHoldTurn, out);
          MarkPlot(kSlotOut);
        } else {
          TurnFeel(b.ctl, ev.detents, g == Gesture::kHoldTurn, feel);
          MarkPage();
        }
      } else if (g == Gesture::kPressLong) {
        // Revert the control to its default exactly once.
        if (out_ctl) {
          RevertOut(b.ctl, out);
          MarkPlot(kSlotOut);
        } else {
          RevertFeel(b.ctl, feel);
          MarkPage();
        }
      }
      // kPressShort: nothing to descend into (kAction executes once PATCH
      // dispatch lands).
      break;
    }
    case BindKind::kPending:
    case BindKind::kNone:
    default:
      break;  // pending is inert
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
  nav.subject = SubjectId::kFilt;
  nav.group = 0;
  nav.focus_col = -1;
  nav.mode = ViewMode::kEdit;
  nav.scope_mode = ScopeMode::kScope;  // power-on: filter curve + embedded scope
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
