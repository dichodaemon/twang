#include "engine.h"

#include "allocator.h"
#include "dsp.h"
#include "ipc.h"
#include "ipc_shared.h"
#include "params.h"

namespace engine {

namespace {

Part g_parts[kNumParts];      // per-part parameters (audio core reads)
Voice g_voices[kNumVoices];   // audio-core-only DSP state
EventRing &g_events = Shared().events;       // control → audio events (shared)
ParamBlock &g_param_block = Shared().params; // control → audio params (shared)
Allocator g_alloc;            // control-core voice ownership (allocator)

// DSP-specific mapping: normalized resonance -> Q (not the display %).
float QFromResonance(float resonance) {
    return 0.5f + resonance * resonance * 20.0f;  // Q 0.5 .. 20.5
}

void UpdateFilterCoeffs(Voice *v, const Part *p) {
    float env_cutoff = p->cutoff + p->filter_env_amount * v->env;
    if (env_cutoff > 1.0f) env_cutoff = 1.0f;
    if (env_cutoff < 0.0f) env_cutoff = 0.0f;
    const float q = QFromResonance(p->resonance);

    // Exact skip: env_cutoff and q are deterministic floats, so bit-identical
    // inputs imply bit-identical fc (ParamNormToDisp) and SVF coefficients
    // (DspSvfSetFq). Avoids the pow + tan for an unchanged filter (static
    // cutoff, or an envelope held at sustain).
    if (env_cutoff == v->last_env_cutoff && q == v->last_q) return;
    v->last_env_cutoff = env_cutoff;
    v->last_q = q;

    const float fc = ParamNormToDisp(
        &g_params[static_cast<std::size_t>(ParamId::kCutoff)], env_cutoff);
    DspSvfSetFq(v, fc, q);
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

// How long a stolen voice ramps its envelope down before retriggering the new
// note. A few ms: long enough to avoid a click, short enough to feel instant.
constexpr float kStealTime = 0.005f;  // 5 ms

// Per-voice output gain, so a polyphonic chord sums below the ±1 clamp. A
// stopgap: proper velocity sensitivity replaces this later.
constexpr float kVoiceGain = 0.25f;

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

void StartNote(Voice *v, float freq_hz, const Part *p);  // defined below

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
    case Voice::Stage::kSteal:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env <= 0.0f) {
            v->env = 0.0f;
            StartNote(v, v->steal_freq, p);  // ramp done: retrigger the new note
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
    v->last_env_cutoff = -1.0f;  // sentinel: force the coefficient recompute

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

// Steal the voice for a new note (audio thread): ramp the current envelope
// down over a few ms, then retrigger the new note. Avoids the click an
// instant cut would cause (digest §9: "terminate", not "kill").
void StealNote(Voice *v, float freq_hz, std::uint8_t part) {
    v->part = part;
    if (v->stage == Voice::Stage::kIdle || v->env <= 0.0f) {
        StartNote(v, freq_hz, &g_parts[part]);  // nothing to ramp: start now
        return;
    }
    v->steal_freq = freq_hz;
    v->stage = Voice::Stage::kSteal;
    v->env_inc = EnvInc(-v->env, kStealTime);
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
        } else if (e.type == Event::Type::kSteal) {
            if (e.part >= kNumParts) continue;
            StealNote(v, e.freq, e.part);
        } else {
            ReleaseNote(v, &g_parts[v->part]);
        }
    }
}

void RenderBlock(float *out, int frames) {
    g_param_block.Commit(g_parts);  // snapshot params into all parts

    for (int i = 0; i < frames; ++i) out[i] = 0.0f;

    // Drain note events at control-step granularity (16 samples = 0.33 ms)
    // rather than once per block: a note-on is applied within one control
    // step, not a whole block. Safe (single-threaded render loop); the mailbox
    // signal remains an advisory notification only.
    for (int start = 0; start < frames; start += kControlDecimation) {
        int n = kControlDecimation;
        if (start + n > frames) n = frames - start;

        ApplyEvents();  // note on/off with the block-start params

        for (int v = 0; v < kNumVoices; ++v) {
            Voice *voice = &g_voices[v];
            if (voice->stage == Voice::Stage::kIdle) continue;
            const Part *part = &g_parts[voice->part];

            UpdateEnvelope(voice, n, part);
            UpdateFilterCoeffs(voice, part);

            for (int i = 0; i < n; ++i) {
                float saw = DspOscTick(voice);
                float lp = DspSvfTick(voice, saw);
                out[start + i] += lp * voice->env * kVoiceGain;
            }
        }
    }

    for (int i = 0; i < frames; ++i) out[i] = Clamp(out[i]);
}

}  // namespace

void EngineInit() {
    for (int i = 0; i < kNumParts; ++i) g_parts[i] = Part{};
    for (int i = 0; i < kNumVoices; ++i) g_voices[i] = Voice{};
    g_alloc.Reset();
    g_events.Reset();
    g_param_block.Reset(g_params);
    g_param_block.Commit(g_parts);
}

__attribute__((weak)) void EngineEventsPending() {}

void EngineNoteOn(int part, float freq_hz) {
    const Allocator::Decision d = g_alloc.NoteOn(part, freq_hz);
    if (d.voice < 0) return;  // dropped: full and nothing to steal
    const Event::Type type =
        d.steal ? Event::Type::kSteal : Event::Type::kNoteOn;
    g_events.Push({type, static_cast<std::uint8_t>(part),
                   static_cast<std::uint8_t>(d.voice), freq_hz});
    EngineEventsPending();
}

void EngineNoteOff(int part, float freq_hz) {
    const int voice = g_alloc.NoteOff(part, freq_hz);
    if (voice < 0) return;  // no matching note
    g_events.Push({Event::Type::kNoteOff, static_cast<std::uint8_t>(part),
                   static_cast<std::uint8_t>(voice), 0.0f});
    EngineEventsPending();
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

float EngineGetParam(int part, ParamId id) {
    if (part < 0 || part >= kNumParts) return 0.0f;
    return g_param_block.Get(part, id);
}

void Render(float *out, int frames) {
    for (int done = 0; done < frames; done += kBlockSize) {
        int n = kBlockSize;
        if (done + n > frames) n = frames - done;
        RenderBlock(out + done, n);
    }
}

}  // namespace engine
