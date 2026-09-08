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
    Voice v = {};

    block.Reset(g_params);
    block.Commit(&v);
    Check(std::fabs(ParamGet(&v, ParamId::kCutoff) -
                    g_params[static_cast<std::size_t>(ParamId::kCutoff)].def) <
              1e-6f,
          "default cutoff committed");

    block.Set(ParamId::kCutoff, 0.5f);
    block.Commit(&v);
    Check(ParamGet(&v, ParamId::kCutoff) == 0.5f, "cutoff set then committed");

    // Multiple params updated in sequence stay consistent.
    block.Set(ParamId::kCutoff, 0.25f);
    block.Set(ParamId::kResonance, 0.75f);
    block.Commit(&v);
    Check(ParamGet(&v, ParamId::kCutoff) == 0.25f, "cutoff updated");
    Check(ParamGet(&v, ParamId::kResonance) == 0.75f, "resonance updated");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: param block\n");
    return 0;
}
