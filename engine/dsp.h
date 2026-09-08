#ifndef ENGINE_DSP_H
#define ENGINE_DSP_H

#include <math.h>

#include "engine.h"

/*
 * Per-sample DSP primitives for one Voice, header-only so the cycle harness
 * can time each stage in isolation with the same code the engine uses.
 */

#define DSP_PI 3.14159265358979323846f

/* Polynomial band-limited step (polyBLEP) correction. */
static inline float dsp_poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;   /* 2t - t^2 - 1 */
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;   /* t^2 + 2t + 1 */
    }
    return 0.0f;
}

/* One sample of the polyBLEP saw; advances v->phase. */
static inline float dsp_osc_tick(Voice *v) {
    float saw = 2.0f * v->phase - 1.0f;
    saw -= dsp_poly_blep(v->phase, v->inc);
    v->phase += v->inc;
    if (v->phase >= 1.0f) v->phase -= 1.0f;
    return saw;
}

/* Recompute TPT SVF coefficients from cutoff (Hz) and Q. Control-rate only. */
static inline void dsp_svf_set_f_q(Voice *v, float fc_hz, float q) {
    v->g = tanf(DSP_PI * fc_hz / ENGINE_SAMPLE_RATE);
    v->k = 1.0f / q;
    v->a1 = 1.0f / (1.0f + v->g * (v->g + v->k));
    v->a2 = v->g * v->a1;
    v->a3 = v->g * v->a2;
}

/* One sample of the TPT SVF; returns the lowpass output. */
static inline float dsp_svf_tick(Voice *v, float x) {
    float v3 = x - v->ic2eq;
    float v1 = v->a1 * v->ic1eq + v->a2 * v3;
    float v2 = v->ic2eq + v->a2 * v->ic1eq + v->a3 * v3;
    v->ic1eq = 2.0f * v1 - v->ic1eq;
    v->ic2eq = 2.0f * v2 - v->ic2eq;
    return v->ic2eq;
}

#endif /* ENGINE_DSP_H */
