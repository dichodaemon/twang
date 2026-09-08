#include <cmath>
#include <cstdio>
#include <vector>

#include "engine.h"
#include "params.h"

using namespace engine;

int main() {
    constexpr int kNumSamples = kSampleRate;  // 1 second
    std::vector<float> buf(kNumSamples);

    engine_init();
    Voice *v = engine_voice();
    param_set_disp(v, ParamId::kAttack, 0.01f);
    param_set_disp(v, ParamId::kDecay, 0.2f);
    param_set(v, ParamId::kSustain, 0.7f);
    param_set_disp(v, ParamId::kRelease, 0.2f);
    engine_note_on(440.0f);
    render(buf.data(), kNumSamples);

    float peak = 0.0f, sum_sq = 0.0f;
    int bad = 0;
    for (int i = 0; i < kNumSamples; ++i) {
        float s = buf[i];
        if (!std::isfinite(s) || s > 1.0f || s < -1.0f) ++bad;
        float a = std::fabs(s);
        if (a > peak) peak = a;
        sum_sq += s * s;
    }
    float rms = std::sqrt(sum_sq / kNumSamples);

    if (bad) { std::printf("FAIL: %d samples out of range or non-finite\n", bad); return 1; }
    if (peak < 0.1f) { std::printf("FAIL: peak %.3f too low (silent)\n", peak); return 1; }
    if (rms < 0.01f) { std::printf("FAIL: rms %.3f too low (silent)\n", rms); return 1; }

    /* release: after one second the envelope must have decayed to silence */
    engine_note_off();
    render(buf.data(), kNumSamples);
    float tail = 0.0f;
    for (int i = kNumSamples - kBlockSize; i < kNumSamples; ++i) {
        float a = std::fabs(buf[i]);
        if (a > tail) tail = a;
    }
    if (tail > 0.01f) { std::printf("FAIL: tail %.3f not silent after release\n", tail); return 1; }

    std::printf("PASS: peak=%.3f rms=%.3f tail=%.4f\n", peak, rms, tail);
    return 0;
}
