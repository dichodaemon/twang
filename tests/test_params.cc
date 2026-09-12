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
    Part p = {};

    Check(ParamCount() == static_cast<int>(ParamId::kCount),
          "ParamCount() == ParamId::kCount");

    for (int i = 0; i < static_cast<int>(ParamId::kCount); ++i) {
        Check(ParamName(static_cast<ParamId>(i))[0] != '\0', "name non-empty");
        Check(g_params[i].def >= 0.0f && g_params[i].def <= 1.0f,
              "default in [0,1]");
    }

    /* clamping */
    ParamSet(&p, ParamId::kCutoff, 9.0f);
    Check(ParamGet(&p, ParamId::kCutoff) == 1.0f, "clamp high to 1");
    ParamSet(&p, ParamId::kCutoff, -9.0f);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.0f, "clamp low to 0");

    /* NaN must not poison a parameter (regression: resonance NaN killed the
     * filter state and silenced the engine until restart). */
    ParamSet(&p, ParamId::kResonance, std::nanf(""));
    Check(ParamGet(&p, ParamId::kResonance) == 0.0f, "NaN clamps to 0");

    /* exponential-from-zero curve: 0 -> 0 s, 1 -> 10 s */
    ParamSet(&p, ParamId::kAttack, 0.0f);
    Check(ParamGetDisp(&p, ParamId::kAttack) == 0.0f, "attack 0 -> 0 s");
    ParamSet(&p, ParamId::kAttack, 1.0f);
    Check(std::fabs(ParamGetDisp(&p, ParamId::kAttack) - 10.0f) < 1e-3f,
          "attack 1 -> 10 s");

    /* display -> normalized round-trip */
    ParamSetDisp(&p, ParamId::kAttack, 0.1f);
    Check(std::fabs(ParamGetDisp(&p, ParamId::kAttack) - 0.1f) < 1e-3f,
          "attack 0.1 s round-trip");

    /* linear curve: sustain 0.6 -> 60 % */
    ParamSet(&p, ParamId::kSustain, 0.6f);
    Check(std::fabs(ParamGetDisp(&p, ParamId::kSustain) - 60.0f) < 1e-3f,
          "sustain norm 0.6 -> 60 %");

    /* cutoff display: norm 1.0 -> 20 kHz */
    ParamSet(&p, ParamId::kCutoff, 1.0f);
    Check(std::fabs(ParamGetDisp(&p, ParamId::kCutoff) - 20000.0f) < 0.5f,
          "cutoff norm 1.0 -> 20000 Hz");

    /* format produces a non-empty string */
    char buf[64];
    ParamFormat(&p, ParamId::kCutoff, buf, sizeof(buf));
    Check(buf[0] != '\0', "ParamFormat non-empty");

    /* phase-1 matrix params: combination class + default */
    Check(g_params[static_cast<std::size_t>(ParamId::kAmp)].comb ==
              CombinationClass::kMultiplicative,
          "amp class multiplicative");
    Check(g_params[static_cast<std::size_t>(ParamId::kAmp)].def == 1.0f,
          "amp def 1.0 (pure level; headroom on the bus)");
    Check(g_params[static_cast<std::size_t>(ParamId::kDrive)].comb ==
              CombinationClass::kAdditive,
          "drive class additive");
    Check(g_params[static_cast<std::size_t>(ParamId::kDrive)].def == 0.0f,
          "drive def 0");
    Check(g_params[static_cast<std::size_t>(ParamId::kDrive)].offset ==
              offsetof(Part, params) + 9 * sizeof(float),
          "drive offset == params index 9");
    Check(g_params[static_cast<std::size_t>(ParamId::kPitchCoarse)].comb ==
              CombinationClass::kExponential,
          "pitch_coarse class exponential");
    Check(g_params[static_cast<std::size_t>(ParamId::kPitchCoarse)].def == 0.5f,
          "pitch_coarse def 0.5");
    Check(g_params[static_cast<std::size_t>(ParamId::kPitchBend)].comb ==
              CombinationClass::kAdditive,
          "pitchbend class additive");
    Check(g_params[static_cast<std::size_t>(ParamId::kPitchBend)].def == 0.5f,
          "pitchbend def 0.5");
    Check(g_params[static_cast<std::size_t>(ParamId::kKeyFollowDepth)].comb ==
              CombinationClass::kAdditive,
          "key_follow class additive");
    Check(g_params[static_cast<std::size_t>(ParamId::kKeyFollowDepth)].def ==
              0.5f,
          "key_follow def 0.5");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: parameter table (%d params)\n",
                static_cast<int>(ParamId::kCount));
    return 0;
}
