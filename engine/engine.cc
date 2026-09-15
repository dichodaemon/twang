/// @file engine.cc
/// @brief Pure DSP primitives for the drive shaper: the clamped Padé curve,
/// its shifted antiderivative, and first-order ADAA. Shared by the audio side
/// (engine_audio.cc) and the unit tests (test_shaper).
///
/// These are the only free functions left in the engine with no state argument:
/// each operates on its parameters (a curve shape, an x, or a Voice's shaper
/// state) and touches no globals.

#include "engine.h"

#include <cmath>

namespace engine {

float CurveEval(CurveShape s, float x) {
    switch (s) {
    case CurveShape::kSoftSat:
        // Clamped Padé soft-saturation (arch-design §6.3): C¹, f(0)=0, f'(0)=1,
        // reaches ±1 at x=±3 with zero slope, then holds ±1.
        if (x >= kFTableMax) return 1.0f;
        if (x <= -kFTableMax) return -1.0f;
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }
    return 0.0f;
}

float AntiderivativeEval(CurveShape s, float x) {
    switch (s) {
    case CurveShape::kSoftSat: {
        const float ax = std::fabs(x);
        // Outside the table the closed-form asymptote |x| + kFTableAsym is
        // exact to float precision (the clamp makes f constant past ±3). The
        // >= (not >) also keeps |x| == kFTableMax out of the interpolation so
        // the last slot never reads i+1 out of bounds.
        if (ax >= kFTableMax) return ax + kFTableAsym;
        // Map x ∈ [-kFTableMax, kFTableMax] to u ∈ [0, kFTableSize-1].
        const float u =
            (x + kFTableMax) * static_cast<float>(kFTableSize - 1) /
            (2.0f * kFTableMax);
        const int i = static_cast<int>(u);
        const float frac = u - static_cast<float>(i);
        return kFTable[i] * (1.0f - frac) + kFTable[i + 1] * frac;
    }
    }
    return 0.0f;
}

float ShaperProcess(Voice *v, float x) {
    const float xp = v->shaper.xp;
    const float dx = x - xp;
    const float fx = AntiderivativeEval(CurveShape::kSoftSat, x);  // F(x)
    float y;
    if (std::fabs(dx) < kAdaaEps) {
        // Midpoint fallback: the quotient's limit as dx -> 0. A larger epsilon
        // is better than a divide-by-zero-only guard — the quotient cancels
        // catastrophically at low frequencies (output-stage arch-design §6.4).
        y = CurveEval(CurveShape::kSoftSat, (x + xp) * 0.5f);
    } else {
        y = (fx - v->shaper.Fp) / dx;
    }
    v->shaper.xp = x;
    v->shaper.Fp = fx;
    return y;
}

}  // namespace engine
