#include "engine.h"

#include "dsp.h"
#include "ipc.h"
#include "params.h"

namespace engine {

namespace {

Part g_parts[kNumParts];      // shared per-part parameters (audio thread reads)
Voice g_voices[kNumVoices];   // audio-thread-only DSP state
EventRing g_events;           // control → audio events
ParamBlock g_param_block;     // control → audio parameters

// Control-thread view of voice ownership: which voices carry a held note.
// This table is the placeholder for the voice allocator (reservation +
// stealing), which will replace the first-free scan in a later step. The
// audio thread never touches it.
struct VoiceOwner {
    bool active;
    float freq;
    std::uint8_t part;
};
VoiceOwner g_owner[kNumVoices];

// DSP-specific mapping: normalized resonance -> Q (not the display %).
float QFromResonance(float resonance) {
    return 0.5f + resonance * resonance * 20.0f;  // Q 0.5 .. 20.5
}

void UpdateFilterCoeffs(Voice *v, const Part *p) {
    float env_cutoff = p->cutoff + p->filter_env_amount * v->env;
    if (env_cutoff > 1.0f) env_cutoff = 1.0f;
    if (env_cutoff < 0.0f) env_cutoff = 0.0f;
    float fc = ParamNormToDisp(
        &g_params[static_cast<std::size_t>(ParamId::kCutoff)], env_cutoff);
    DspSvfSetFq(v, fc, QFromResonance(p->resonance));
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
void EnterDecay(Voice *v, const Part *p) {
    v->env = 1.0f;
    float decay_s = ParamGetDisp(p, ParamId::kDecay);
    if (decay_s <= 0.0f) {
        v->env = p->sustain;
        v->stage = Voice::Stage::kSustain;
        v->env_inc = 0.0f;
    } else {
        v->stage = Voice::Stage::kDecay;
        v->env_inc = EnvInc(-(1.0f - p->sustain), decay_s);
    }
}

// Advance the envelope by `samples` (control step).
void UpdateEnvelope(Voice *v, int samples, const Part *p) {
    switch (v->stage) {
    case Voice::Stage::kAttack:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env >= 1.0f) EnterDecay(v, p);
        break;
    case Voice::Stage::kDecay:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env <= p->sustain) {
            v->env = p->sustain;
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
void StartNote(Voice *v, float freq_hz, const Part *p) {
    v->phase = 0.0f;
    v->inc = freq_hz / kSampleRate;
    v->env = 0.0f;
    // Clear the filter integrators so a reused voice starts a note with no
    // leftover energy from the previous note (a fresh note = a fresh filter).
    v->ic1eq = 0.0f;
    v->ic2eq = 0.0f;

    float attack_s = ParamGetDisp(p, ParamId::kAttack);
    if (attack_s <= 0.0f) {
        EnterDecay(v, p);  // instant attack
    } else {
        v->stage = Voice::Stage::kAttack;
        v->env_inc = EnvInc(1.0f, attack_s);
    }
}

// Release the current note (audio thread).
void ReleaseNote(Voice *v, const Part *p) {
    if (v->stage == Voice::Stage::kIdle) return;
    float release_s = ParamGetDisp(p, ParamId::kRelease);
    if (release_s <= 0.0f) {
        v->env = 0.0f;
        v->stage = Voice::Stage::kIdle;
        v->env_inc = 0.0f;
    } else {
        v->stage = Voice::Stage::kRelease;
        v->env_inc = EnvInc(-v->env, release_s);
    }
}

// Drain queued events into the voices (audio thread, block boundary).
void ApplyEvents() {
    Event e;
    while (g_events.Pop(&e)) {
        if (e.voice >= kNumVoices) continue;
        Voice *v = &g_voices[e.voice];
        if (e.type == Event::Type::kNoteOn) {
            if (e.part >= kNumParts) continue;
            v->part = e.part;
            StartNote(v, e.freq, &g_parts[e.part]);
        } else {
            ReleaseNote(v, &g_parts[v->part]);
        }
    }
}

void RenderBlock(float *out, int frames) {
    g_param_block.Commit(g_parts);  // snapshot params into all parts
    ApplyEvents();                  // then apply note on/off with latest params

    for (int i = 0; i < frames; ++i) out[i] = 0.0f;

    for (int v = 0; v < kNumVoices; ++v) {
        Voice *voice = &g_voices[v];
        if (voice->stage == Voice::Stage::kIdle) continue;
        const Part *part = &g_parts[voice->part];
        for (int start = 0; start < frames; start += kControlDecimation) {
            int n = kControlDecimation;
            if (start + n > frames) n = frames - start;

            UpdateEnvelope(voice, n, part);
            UpdateFilterCoeffs(voice, part);

            for (int i = 0; i < n; ++i) {
                float saw = DspOscTick(voice);
                float lp = DspSvfTick(voice, saw);
                out[start + i] += lp * voice->env;
            }
        }
    }

    for (int i = 0; i < frames; ++i) out[i] = Clamp(out[i]);
}

}  // namespace

void EngineInit() {
    for (int i = 0; i < kNumParts; ++i) g_parts[i] = Part{};
    for (int i = 0; i < kNumVoices; ++i) {
        g_voices[i] = Voice{};
        g_owner[i] = VoiceOwner{};
    }
    g_events.Reset();
    g_param_block.Reset(g_params);
    g_param_block.Commit(g_parts);
}

void EngineNoteOn(int part, float freq_hz) {
    if (part < 0 || part >= kNumParts) return;
    for (int i = 0; i < kNumVoices; ++i) {
        if (g_owner[i].active) continue;
        g_owner[i].active = true;
        g_owner[i].freq = freq_hz;
        g_owner[i].part = static_cast<std::uint8_t>(part);
        g_events.Push({Event::Type::kNoteOn, static_cast<std::uint8_t>(part),
                       static_cast<std::uint8_t>(i), freq_hz});
        return;
    }
    // No free voice: drop the note. The voice allocator will steal here.
}

void EngineNoteOff(int part, float freq_hz) {
    if (part < 0 || part >= kNumParts) return;
    for (int i = 0; i < kNumVoices; ++i) {
        if (g_owner[i].active && g_owner[i].part == part &&
            g_owner[i].freq == freq_hz) {
            g_owner[i].active = false;
            g_events.Push({Event::Type::kNoteOff,
                           static_cast<std::uint8_t>(part),
                           static_cast<std::uint8_t>(i), 0.0f});
            return;
        }
    }
}

void EngineSetParam(int part, ParamId id, float norm) {
    if (part < 0 || part >= kNumParts) return;
    g_param_block.Set(part, id, norm);
}

void EngineSetParamDisp(int part, ParamId id, float disp) {
    if (part < 0 || part >= kNumParts) return;
    g_param_block.Set(
        part, id,
        ParamDispToNorm(&g_params[static_cast<std::size_t>(id)], disp));
}

void Render(float *out, int frames) {
    for (int done = 0; done < frames; done += kBlockSize) {
        int n = kBlockSize;
        if (done + n > frames) n = frames - done;
        RenderBlock(out + done, n);
    }
}

}  // namespace engine
