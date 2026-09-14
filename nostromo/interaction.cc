// interaction.cc — the interaction layer's stateful components.
//
// Phase 4 holds the GestureRecognizer; phase 5 adds the Dispatcher,
// InteractionOnInput, InteractionCreateRoute and InteractionInit. Everything
// here is control-side (the M33 input task on the target): no drawing, no
// allocation, no invalidation-state writes.

#include "interaction.h"

#include "feel.h"

namespace nostromo {

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

}  // namespace nostromo
