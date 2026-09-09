#include <cmath>
#include <cstdio>

#include "engine.h"
#include "ipc.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

int main() {
    ParamBlock block;
    Part p = {};

    block.Reset(g_params);
    block.Commit(&p);
    Check(std::fabs(ParamGet(&p, ParamId::kCutoff) -
                    g_params[static_cast<std::size_t>(ParamId::kCutoff)].def) <
              1e-6f,
          "default cutoff committed");

    block.Set(0, ParamId::kCutoff, 0.5f);
    block.Commit(&p);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.5f, "cutoff set then committed");

    // Multiple params updated in sequence stay consistent.
    block.Set(0, ParamId::kCutoff, 0.25f);
    block.Set(0, ParamId::kResonance, 0.75f);
    block.Commit(&p);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.25f, "cutoff updated");
    Check(ParamGet(&p, ParamId::kResonance) == 0.75f, "resonance updated");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: param block\n");
    return 0;
}
