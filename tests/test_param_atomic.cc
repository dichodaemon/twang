#include <atomic>
#include <cstdio>
#include <thread>

#include "engine.h"
#include "ipc.h"
#include "ipc_shared.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

// The control thread writes cutoff and resonance as a matched pair (both = v)
// inside one flush; the audio thread (Commit) must therefore never observe a
// snapshot where the two differ. A differing snapshot is a half-applied batch.
int main() {
    // Section 1: threaded stress — a multi-field batch is atomic.
    {
        ParamBlock block;
        block.Reset(g_params);
        block.Set(0, ParamId::kCutoff, 0.5f);
        block.Set(0, ParamId::kResonance, 0.5f);
        block.Flush();  // matched baseline before the reader starts

        std::atomic<bool> done{false};
        std::atomic<int> mismatch{0};

        std::thread writer([&]() {
            for (int i = 0; i < 20000; ++i) {
                const float v = static_cast<float>(i % 101) / 100.0f;
                block.Set(0, ParamId::kCutoff, v);
                block.Set(0, ParamId::kResonance, v);
                block.Flush();
                std::this_thread::yield();  // widen the interleaving window
            }
            done.store(true, std::memory_order_release);
        });

        Part snap[kNumParts] = {};
        int commits = 0;
        while (!done.load(std::memory_order_acquire)) {
            block.Commit(snap);
            ++commits;
            const float c = ParamGet(&snap[0], ParamId::kCutoff);
            const float r = ParamGet(&snap[0], ParamId::kResonance);
            if (c != r) ++mismatch;
        }
        writer.join();

        Check(mismatch.load() == 0, "no half-applied commit observed");
        Check(commits > 0, "reader observed at least one commit");
    }

    // Section 2: engine batch API — single-field auto-flush vs batched defer.
    {
        EngineInit();

        EngineSetParam(0, ParamId::kCutoff, 0.4f);  // auto-flush
        Part p[kNumParts] = {};
        Shared().params.Commit(p);
        Check(ParamGet(&p[0], ParamId::kCutoff) == 0.4f,
              "EngineSetParam auto-flushes");

        EngineBeginBatch();
        EngineSetParam(0, ParamId::kCutoff, 0.8f);
        EngineSetParam(0, ParamId::kResonance, 0.8f);
        // Before Flush, the committed snapshot is still the pre-batch state.
        Shared().params.Commit(p);
        Check(ParamGet(&p[0], ParamId::kCutoff) == 0.4f,
              "EngineBeginBatch defers publish");

        EngineFlush();
        Shared().params.Commit(p);
        Check(ParamGet(&p[0], ParamId::kCutoff) == 0.8f &&
                  ParamGet(&p[0], ParamId::kResonance) == 0.8f,
              "EngineFlush publishes the batch atomically");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: param atomic\n");
    return 0;
}
