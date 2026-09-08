#include <math.h>
#include <stdio.h>

#include "engine.h"
#include "params.h"

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(void) {
    engine_init();
    Voice *v = engine_voice();

    CHECK(param_count() == PARAM_COUNT, "param_count == PARAM_COUNT");

    for (int i = 0; i < PARAM_COUNT; ++i) {
        CHECK(param_name((ParamId)i)[0] != '\0', "name non-empty");
        CHECK(g_params[i].def >= 0.0f && g_params[i].def <= 1.0f,
              "default in [0,1]");
    }

    /* clamping */
    param_set(v, PARAM_CUTOFF, 9.0f);
    CHECK(param_get(v, PARAM_CUTOFF) == 1.0f, "clamp high to 1");
    param_set(v, PARAM_CUTOFF, -9.0f);
    CHECK(param_get(v, PARAM_CUTOFF) == 0.0f, "clamp low to 0");

    /* exponential curve: attack 0.5 -> 100 ms */
    param_set(v, PARAM_ATTACK, 0.5f);
    CHECK(fabsf(param_get_disp(v, PARAM_ATTACK) - 0.1f) < 1e-3f,
          "attack norm 0.5 -> 0.1 s");

    /* display -> normalized round-trip */
    param_set_disp(v, PARAM_ATTACK, 0.1f);
    CHECK(fabsf(param_get(v, PARAM_ATTACK) - 0.5f) < 1e-3f,
          "attack 0.1 s -> norm 0.5");

    /* linear curve: sustain 0.6 -> 60 % */
    param_set(v, PARAM_SUSTAIN, 0.6f);
    CHECK(fabsf(param_get_disp(v, PARAM_SUSTAIN) - 60.0f) < 1e-3f,
          "sustain norm 0.6 -> 60 %");

    /* cutoff display: norm 1.0 -> 20 kHz */
    param_set(v, PARAM_CUTOFF, 1.0f);
    CHECK(fabsf(param_get_disp(v, PARAM_CUTOFF) - 20000.0f) < 0.5f,
          "cutoff norm 1.0 -> 20000 Hz");

    /* format produces a non-empty string */
    char buf[64];
    param_format(v, PARAM_CUTOFF, buf, sizeof(buf));
    CHECK(buf[0] != '\0', "param_format non-empty");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("PASS: parameter table (%d params)\n", PARAM_COUNT);
    return 0;
}
