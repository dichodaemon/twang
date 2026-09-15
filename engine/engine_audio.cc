/// @file engine_audio.cc
/// @brief Audio-side engine: the render loop and its DSP helpers over the
/// public EngineAudio state.
///
/// Runs on the audio thread (the M85 on the target). Drains the shared event
/// ring and parameter block at each block boundary, evaluates the modulation
/// matrix at control rate, and renders voices into the output bus. All state
/// is reached through the passed EngineAudio&; the pure DSP primitives
/// (CurveEval/AntiderivativeEval/ShaperProcess) live in engine.cc.

#include "engine_audio.h"

#include <cmath>

#include "dsp.h"
#include "params.h"

namespace engine {

namespace {

// DSP-specific mapping: normalized resonance -> Q (not the display %).
float QFromResonance(float resonance) {
    return 0.5f + resonance * resonance * 20.0f;  // Q 0.5 .. 20.5
}

// Read a modulation source's value for the current control step.
//
// Phase 1 computes seven sources — velocity, note (key follow), gate, env0,
// env1, pitchbend, constant. The remaining sources (LFOs, env2, random,
// performance CCs) return their rest value (0/neutral) and are unreachable
// because no phase-1 route references them, so source gating never advances
// them (arch-design §5.5 "Culling").
float ReadSource(ModSourceId src, const Part *p, const Voice *v) {
    switch (src) {
    case ModSourceId::kVelocity:
        return v->vel / 127.0f;
    case ModSourceId::kNote:
        return v->key_follow;  // octaves from C4
    case ModSourceId::kGate:
        return v->gate;
    case ModSourceId::kEnv0:
    case ModSourceId::kEnv1:
        return v->env;  // phase 1: the single envelope reads as both
    case ModSourceId::kPitchBend:
        return 2.0f *
                   p->params[static_cast<std::size_t>(ParamId::kPitchBend)] -
               1.0f;
    case ModSourceId::kConstant:
        return 1.0f;
    default:
        return 0.0f;
    }
}

void UpdateFilterCoeffs(Voice *v, const Part *p, float cutoff_norm_eff,
                        float key_follow_factor) {
    const float q = QFromResonance(
        p->params[static_cast<std::size_t>(ParamId::kResonance)]);

    // Exact skip: cutoff_norm_eff, key_follow_factor and q are deterministic
    // floats, so bit-identical inputs imply bit-identical fc (ParamNormToDisp)
    // and SVF coefficients (DspSvfSetFq). Avoids the pow + tan for an
    // unchanged filter (static cutoff, or an envelope held at sustain).
    if (cutoff_norm_eff == v->last_env_cutoff &&
        key_follow_factor == v->last_key_follow && q == v->last_q)
        return;
    v->last_env_cutoff = cutoff_norm_eff;
    v->last_key_follow = key_follow_factor;
    v->last_q = q;

    // Key follow applies in the Hz domain, after the cutoff display mapping
    // (arch-design §9): fc = NormToHz(cutoff_norm_eff) × 2^key_follow_factor.
    // Clamp to the cutoff display max so key follow can never push fc above
    // Nyquist (fs/2): beyond it tan(π·fc/fs) turns negative and destabilizes
    // the SVF (the envelope-held filter would produce NaN).
    float fc = ParamNormToDisp(
                   &k_params[static_cast<std::size_t>(ParamId::kCutoff)],
                   cutoff_norm_eff) *
               std::exp2f(key_follow_factor);
    const float fc_max = k_params[static_cast<std::size_t>(ParamId::kCutoff)].disp_max;
    if (fc > fc_max) fc = fc_max;
    DspSvfSetFq(v, fc, q);
}

float Clamp(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

// DriveCurve maps the normalized drive depth [0,1] to an input gain, unity at
// 0 and a 1->10 (20 dB) span. The exact curve and ceiling are tuning
// (output-stage arch-design §8); this pins a sane starting point.
float DriveCurve(float drive_eff) {
    return std::exp2f(drive_eff * std::log2f(10.0f));
}

// Linear blend: lerp(a, b, t) = a + (b - a) * t.
inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
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
    float decay_s = ParamGetDisp(p, ParamRef{0, ParamId::kDecay});
    if (decay_s <= 0.0f) {
        v->env = p->params[static_cast<std::size_t>(ParamId::kSustain)];
        v->stage = Voice::Stage::kSustain;
        v->env_inc = 0.0f;
    } else {
        v->stage = Voice::Stage::kDecay;
        v->env_inc = EnvInc(
            -(1.0f - p->params[static_cast<std::size_t>(ParamId::kSustain)]),
            decay_s);
    }
}

void StartNote(Voice *v, float freq_hz, float velocity, const Part *p);  // below

// Advance the envelope by `samples` (control step).
void UpdateEnvelope(Voice *v, int samples, const Part *p) {
    switch (v->stage) {
    case Voice::Stage::kAttack:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env >= 1.0f) EnterDecay(v, p);
        break;
    case Voice::Stage::kDecay:
        v->env += v->env_inc * static_cast<float>(samples);
        if (v->env <=
            p->params[static_cast<std::size_t>(ParamId::kSustain)]) {
            v->env = p->params[static_cast<std::size_t>(ParamId::kSustain)];
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
            StartNote(v, v->steal_freq, v->steal_vel, p);  // ramp done: retrigger
        }
        break;
    default:  // kIdle or kSustain: hold
        break;
    }
}

// Start a note on the voice (audio thread).
void StartNote(Voice *v, float freq_hz, float velocity, const Part *p) {
    v->phase = 0.0f;
    v->inc = freq_hz / kSampleRate;
    v->vel = velocity;
    v->note = 69.0f + 12.0f * std::log2f(freq_hz / 440.0f);
    v->key_follow = (v->note - 60.0f) / 12.0f;  // octaves from C4
    v->gate = 1.0f;
    v->env = 0.0f;
    // Clear the filter integrators so a reused voice starts a note with no
    // leftover energy from the previous note (a fresh note = a fresh filter).
    v->ic1eq = 0.0f;
    v->ic2eq = 0.0f;
    v->shaper.xp = 0.0f;  // fresh note: first sample computed against silence
    v->shaper.Fp = 0.0f;
    v->last_env_cutoff = -1.0f;  // sentinel: force the coefficient recompute

    float attack_s = ParamGetDisp(p, ParamRef{0, ParamId::kAttack});
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
    v->gate = 0.0f;
    float release_s = ParamGetDisp(p, ParamRef{0, ParamId::kRelease});
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
void StealNote(Voice *v, float freq_hz, float velocity, std::uint8_t part,
               Part *parts) {
    v->part = part;
    if (v->stage == Voice::Stage::kIdle || v->env <= 0.0f) {
        StartNote(v, freq_hz, velocity, &parts[part]);  // nothing to ramp: start now
        return;
    }
    v->steal_freq = freq_hz;
    v->steal_vel = velocity;
    v->stage = Voice::Stage::kSteal;
    v->env_inc = EnvInc(-v->env, kStealTime);
}

// Drain queued events into the voices (audio thread, block boundary).
void ApplyEvents(EngineAudio &e) {
    Event ev;
    while (e.ipc->events.Pop(&ev)) {
        if (ev.voice >= kNumVoices) continue;
        Voice *v = &e.voices[ev.voice];
        if (ev.type == Event::Type::kNoteOn) {
            if (ev.part >= kNumParts) continue;
            v->part = ev.part;
            StartNote(v, ev.freq, ev.velocity, &e.parts[ev.part]);
        } else if (ev.type == Event::Type::kSteal) {
            if (ev.part >= kNumParts) continue;
            StealNote(v, ev.freq, ev.velocity, ev.part, e.parts);
        } else {
            ReleaseNote(v, &e.parts[v->part]);
        }
    }
}

void RenderBlock(EngineAudio &e, float *out, int frames) {
    e.ipc->params.Commit(e.parts);  // snapshot params into all parts

    for (int i = 0; i < frames; ++i) {
        e.buses[0].L[i] = 0.0f;
        e.buses[0].R[i] = 0.0f;
    }

    // Drain note events at control-step granularity (16 samples = 0.33 ms)
    // rather than once per block: a note-on is applied within one control
    // step, not a whole block. Safe (single-threaded render loop); the mailbox
    // signal remains an advisory notification only.
    for (int start = 0; start < frames; start += kControlDecimation) {
        int n = kControlDecimation;
        if (start + n > frames) n = frames - start;

        ApplyEvents(e);  // note on/off with the block-start params

        // Compute drive_in_use per part and reset the shaper state on a
        // false -> true enable edge. A ramped enable (depth ~= 0) needs no
        // reset, but a jumped enable (preset load, CC 0 -> 100) would resume
        // from stale ADAA state and click (output-stage arch-design §7).
        for (int p = 0; p < kNumParts; ++p) {
            bool in_use =
                e.parts[p].params[static_cast<std::size_t>(ParamId::kDrive)] !=
                0.0f;
            if (!in_use) {
                for (int slot = 0; slot < kModSlots; ++slot) {
                    const ModRoute &r = e.parts[p].routes[slot];
                    if (r.source != ModSourceId::kNone &&
                        r.dst.id == ParamId::kDrive) {
                        in_use = true;
                        break;
                    }
                }
            }
            e.drive_in_use[p] = in_use;
            if (in_use && !e.prev_drive_in_use[p]) {
                for (int v = 0; v < kNumVoices; ++v) {
                    if (e.voices[v].part == p) {
                        e.voices[v].shaper.xp = 0.0f;
                        e.voices[v].shaper.Fp = 0.0f;
                    }
                }
            }
            e.prev_drive_in_use[p] = in_use;
        }

        for (int v = 0; v < kNumVoices; ++v) {
            Voice *voice = &e.voices[v];
            if (voice->stage == Voice::Stage::kIdle) continue;
            const Part *part = &e.parts[voice->part];

            UpdateEnvelope(voice, n, part);

            // Matrix evaluation (control rate): fold the 16 routes into
            // effective amp / cutoff / pitch for this voice.
            float amp_eff =
                part->params[static_cast<std::size_t>(ParamId::kAmp)];
            float cutoff_eff =
                part->params[static_cast<std::size_t>(ParamId::kCutoff)];
            float pitch_semitones =
                (part->params[static_cast<std::size_t>(ParamId::kPitchCoarse)] -
                 0.5f) *
                48.0f;
            float pitch_route = 0.0f;
            float drive_eff =
                part->params[static_cast<std::size_t>(ParamId::kDrive)];
            for (int slot = 0; slot < kModSlots; ++slot) {
                const ModRoute &r = part->routes[slot];
                if (r.source == ModSourceId::kNone) continue;  // empty slot
                // kNote (key follow) never accumulates: its octave offset is
                // applied exponentially in Hz via the named key_follow_depth
                // field (source-level exception, arch-design §5.3).
                if (r.source == ModSourceId::kNote) continue;
                const float src = ReadSource(r.source, part, voice);
                const float contrib = r.amount * src;

                float *acc;
                switch (r.dst.id) {
                case ParamId::kAmp:         acc = &amp_eff; break;
                case ParamId::kCutoff:      acc = &cutoff_eff; break;
                case ParamId::kPitchCoarse: acc = &pitch_route; break;
                case ParamId::kDrive:       acc = &drive_eff; break;
                default:                    continue;  // deferred destination
                }

                switch (k_params[static_cast<std::size_t>(r.dst.id)].comb) {
                case CombinationClass::kMultiplicative: {
                    const float factor =
                        kSourceBipolar[static_cast<std::size_t>(r.source)]
                            ? 1.0f + contrib
                            : 1.0f + r.amount * (src - 1.0f);
                    *acc *= factor;
                    break;
                }
                case CombinationClass::kAdditive:
                case CombinationClass::kExponential:
                    *acc += contrib;
                    break;
                }
            }
            if (cutoff_eff > 1.0f) cutoff_eff = 1.0f;
            if (cutoff_eff < 0.0f) cutoff_eff = 0.0f;
            if (drive_eff > 1.0f) drive_eff = 1.0f;
            if (drive_eff < 0.0f) drive_eff = 0.0f;
            const float depth = drive_eff;            // blend [0,1], 0 = dry
            const float gain = DriveCurve(drive_eff); // input gain, unity at 0
            const float pitch_factor =
                std::exp2f((pitch_semitones + pitch_route) / 12.0f);
            const float key_follow_factor =
                part->key_follow_depth * voice->key_follow;

            UpdateFilterCoeffs(voice, part, cutoff_eff, key_follow_factor);

            const float base_inc = voice->inc;
            voice->inc = base_inc * pitch_factor;
            for (int i = 0; i < n; ++i) {
                float saw = DspOscTick(voice);
                float lp = DspSvfTick(voice, saw);
                if (e.drive_in_use[voice->part]) {
                    const float wet = ShaperProcess(voice, lp * gain);
                    e.buses[0].L[start + i] += Lerp(lp, wet, depth) * amp_eff;
                } else {
                    e.buses[0].L[start + i] += lp * amp_eff;
                }
            }
            voice->inc = base_inc;
        }
    }

    // Bus protection (output-stage arch-design §6.2): bus gain -> soft-saturation
    // saturator -> hard clamp, tracking the pre-saturator peak into the meter.
    float block_peak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float s = e.buses[0].L[i] * kBusGain;
        const float a = std::fabs(s);
        if (a > block_peak) block_peak = a;
        out[i] = Clamp(CurveEval(CurveShape::kSoftSat, s));
    }
    // Block end: merge the pre-saturator peak into the shared meter with a
    // relaxed CAS-max. The `block_peak > cur` guard is NaN-safe: a NaN peak
    // fails the comparison and is dropped (output-stage arch-design §8).
    float cur = e.ipc->meter.load(std::memory_order_relaxed);
    while (block_peak > cur &&
           !e.ipc->meter.compare_exchange_weak(cur, block_peak,
                                               std::memory_order_relaxed)) {
    }
}

}  // namespace

void Render(EngineAudio &e, float *out, int frames) {
    for (int done = 0; done < frames; done += kBlockSize) {
        int n = kBlockSize;
        if (done + n > frames) n = frames - done;
        RenderBlock(e, out + done, n);
    }
}

}  // namespace engine
