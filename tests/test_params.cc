#include <cmath>
#include <cstdio>

#include "engine.h"
#include "params.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

int main() {
    Voice v = {};

    Check(ParamCount() == static_cast<int>(ParamId::kCount),
          "ParamCount() == ParamId::kCount");

    for (int i = 0; i < static_cast<int>(ParamId::kCount); ++i) {
        Check(ParamName(static_cast<ParamId>(i))[0] != '\0', "name non-empty");
        Check(g_params[i].def >= 0.0f && g_params[i].def <= 1.0f,
              "default in [0,1]");
    }

    /* clamping */
    ParamSet(&v, ParamId::kCutoff, 9.0f);
    Check(ParamGet(&v, ParamId::kCutoff) == 1.0f, "clamp high to 1");
    ParamSet(&v, ParamId::kCutoff, -9.0f);
    Check(ParamGet(&v, ParamId::kCutoff) == 0.0f, "clamp low to 0");

    /* NaN must not poison a parameter (regression: resonance NaN killed the
     * filter state and silenced the engine until restart). */
    ParamSet(&v, ParamId::kResonance, std::nanf(""));
    Check(ParamGet(&v, ParamId::kResonance) == 0.0f, "NaN clamps to 0");

    /* exponential-from-zero curve: 0 -> 0 s, 1 -> 10 s */
    ParamSet(&v, ParamId::kAttack, 0.0f);
    Check(ParamGetDisp(&v, ParamId::kAttack) == 0.0f, "attack 0 -> 0 s");
    ParamSet(&v, ParamId::kAttack, 1.0f);
    Check(std::fabs(ParamGetDisp(&v, ParamId::kAttack) - 10.0f) < 1e-3f,
          "attack 1 -> 10 s");

    /* display -> normalized round-trip */
    ParamSetDisp(&v, ParamId::kAttack, 0.1f);
    Check(std::fabs(ParamGetDisp(&v, ParamId::kAttack) - 0.1f) < 1e-3f,
          "attack 0.1 s round-trip");

    /* linear curve: sustain 0.6 -> 60 % */
    ParamSet(&v, ParamId::kSustain, 0.6f);
    Check(std::fabs(ParamGetDisp(&v, ParamId::kSustain) - 60.0f) < 1e-3f,
          "sustain norm 0.6 -> 60 %");

    /* cutoff display: norm 1.0 -> 20 kHz */
    ParamSet(&v, ParamId::kCutoff, 1.0f);
    Check(std::fabs(ParamGetDisp(&v, ParamId::kCutoff) - 20000.0f) < 0.5f,
          "cutoff norm 1.0 -> 20000 Hz");

    /* format produces a non-empty string */
    char buf[64];
    ParamFormat(&v, ParamId::kCutoff, buf, sizeof(buf));
    Check(buf[0] != '\0', "ParamFormat non-empty");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: parameter table (%d params)\n",
                static_cast<int>(ParamId::kCount));
    return 0;
}
