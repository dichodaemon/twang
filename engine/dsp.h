/// @file dsp.h
/// @brief Per-sample DSP primitives for one voice.
///
/// Header-only so the cycle harness can time each stage in isolation using
/// the exact code the engine runs.

#pragma once

#include <cmath>

#include "engine.h"

namespace engine {

/// The constant pi, as a float.
inline constexpr float kPi = 3.14159265358979323846f;

/// @brief Polynomial band-limited step (polyBLEP) correction.
/// @param t Phase within the current cycle, in [0, 1).
/// @param dt Phase increment per sample.
/// @return Correction to subtract from the naive sawtooth.
inline float DspPolyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;   // 2t - t^2 - 1
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;   // t^2 + 2t + 1
    }
    return 0.0f;
}

/// @brief Advance the polyBLEP sawtooth one sample.
/// @param v Voice to update.
/// @return Sawtooth sample.
inline float DspOscTick(Voice *v) {
    float saw = 2.0f * v->phase - 1.0f;
    saw -= DspPolyBlep(v->phase, v->inc);
    v->phase += v->inc;
    if (v->phase >= 1.0f) v->phase -= 1.0f;
    return saw;
}

/// @brief Recompute TPT SVF coefficients from cutoff and Q.
///
/// Control-rate only: call once per control step, not once per sample.
/// @param v Voice to update.
/// @param fc_hz Cutoff frequency in Hz.
/// @param q Filter quality factor.
inline void DspSvfSetFq(Voice *v, float fc_hz, float q) {
    v->g = std::tan(kPi * fc_hz / kSampleRate);
    v->k = 1.0f / q;
    v->a1 = 1.0f / (1.0f + v->g * (v->g + v->k));
    v->a2 = v->g * v->a1;
    v->a3 = v->g * v->a2;
}

/// @brief Run one sample through the TPT SVF.
/// @param v Voice to update.
/// @param x Input sample.
/// @return Lowpass output sample.
inline float DspSvfTick(Voice *v, float x) {
    float v3 = x - v->ic2eq;
    float v1 = v->a1 * v->ic1eq + v->a2 * v3;
    float v2 = v->ic2eq + v->a2 * v->ic1eq + v->a3 * v3;
    v->ic1eq = 2.0f * v1 - v->ic1eq;
    v->ic2eq = 2.0f * v2 - v->ic2eq;
    return v->ic2eq;
}

}  // namespace engine
