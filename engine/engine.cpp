#include "engine.h"

#include <cmath>

#include "dsp.h"

namespace {

constexpr float kMinHz = 20.0f;
constexpr float kMaxHz = 20000.0f;
constexpr float kMinTime = 0.001f;  // avoids div-by-zero and denormals

Voice g_voice;

/* --- parameter mapping (control rate only) --- */

float cutoff_hz(float normalized) {
    return kMinHz * std::pow(kMaxHz / kMinHz, normalized);
}

float q_from_resonance(float resonance) {
    return 0.5f + resonance * resonance * 20.0f;  // Q 0.5 .. 20.5
}

void update_filter_coeffs() {
    Voice *v = &g_voice;
    float env_cutoff = v->cutoff + v->filter_env_amount * v->env;
    if (env_cutoff > 1.0f) env_cutoff = 1.0f;
    if (env_cutoff < 0.0f) env_cutoff = 0.0f;
    dsp_svf_set_f_q(v, cutoff_hz(env_cutoff), q_from_resonance(v->resonance));
}

float clampf(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

/* Advance the envelope by `samples` (control step). */
void update_envelope(int samples) {
    Voice *v = &g_voice;
    switch (v->stage) {
    case 1:  /* attack */
        v->env += v->env_inc * (float)samples;
        if (v->env >= 1.0f) {
            v->env = 1.0f;
            v->stage = 2;
            v->env_inc = -(1.0f - v->sustain) / (v->decay_s * ENGINE_SAMPLE_RATE);
        }
        break;
    case 2:  /* decay */
        v->env += v->env_inc * (float)samples;
        if (v->env <= v->sustain) {
            v->env = v->sustain;
            v->stage = 3;
            v->env_inc = 0.0f;
        }
        break;
    case 4:  /* release */
        v->env += v->env_inc * (float)samples;
        if (v->env <= 0.0f) {
            v->env = 0.0f;
            v->stage = 0;
            v->env_inc = 0.0f;
        }
        break;
    default:  /* idle (0) or sustain (3): hold */
        break;
    }
}

void render_block(float *out, int frames) {
    Voice *v = &g_voice;
    for (int start = 0; start < frames; start += ENGINE_CONTROL_DECIMATION) {
        int n = ENGINE_CONTROL_DECIMATION;
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

extern "C" void engine_init(void) {
    g_voice = Voice{};
    g_voice.cutoff = 1.0f;
    g_voice.resonance = 0.0f;
    g_voice.filter_env_amount = 0.0f;
    g_voice.sustain = 0.7f;
    g_voice.attack_s = 0.01f;
    g_voice.decay_s = 0.2f;
    g_voice.release_s = 0.2f;
    update_filter_coeffs();
}

extern "C" void engine_note_on(float freq_hz) {
    Voice *v = &g_voice;
    v->phase = 0.0f;
    v->inc = freq_hz / ENGINE_SAMPLE_RATE;
    v->env = 0.0f;
    v->stage = 1;
    v->env_inc = 1.0f / (v->attack_s * ENGINE_SAMPLE_RATE);
    v->gate = 1;
}

extern "C" void engine_note_off(void) {
    Voice *v = &g_voice;
    if (v->stage == 0) return;
    float t = v->release_s < kMinTime ? kMinTime : v->release_s;
    v->stage = 4;
    v->env_inc = -(v->env / (t * ENGINE_SAMPLE_RATE));
    v->gate = 0;
}

extern "C" void engine_set_cutoff(float normalized) {
    g_voice.cutoff = normalized;
}

extern "C" void engine_set_resonance(float normalized) {
    g_voice.resonance = normalized;
}

extern "C" void engine_set_filter_env(float amount) {
    g_voice.filter_env_amount = amount;
}

extern "C" void engine_set_adsr(float attack_s, float decay_s, float sustain,
                                float release_s) {
    Voice *v = &g_voice;
    v->attack_s = attack_s < kMinTime ? kMinTime : attack_s;
    v->decay_s = decay_s < kMinTime ? kMinTime : decay_s;
    v->release_s = release_s < kMinTime ? kMinTime : release_s;
    v->sustain = sustain < 0.0f ? 0.0f : (sustain > 1.0f ? 1.0f : sustain);
}

extern "C" void render(float *out, int frames) {
    for (int done = 0; done < frames; done += ENGINE_BLOCK_SIZE) {
        int n = ENGINE_BLOCK_SIZE;
        if (done + n > frames) n = frames - done;
        render_block(out + done, n);
    }
}
