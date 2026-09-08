#include "engine.h"

#include "dsp.h"
#include "params.h"

namespace engine {

namespace {

Voice g_voice;

// DSP-specific mapping: normalized resonance -> Q (not the display %).
float q_from_resonance(float resonance) {
    return 0.5f + resonance * resonance * 20.0f;  // Q 0.5 .. 20.5
}

void update_filter_coeffs() {
    Voice *v = &g_voice;
    float env_cutoff = v->cutoff + v->filter_env_amount * v->env;
    if (env_cutoff > 1.0f) env_cutoff = 1.0f;
    if (env_cutoff < 0.0f) env_cutoff = 0.0f;
    float fc = param_norm_to_disp(
        &g_params[static_cast<std::size_t>(ParamId::kCutoff)], env_cutoff);
    dsp_svf_set_f_q(v, fc, q_from_resonance(v->resonance));
}

float clampf(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

// Advance the envelope by `samples` (control step).
void update_envelope(int samples) {
    Voice *v = &g_voice;
    switch (v->stage) {
    case Voice::Stage::kAttack:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env >= 1.0f) {
            v->env = 1.0f;
            v->stage = Voice::Stage::kDecay;
            float decay_s = param_get_disp(v, ParamId::kDecay);
            v->env_inc = -(1.0f - v->sustain) / (decay_s * kSampleRate);
        }
        break;
    case Voice::Stage::kDecay:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env <= v->sustain) {
            v->env = v->sustain;
            v->stage = Voice::Stage::kSustain;
            v->env_inc = 0.0f;
        }
        break;
    case Voice::Stage::kRelease:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env <= 0.0f) {
            v->env = 0.0f;
            v->stage = Voice::Stage::kIdle;
            v->env_inc = 0.0f;
        }
        break;
    default:  // kIdle or kSustain: hold
        break;
    }
}

void render_block(float *out, int frames) {
    Voice *v = &g_voice;
    for (int start = 0; start < frames; start += kControlDecimation) {
        int n = kControlDecimation;
        if (start + n > frames) n = frames - start;

        update_envelope(n);
        update_filter_coeffs();

        for (int i = 0; i < n; ++i) {
            float saw = dsp_osc_tick(v);
            float lp = dsp_svf_tick(v, saw);
            out[start + i] = clampf(lp * v->env);
        }
    }
}

}  // namespace

void engine_init() {
    g_voice = Voice{};
    for (int i = 0; i < static_cast<int>(ParamId::kCount); ++i)
        param_set(&g_voice, static_cast<ParamId>(i), g_params[i].def);
    update_filter_coeffs();
}

void engine_note_on(float freq_hz) {
    Voice *v = &g_voice;
    v->phase = 0.0f;
    v->inc = freq_hz / kSampleRate;
    v->env = 0.0f;
    v->stage = Voice::Stage::kAttack;
    float attack_s = param_get_disp(v, ParamId::kAttack);
    v->env_inc = 1.0f / (attack_s * kSampleRate);
    v->gate = true;
}

void engine_note_off() {
    Voice *v = &g_voice;
    if (v->stage == Voice::Stage::kIdle) return;
    float release_s = param_get_disp(v, ParamId::kRelease);
    v->stage = Voice::Stage::kRelease;
    v->env_inc = -(v->env / (release_s * kSampleRate));
    v->gate = false;
}

Voice *engine_voice() {
    return &g_voice;
}

void render(float *out, int frames) {
    for (int done = 0; done < frames; done += kBlockSize) {
        int n = kBlockSize;
        if (done + n > frames) n = frames - done;
        render_block(out + done, n);
    }
}

}  // namespace engine
