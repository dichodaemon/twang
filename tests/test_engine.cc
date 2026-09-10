#include <cmath>
#include <cstdio>
#include <vector>

#include "engine.h"
#include "params.h"

using namespace engine;

int main() {
    constexpr int kNumSamples = kSampleRate;  // 1 second
    std::vector<float> buf(kNumSamples);

    EngineInit();
    EngineSetParamDisp(0, ParamId::kAttack, 0.01f);
    EngineSetParamDisp(0, ParamId::kDecay, 0.2f);
    EngineSetParam(0, ParamId::kSustain, 0.7f);
    EngineSetParamDisp(0, ParamId::kRelease, 0.2f);
    EngineNoteOn(0, 440.0f, 127);
    Render(buf.data(), kNumSamples);

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
    EngineNoteOff(0, 440.0f);
    Render(buf.data(), kNumSamples);
    float tail = 0.0f;
    for (int i = kNumSamples - kBlockSize; i < kNumSamples; ++i) {
        float a = std::fabs(buf[i]);
        if (a > tail) tail = a;
    }
    if (tail > 0.01f) { std::printf("FAIL: tail %.3f not silent after release\n", tail); return 1; }

    // Velocity: a soft note is quieter than a full-velocity note.
    EngineInit();
    EngineSetParamDisp(0, ParamId::kAttack, 0.01f);
    EngineSetParamDisp(0, ParamId::kDecay, 0.2f);
    EngineSetParam(0, ParamId::kSustain, 0.7f);
    EngineNoteOn(0, 440.0f, 127);
    Render(buf.data(), kNumSamples);
    float peak_full = 0.0f;
    for (int i = 0; i < kNumSamples; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak_full) peak_full = a;
    }
    EngineInit();
    EngineSetParamDisp(0, ParamId::kAttack, 0.01f);
    EngineSetParamDisp(0, ParamId::kDecay, 0.2f);
    EngineSetParam(0, ParamId::kSustain, 0.7f);
    EngineNoteOn(0, 440.0f, 32);
    Render(buf.data(), kNumSamples);
    float peak_soft = 0.0f;
    for (int i = 0; i < kNumSamples; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak_soft) peak_soft = a;
    }
    if (peak_full <= peak_soft) {
        std::printf("FAIL: velocity 127 peak %.3f not louder than velocity 32 peak %.3f\n",
                    peak_full, peak_soft);
        return 1;
    }

    std::printf("PASS: peak=%.3f rms=%.3f tail=%.4f vel=%.3f/%.3f\n",
                peak, rms, tail, peak_full, peak_soft);
    return 0;
}
