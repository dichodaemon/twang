#include <cmath>
#include <cstdio>

#include "engine.h"
#include "params.h"

using namespace engine;

static int g_failures = 0;

static void check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

int main() {
    engine_init();
    Voice *v = engine_voice();

    check(param_count() == static_cast<int>(ParamId::kCount),
          "param_count == ParamId::kCount");

    for (int i = 0; i < static_cast<int>(ParamId::kCount); ++i) {
        check(param_name(static_cast<ParamId>(i))[0] != '\0', "name non-empty");
        check(g_params[i].def >= 0.0f && g_params[i].def <= 1.0f,
              "default in [0,1]");
    }

    /* clamping */
    param_set(v, ParamId::kCutoff, 9.0f);
    check(param_get(v, ParamId::kCutoff) == 1.0f, "clamp high to 1");
    param_set(v, ParamId::kCutoff, -9.0f);
    check(param_get(v, ParamId::kCutoff) == 0.0f, "clamp low to 0");

    /* exponential curve: attack 0.5 -> 100 ms */
    param_set(v, ParamId::kAttack, 0.5f);
    check(std::fabs(param_get_disp(v, ParamId::kAttack) - 0.1f) < 1e-3f,
          "attack norm 0.5 -> 0.1 s");

    /* display -> normalized round-trip */
    param_set_disp(v, ParamId::kAttack, 0.1f);
    check(std::fabs(param_get(v, ParamId::kAttack) - 0.5f) < 1e-3f,
          "attack 0.1 s -> norm 0.5");

    /* linear curve: sustain 0.6 -> 60 % */
    param_set(v, ParamId::kSustain, 0.6f);
    check(std::fabs(param_get_disp(v, ParamId::kSustain) - 60.0f) < 1e-3f,
          "sustain norm 0.6 -> 60 %");

    /* cutoff display: norm 1.0 -> 20 kHz */
    param_set(v, ParamId::kCutoff, 1.0f);
    check(std::fabs(param_get_disp(v, ParamId::kCutoff) - 20000.0f) < 0.5f,
          "cutoff norm 1.0 -> 20000 Hz");

    /* format produces a non-empty string */
    char buf[64];
    param_format(v, ParamId::kCutoff, buf, sizeof(buf));
    check(buf[0] != '\0', "param_format non-empty");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: parameter table (%d params)\n",
                static_cast<int>(ParamId::kCount));
    return 0;
}
