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

// Per-block stereo accumulation. Phase 1 is mono: voices accumulate into
// g_buses[0].L and Render downmixes to the mono `out`; the R side and the
// per-voice pan/level/send taps are phase 4.
struct Bus {
    float L[kBlockSize];
    float R[kBlockSize];
};
Bus g_buses[kNumBuses];

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
                   &g_params[static_cast<std::size_t>(ParamId::kCutoff)],
                   cutoff_norm_eff) *
               std::exp2f(key_follow_factor);
    const float fc_max = g_params[static_cast<std::size_t>(ParamId::kCutoff)].disp_max;
    if (fc > fc_max) fc = fc_max;
    DspSvfSetFq(v, fc, q);
}

float Clamp(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

constexpr float kLn2 = 0.6931471805599453f;

// The antiderivative F(x) = log(cosh(x)) over x ∈ [-kFTableMax, kFTableMax],
// 256 entries, linear interpolation (output-stage arch-design §6.4). The
// values are authored literals (C++17 has no constexpr log/cosh); test_shaper
// validates them against a float64 closed-form reference.
constexpr float kFTable[kFTableSize] = {
    7.30685293f, 7.24410785f, 7.18136277f, 7.11861769f, 7.05587261f, 6.99312754f, 6.93038247f, 6.8676374f,
    6.80489234f, 6.74214729f, 6.67940223f, 6.61665719f, 6.55391215f, 6.49116712f, 6.4284221f, 6.36567709f,
    6.30293209f, 6.2401871f, 6.17744213f, 6.11469718f, 6.05195224f, 5.98920733f, 5.92646244f, 5.86371758f,
    5.80097275f, 5.73822796f, 5.67548321f, 5.6127385f, 5.54999385f, 5.48724926f, 5.42450473f, 5.36176029f,
    5.29901592f, 5.23627166f, 5.17352751f, 5.11078348f, 5.0480396f, 4.98529588f, 4.92255235f, 4.85980902f,
    4.79706593f, 4.73432311f, 4.67158059f, 4.60883842f, 4.54609664f, 4.48335531f, 4.42061447f, 4.35787421f,
    4.29513459f, 4.23239571f, 4.16965766f, 4.10692055f, 4.0441845f, 3.98144967f, 3.91871621f, 3.85598431f,
    3.79325417f, 3.73052602f, 3.66780015f, 3.60507685f, 3.54235646f, 3.47963937f, 3.41692603f, 3.35421693f,
    3.29151264f, 3.22881381f, 3.16612116f, 3.10343552f, 3.04075782f, 2.97808913f, 2.91543065f, 2.85278375f,
    2.79014996f, 2.72753103f, 2.66492896f, 2.60234599f, 2.53978466f, 2.47724786f, 2.41473886f, 2.35226136f,
    2.28981955f, 2.22741818f, 2.16506263f, 2.10275897f, 2.04051411f, 1.97833582f, 1.91623293f, 1.85421541f,
    1.79229453f, 1.73048302f, 1.66879529f, 1.60724759f, 1.54585825f, 1.48464796f, 1.42364003f, 1.36286073f,
    1.3023396f, 1.24210986f, 1.18220879f, 1.12267824f, 1.06356502f, 1.00492152f, 0.946806153f, 0.889284007f,
    0.832427384f, 0.776316403f, 0.72103958f, 0.66669438f, 0.613387713f, 0.561236352f, 0.510367225f, 0.460917553f,
    0.413034781f, 0.366876249f, 0.322608554f, 0.280406556f, 0.240451973f, 0.202931538f, 0.168034689f, 0.135950813f,
    0.106866049f, 0.080959742f, 0.0584006355f, 0.0393429373f, 0.023922434f, 0.0122528347f, 0.00442254227f, 0.00049203771f,
    0.00049203771f, 0.00442254227f, 0.0122528347f, 0.023922434f, 0.0393429373f, 0.0584006355f, 0.080959742f, 0.106866049f,
    0.135950813f, 0.168034689f, 0.202931538f, 0.240451973f, 0.280406556f, 0.322608554f, 0.366876249f, 0.413034781f,
    0.460917553f, 0.510367225f, 0.561236352f, 0.613387713f, 0.66669438f, 0.72103958f, 0.776316403f, 0.832427384f,
    0.889284007f, 0.946806153f, 1.00492152f, 1.06356502f, 1.12267824f, 1.18220879f, 1.24210986f, 1.3023396f,
    1.36286073f, 1.42364003f, 1.48464796f, 1.54585825f, 1.60724759f, 1.66879529f, 1.73048302f, 1.79229453f,
    1.85421541f, 1.91623293f, 1.97833582f, 2.04051411f, 2.10275897f, 2.16506263f, 2.22741818f, 2.28981955f,
    2.35226136f, 2.41473886f, 2.47724786f, 2.53978466f, 2.60234599f, 2.66492896f, 2.72753103f, 2.79014996f,
    2.85278375f, 2.91543065f, 2.97808913f, 3.04075782f, 3.10343552f, 3.16612116f, 3.22881381f, 3.29151264f,
    3.35421693f, 3.41692603f, 3.47963937f, 3.54235646f, 3.60507685f, 3.66780015f, 3.73052602f, 3.79325417f,
    3.85598431f, 3.91871621f, 3.98144967f, 4.0441845f, 4.10692055f, 4.16965766f, 4.23239571f, 4.29513459f,
    4.35787421f, 4.42061447f, 4.48335531f, 4.54609664f, 4.60883842f, 4.67158059f, 4.73432311f, 4.79706593f,
    4.85980902f, 4.92255235f, 4.98529588f, 5.0480396f, 5.11078348f, 5.17352751f, 5.23627166f, 5.29901592f,
    5.36176029f, 5.42450473f, 5.48724926f, 5.54999385f, 5.6127385f, 5.67548321f, 5.73822796f, 5.80097275f,
    5.86371758f, 5.92646244f, 5.98920733f, 6.05195224f, 6.11469718f, 6.17744213f, 6.2401871f, 6.30293209f,
    6.36567709f, 6.4284221f, 6.49116712f, 6.55391215f, 6.61665719f, 6.67940223f, 6.74214729f, 6.80489234f,
    6.8676374f, 6.93038247f, 6.99312754f, 7.05587261f, 7.11861769f, 7.18136277f, 7.24410785f, 7.30685293f,
};

// Per-sample envelope increment to cover `delta` over `time_s` seconds.
// A non-positive time is instant: returns 0 and the caller jumps the level.
float EnvInc(float delta, float time_s) {
    if (time_s <= 0.0f) return 0.0f;
    return delta / (time_s * kSampleRate);
}

// How long a stolen voice ramps its envelope down before retriggering the new
// note. A few ms: long enough to avoid a click, short enough to feel instant.
constexpr float kStealTime = 0.005f;  // 5 ms

// Move from the attack peak into decay, skipping straight to sustain if the
// decay time is zero (instant).
void EnterDecay(Voice *v, const Part *p) {
    v->env = 1.0f;
    float decay_s = ParamGetDisp(p, ParamId::kDecay);
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

void StartNote(Voice *v, float freq_hz, float velocity, const Part *p);  // defined below

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
            StartNote(v, v->steal_freq, v->steal_vel, p);  // ramp done: retrigger the new note
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
    v->gate = 0.0f;
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
void StealNote(Voice *v, float freq_hz, float velocity, std::uint8_t part) {
    v->part = part;
    if (v->stage == Voice::Stage::kIdle || v->env <= 0.0f) {
        StartNote(v, freq_hz, velocity, &g_parts[part]);  // nothing to ramp: start now
        return;
    }
    v->steal_freq = freq_hz;
    v->steal_vel = velocity;
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
            StartNote(v, e.freq, e.velocity, &g_parts[e.part]);
        } else if (e.type == Event::Type::kSteal) {
            if (e.part >= kNumParts) continue;
            StealNote(v, e.freq, e.velocity, e.part);
        } else {
            ReleaseNote(v, &g_parts[v->part]);
        }
    }
}

