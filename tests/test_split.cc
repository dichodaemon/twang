#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "engine.h"
#include "params.h"

using namespace engine;

// Exercises the control/audio split concurrently. Run under ThreadSanitizer
// to detect data races in the event ring and parameter block; run normally to
// verify the split produces audio.
int main() {
    EngineInit();

    std::atomic<bool> done{false};
    std::thread control([&done] {
        for (int i = 0; i < 50; ++i) {
            EngineSetParamDisp(ParamId::kCutoff, 100.0f + (i % 10) * 500.0f);
            EngineSetParamDisp(ParamId::kResonance, (i % 10) * 10.0f);
            EngineNoteOn(220.0f + (i % 12) * 55.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            EngineNoteOff();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        done.store(true, std::memory_order_release);
    });

    // Audio thread (this thread): render continuously.
    std::vector<float> buf(kBlockSize);
    float peak = 0.0f;
    while (!done.load(std::memory_order_acquire)) {
        Render(buf.data(), kBlockSize);
        for (float s : buf)
            if (s > peak) peak = s;
    }
    Render(buf.data(), kBlockSize);  // drain the final block

    control.join();

    if (peak < 0.001f) {
        std::printf("FAIL: peak %.4f too low (no audio produced)\n", peak);
        return 1;
    }
    std::printf("PASS: control/audio split, peak=%.3f\n", peak);
    return 0;
}
