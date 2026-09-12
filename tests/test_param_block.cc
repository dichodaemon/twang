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
    Part parts[kNumParts] = {};
    Part &p = parts[0];  // Commit writes all kNumParts; read the part under test

    block.Reset(g_params);
    block.Commit(parts);
    Check(std::fabs(ParamGet(&p, ParamId::kCutoff) -
                    g_params[static_cast<std::size_t>(ParamId::kCutoff)].def) <
              1e-6f,
          "default cutoff committed");

    block.Set(0, ParamId::kCutoff, 0.5f);
    block.Flush();
    block.Commit(parts);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.5f, "cutoff set then committed");

    // Multiple params updated in sequence stay consistent.
    block.Set(0, ParamId::kCutoff, 0.25f);
    block.Set(0, ParamId::kResonance, 0.75f);
    block.Flush();
    block.Commit(parts);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.25f, "cutoff updated");
    Check(ParamGet(&p, ParamId::kResonance) == 0.75f, "resonance updated");

    // Route round-trip: SetRoute then Flush+Commit carries the route into the Part.
    block.SetRoute(0, 2, ModSourceId::kEnv1, ParamId::kCutoff, 0.5f);
    block.Flush();
    block.Commit(parts);
    Check(p.routes[2].source == ModSourceId::kEnv1 &&
              p.routes[2].destination == ParamId::kCutoff &&
              p.routes[2].amount == 0.5f,
          "route round-trip through Commit");

    // Deferred publish: Set writes pending_ but does not publish, so Commit
    // still observes the previous snapshot (no half-applied state).
    block.Set(0, ParamId::kCutoff, 0.9f);
    block.Commit(parts);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.25f, "Set defers publish (cutoff)");
    Check(ParamGet(&p, ParamId::kResonance) == 0.75f, "Set defers publish (resonance)");

    // Atomic multi-field: two params + a route clear land as ONE snapshot.
    block.Set(0, ParamId::kCutoff, 0.1f);
    block.Set(0, ParamId::kResonance, 0.2f);
    block.SetRoute(0, 2, ModSourceId::kNone, ParamId::kCutoff, 0.0f);
    block.Flush();
    block.Commit(parts);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.1f, "atomic flush: cutoff");
    Check(ParamGet(&p, ParamId::kResonance) == 0.2f, "atomic flush: resonance");
    Check(p.routes[2].source == ModSourceId::kNone, "atomic flush: route cleared");

    // Idempotent flush: a second Flush with nothing pending publishes nothing.
    block.Flush();
    block.Commit(parts);
    Check(ParamGet(&p, ParamId::kCutoff) == 0.1f &&
              ParamGet(&p, ParamId::kResonance) == 0.2f,
          "idempotent flush (no re-publish)");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: param block\n");
    return 0;
}