void RenderBlock(float *out, int frames) {
    g_param_block.Commit(g_parts);  // snapshot params into all parts

    for (int i = 0; i < frames; ++i) {
        g_buses[0].L[i] = 0.0f;
        g_buses[0].R[i] = 0.0f;
    }

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
            for (int slot = 0; slot < kModSlots; ++slot) {
                const ModRoute &r = part->routes[slot];
                if (r.source == ModSourceId::kNone) continue;  // empty slot
                // kNote (key follow) never accumulates: its octave offset is
                // applied exponentially in Hz via the named key_follow_depth
                // field (source-level exception, arch-design §5.3).
                if (r.source == ModSourceId::kNote) continue;
                const float src = ReadSource(r.source, part, voice);
                const float contrib = r.amount * src;

                // Destination -> accumulator. The fold operator is read from
                // the destination's combination class so g_params stays the
                // single source of truth for how a destination combines
                // (arch-design §5.3); the switch only selects which accumulator
                // a destination folds into (grows in phases 2-4).
                float *acc;
                switch (r.destination) {
                case ParamId::kAmp:         acc = &amp_eff; break;
                case ParamId::kCutoff:      acc = &cutoff_eff; break;
                case ParamId::kPitchCoarse: acc = &pitch_route; break;
                default:                    continue;  // deferred destination
                }

                switch (g_params[static_cast<std::size_t>(r.destination)].comb) {
                case CombinationClass::kMultiplicative: {
                    // A unipolar source attenuates (x(1 + amount*(src-1))), a
                    // bipolar source tremolos around the base (x(1 + amount*src)).
                    // amount=0 is neutral in both.
                    const float factor =
                        kSourceBipolar[static_cast<std::size_t>(r.source)]
                            ? 1.0f + contrib
                            : 1.0f + r.amount * (src - 1.0f);
                    *acc *= factor;
                    break;
                }
                case CombinationClass::kAdditive:
                case CombinationClass::kExponential:
                    // Both accumulate by sum; exponential applies exp2f after
                    // the loop (pitch), additive clamps after the loop (cutoff).
                    *acc += contrib;
                    break;
                }
            }
            if (cutoff_eff > 1.0f) cutoff_eff = 1.0f;
            if (cutoff_eff < 0.0f) cutoff_eff = 0.0f;
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
                g_buses[0].L[start + i] += lp * amp_eff;
            }
            voice->inc = base_inc;
        }
    }

    for (int i = 0; i < frames; ++i) out[i] = Clamp(g_buses[0].L[i]);
}

}  // namespace

