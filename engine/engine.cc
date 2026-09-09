#include "engine.h"

#include "dsp.h"
#include "ipc.h"
#include "params.h"

namespace engine {

namespace {

Voice g_voice;            // audio-thread-only DSP state
EventRing g_events;       // control → audio events
ParamBlock g_param_block;  // control → audio parameters

// DSP-specific mapping: normalized resonance -> Q (not the display %).
float QFromResonance(float resonance) {
    return 0.5f + resonance * resonance * 20.0f;  // Q 0.5 .. 20.5
}

void UpdateFilterCoeffs() {
    Voice *v = &g_voice;
    float env_cutoff = v->cutoff + v->filter_env_amount * v->env;
    if (env_cutoff > 1.0f) env_cutoff = 1.0f;
    if (env_cutoff < 0.0f) env_cutoff = 0.0f;
    float fc = ParamNormToDisp(
        &g_params[static_cast<std::size_t>(ParamId::kCutoff)], env_cutoff);
    DspSvfSetFq(v, fc, QFromResonance(v->resonance));
}

float Clamp(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

// Per-sample envelope increment to cover `delta` over `time_s` seconds.
// A non-positive time is instant: returns 0 and the caller jumps the level.
float EnvInc(float delta, float time_s) {
    if (time_s <= 0.0f) return 0.0f;
    return delta / (time_s * kSampleRate);
}

// Move from the attack peak into decay, skipping straight to sustain if the
// decay time is zero (instant).
void EnterDecay() {
    Voice *v = &g_voice;
    v->env = 1.0f;
    float decay_s = ParamGetDisp(v, ParamId::kDecay);
    if (decay_s <= 0.0f) {
        v->env = v->sustain;
        v->stage = Voice::Stage::kSustain;
        v->env_inc = 0.0f;
    } else {
        v->stage = Voice::Stage::kDecay;
        v->env_inc = EnvInc(-(1.0f - v->sustain), decay_s);
    }
}

// Advance the envelope by `samples` (control step).
void UpdateEnvelope(int samples) {
    Voice *v = &g_voice;
    switch (v->stage) {
    case Voice::Stage::kAttack:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env >= 1.0f) EnterDecay();
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

// Start a note on the voice (audio thread).
void StartNote(float freq_hz) {
    Voice *v = &g_voice;
    v->phase = 0.0f;
    v->inc = freq_hz / kSampleRate;
    v->env = 0.0f;
    v->gate = true;

    float attack_s = ParamGetDisp(v, ParamId::kAttack);
    if (attack_s <= 0.0f) {
        EnterDecay();  // instant attack
    } else {
        v->stage = Voice::Stage::kAttack;
        v->env_inc = EnvInc(1.0f, attack_s);
    }
}

// Release the current note (audio thread).
void ReleaseNote() {
    Voice *v = &g_voice;
    if (v->stage == Voice::Stage::kIdle) return;
    float release_s = ParamGetDisp(v, ParamId::kRelease);
    if (release_s <= 0.0f) {
        v->env = 0.0f;
        v->stage = Voice::Stage::kIdle;
        v->env_inc = 0.0f;
    } else {
        v->stage = Voice::Stage::kRelease;
        v->env_inc = EnvInc(-v->env, release_s);
    }
    v->gate = false;
}

// Drain queued events into the voice (audio thread, block boundary).
void ApplyEvents() {
    Event e;
    while (g_events.Pop(&e)) {
        if (e.type == Event::Type::kNoteOn)
            StartNote(e.freq);
        else if (e.type == Event::Type::kNoteOff)
            ReleaseNote();
    }
}

void RenderBlock(float *out, int frames) {
    g_param_block.Commit(&g_voice);  // snapshot params into the voice
    ApplyEvents();                   // then apply note on/off with latest params

    Voice *v = &g_voice;
    for (int start = 0; start < frames; start += kControlDecimation) {
        int n = kControlDecimation;
        if (start + n > frames) n = frames - start;

        UpdateEnvelope(n);
        UpdateFilterCoeffs();

        for (int i = 0; i < n; ++i) {
            float saw = DspOscTick(v);
            float lp = DspSvfTick(v, saw);
            out[start + i] = Clamp(lp * v->env);
        }
    }
}

}  // namespace

void EngineInit() {
    g_voice = Voice{};
    g_events.Reset();
    g_param_block.Reset(g_params);
    g_param_block.Commit(&g_voice);
    UpdateFilterCoeffs();
}

void EngineNoteOn(float freq_hz) {
    g_events.Push({Event::Type::kNoteOn, freq_hz});
}

void EngineNoteOff() {
    g_events.Push({Event::Type::kNoteOff, 0.0f});
}

void EngineSetParam(ParamId id, float norm) {
    g_param_block.Set(id, norm);
}

void EngineSetParamDisp(ParamId id, float disp) {
    g_param_block.Set(
        id, ParamDispToNorm(&g_params[static_cast<std::size_t>(id)], disp));
}

void Render(float *out, int frames) {
    for (int done = 0; done < frames; done += kBlockSize) {
        int n = kBlockSize;
        if (done + n > frames) n = frames - done;
        RenderBlock(out + done, n);
    }
}

}  // namespace engine
