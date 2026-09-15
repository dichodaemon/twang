// test_gestures.cc — gesture-recognition disambiguation boundaries.
//
// The cases that matter are the press/turn boundaries (arch-design §10):
// press with no detent under/over the long-press threshold, a detent arriving
// during a press (absorbed), and a detent after release. Pure and table-driven.

#include <cstdio>

#include "feel.h"
#include "interaction.h"

using namespace nostromo;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static InputEvent Down(Control c, std::uint32_t t) {
    return InputEvent{c, 0, Edge::kDown, t};
}
static InputEvent Up(Control c, std::uint32_t t) {
    return InputEvent{c, 0, Edge::kUp, t};
}
static InputEvent Turn(Control c, std::int8_t d, std::uint32_t t) {
    return InputEvent{c, d, Edge::kNone, t};
}

int main() {
    FeelProfile feel = DefaultFeel();
    feel.long_press_ms = 500;  // explicit threshold for the boundary cases

    // Press, no detent, release under the threshold → short press.
    {
        PressState st;
        Check(Recognize(Down(Control::kMod, 0), st, feel) == Gesture::kNone,
              "down emits nothing");
        Check(Recognize(Up(Control::kMod, 300), st, feel) == Gesture::kPressShort,
              "under-threshold release is a short press");
    }

    // Press, no detent, release over the threshold → long press.
    {
        PressState st;
        Recognize(Down(Control::kMod, 0), st, feel);
        Check(Recognize(Up(Control::kMod, 600), st, feel) == Gesture::kPressLong,
              "over-threshold release is a long press");
    }

    // Detent arriving during a press → hold-turn, release absorbed.
    {
        PressState st;
        Recognize(Down(Control::kEnc0, 0), st, feel);
        Check(Recognize(Turn(Control::kEnc0, 1, 10), st, feel) == Gesture::kHoldTurn,
              "detent at 10ms is a hold-turn");
        Check(Recognize(Up(Control::kEnc0, 300), st, feel) == Gesture::kNone,
              "release after a detent is absorbed");
    }

    // Detent just before release → still hold-turn, still absorbed.
    {
        PressState st;
        Recognize(Down(Enc(1), 0), st, feel);
        Check(Recognize(Turn(Enc(1), -1, 490), st, feel) == Gesture::kHoldTurn,
              "detent at 490ms is a hold-turn");
        Check(Recognize(Up(Enc(1), 500), st, feel) == Gesture::kNone,
              "release after a late detent is absorbed");
    }

    // Detent after release → a plain turn, no press state involved.
    {
        PressState st;
        Recognize(Down(Control::kEnc0, 0), st, feel);
        Recognize(Up(Control::kEnc0, 300), st, feel);
        Check(Recognize(Turn(Control::kEnc0, 1, 400), st, feel) == Gesture::kTurn,
              "detent after release is a turn");
    }

    // Release without a press → nothing.
    {
        PressState st;
        Check(Recognize(Up(Control::kMod, 100), st, feel) == Gesture::kNone,
              "release without a press emits nothing");
    }

    // The threshold is exclusive: exactly long_press_ms is long, one ms under
    // is short.
    {
        PressState st;
        Recognize(Down(Control::kMod, 0), st, feel);
        Check(Recognize(Up(Control::kMod, 500), st, feel) == Gesture::kPressLong,
              "exactly 500ms is a long press");
    }
    {
        PressState st;
        Recognize(Down(Control::kMod, 0), st, feel);
        Check(Recognize(Up(Control::kMod, 499), st, feel) == Gesture::kPressShort,
              "499ms is a short press");
    }

    // A turn with no press emits kTurn and never sets the detent flag.
    {
        PressState st;
        Check(Recognize(Turn(Control::kEnc0, 3, 0), st, feel) == Gesture::kTurn,
              "unpressed turn is a turn");
        Check(!st.pressed && !st.detent, "turn leaves press state clean");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: gestures\n");
    return 0;
}