float CurveEval(CurveShape s, float x) {
    switch (s) {
    case CurveShape::kSoftSat:
        return ::tanhf(x);
    }
    return 0.0f;
}

float AntiderivativeEval(CurveShape s, float x) {
    switch (s) {
    case CurveShape::kSoftSat: {
        const float ax = std::fabs(x);
        // Outside the table the closed-form asymptote |x| - log 2 is exact to
        // float precision. The >= (not >) also keeps |x| == kFTableMax out of
        // the interpolation so the last slot never reads i+1 out of bounds.
        if (ax >= kFTableMax) return ax - kLn2;
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

void EngineInit() {
    for (int i = 0; i < kNumParts; ++i) g_parts[i] = Part{};
    for (int i = 0; i < kNumVoices; ++i) g_voices[i] = Voice{};
    g_alloc.Reset();
    g_events.Reset();
    g_param_block.Reset(g_params);

    // Pre-populate the 5 default routes (arch-design §5.4). Slots 0-2 absorb
    // today's hardcoded modulation (velocity->amp, env0->amp, env1->cutoff);
    // velocity->amp uses full depth (1.0) with the 4-voice headroom carried in
    // the kAmp base level (default 0.25). Slot 3 (key follow) is enabled at
    // half depth (0.5) and slot 4 (pitchbend) is off (amount 0) so it
    // contributes nothing at rest. Slot 3's amount is a seed only — key-follow
    // depth is read from the named Part::key_follow_depth field, not this
    // route's amount.
    for (int p = 0; p < kNumParts; ++p) {
        EngineSetRoute(p, 0, ModSourceId::kVelocity, ParamId::kAmp, 1.0f);
        EngineSetRoute(p, 1, ModSourceId::kEnv0, ParamId::kAmp, 1.0f);
        EngineSetRoute(p, 2, ModSourceId::kEnv1, ParamId::kCutoff, 0.0f);
        EngineSetRoute(p, 3, ModSourceId::kNote, ParamId::kCutoff, 0.5f);
        EngineSetRoute(p, 4, ModSourceId::kPitchBend, ParamId::kPitchCoarse, 0.0f);
    }
    g_param_block.Commit(g_parts);
}

__attribute__((weak)) void EngineEventsPending() {}

void EngineNoteOn(int part, float freq_hz, std::uint8_t velocity) {
    const Allocator::Decision d = g_alloc.NoteOn(part, freq_hz);
    if (d.voice < 0) return;  // dropped: full and nothing to steal
    const Event::Type type =
        d.steal ? Event::Type::kSteal : Event::Type::kNoteOn;
    g_events.Push({type, static_cast<std::uint8_t>(part),
                   static_cast<std::uint8_t>(d.voice), velocity, freq_hz});
    EngineEventsPending();
}

void EngineNoteOff(int part, float freq_hz) {
    const int voice = g_alloc.NoteOff(part, freq_hz);
    if (voice < 0) return;  // no matching note
    g_events.Push({Event::Type::kNoteOff, static_cast<std::uint8_t>(part),
                   static_cast<std::uint8_t>(voice), 0, 0.0f});
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

bool EngineSetRoute(int part, int slot, ModSourceId src, ParamId dst,
                    float amount) {
    if (part < 0 || part >= kNumParts) return false;
    if (slot < 0 || slot >= kModSlots) return false;
    // dst must be a phase-1 destination the matrix folds. Named fields
    // (kKeyFollowDepth), performance inputs (kPitchBend), and kCount are not
    // destinations; resonance / envelope-time / send destinations land in
    // phases 2-4. Reject them so a stored route can never silently do nothing.
    switch (dst) {
    case ParamId::kCutoff:
    case ParamId::kAmp:
    case ParamId::kPitchCoarse:
        break;
    default:
        return false;
    }
    g_param_block.SetRoute(part, slot, src, dst, amount);
    return true;
}

void Render(float *out, int frames) {
    for (int done = 0; done < frames; done += kBlockSize) {
        int n = kBlockSize;
        if (done + n > frames) n = frames - done;
        RenderBlock(out + done, n);
    }
}

}  // namespace engine
