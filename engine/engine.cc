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

// Per-part flag: is drive in use (base kDrive != 0 or a route targets it)?
// Computed per control step in RenderBlock; gates the per-voice shaper.
bool g_drive_in_use[kNumParts];
bool g_prev_drive_in_use[kNumParts];  // previous control step, for enable edges

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

// The shifted antiderivative F(x) = x²/18 + (4/3)·ln((x²+3)/3) over
// x ∈ [-kFTableMax, kFTableMax] (F(0) = 0), 256 entries, linear interpolation
// (output-stage arch-design §6.4). The values are authored literals (C++17 has
// no constexpr log); test_shaper validates them against a float64 reference.
constexpr float kFTable[kFTableSize] = {
    2.34839248f, 2.32486307f, 2.30133367f, 2.27780431f, 2.25427502f, 2.23074589f, 2.20721698f, 2.18368842f,
    2.16016032f, 2.13663285f, 2.11310617f, 2.0895805f, 2.06605605f, 2.0425331f, 2.01901192f, 1.99549283f,
    1.97197619f, 1.94846237f, 1.9249518f, 1.90144492f, 1.87794223f, 1.85444426f, 1.83095158f, 1.80746481f,
    1.7839846f, 1.76051166f, 1.73704675f, 1.71359066f, 1.69014426f, 1.66670845f, 1.64328421f, 1.61987255f,
    1.59647456f, 1.5730914f, 1.54972428f, 1.52637449f, 1.50304338f, 1.47973237f, 1.45644299f, 1.4331768f,
    1.40993548f, 1.38672079f, 1.36353455f, 1.3403787f, 1.31725528f, 1.29416639f, 1.27111427f, 1.24810125f,
    1.22512976f, 1.20220234f, 1.17932167f, 1.15649052f, 1.1337118f, 1.11098853f, 1.08832387f, 1.06572112f,
    1.04318369f, 1.02071516f, 0.998319241f, 0.975999786f, 0.95376081f, 0.931606477f, 0.909541111f, 0.887569199f,
    0.865695389f, 0.843924503f, 0.82226153f, 0.800711634f, 0.779280158f, 0.757972623f, 0.736794732f, 0.715752372f,
    0.694851615f, 0.67409872f, 0.653500134f, 0.633062493f, 0.612792622f, 0.592697532f, 0.572784426f, 0.553060689f,
    0.533533895f, 0.514211798f, 0.49510233f, 0.476213603f, 0.457553894f, 0.439131652f, 0.420955482f, 0.403034143f,
    0.38537654f, 0.367991714f, 0.350888835f, 0.334077188f, 0.317566167f, 0.301365256f, 0.285484023f, 0.2699321f,
    0.254719169f, 0.23985495f, 0.22534918f, 0.211211595f, 0.197451912f, 0.184079812f, 0.171104914f, 0.158536758f,
    0.146384783f, 0.134658302f, 0.123366481f, 0.112518315f, 0.102122605f, 0.0921879328f, 0.0827226379f, 0.0737347932f,
    0.0652321811f, 0.0572222701f, 0.0497121919f, 0.0427087183f, 0.0362182398f, 0.0302467445f, 0.024799798f, 0.0198825246f,
    0.0154995893f, 0.0116551824f, 0.00835300345f, 0.00559624923f, 0.0033876015f, 0.0017292176f, 0.000622722461f, 6.92027333e-05f,
    6.92027333e-05f, 0.000622722461f, 0.0017292176f, 0.0033876015f, 0.00559624923f, 0.00835300345f, 0.0116551824f, 0.0154995893f,
    0.0198825246f, 0.024799798f, 0.0302467445f, 0.0362182398f, 0.0427087183f, 0.0497121919f, 0.0572222701f, 0.0652321811f,
    0.0737347932f, 0.0827226379f, 0.0921879328f, 0.102122605f, 0.112518315f, 0.123366481f, 0.134658302f, 0.146384783f,
    0.158536758f, 0.171104914f, 0.184079812f, 0.197451912f, 0.211211595f, 0.22534918f, 0.23985495f, 0.254719169f,
    0.2699321f, 0.285484023f, 0.301365256f, 0.317566167f, 0.334077188f, 0.350888835f, 0.367991714f, 0.38537654f,
    0.403034143f, 0.420955482f, 0.439131652f, 0.457553894f, 0.476213603f, 0.49510233f, 0.514211798f, 0.533533895f,
    0.553060689f, 0.572784426f, 0.592697532f, 0.612792622f, 0.633062493f, 0.653500134f, 0.67409872f, 0.694851615f,
    0.715752372f, 0.736794732f, 0.757972623f, 0.779280158f, 0.800711634f, 0.82226153f, 0.843924503f, 0.865695389f,
    0.887569199f, 0.909541111f, 0.931606477f, 0.95376081f, 0.975999786f, 0.998319241f, 1.02071516f, 1.04318369f,
    1.06572112f, 1.08832387f, 1.11098853f, 1.1337118f, 1.15649052f, 1.17932167f, 1.20220234f, 1.22512976f,
    1.24810125f, 1.27111427f, 1.29416639f, 1.31725528f, 1.3403787f, 1.36353455f, 1.38672079f, 1.40993548f,
    1.4331768f, 1.45644299f, 1.47973237f, 1.50304338f, 1.52637449f, 1.54972428f, 1.5730914f, 1.59647456f,
    1.61987255f, 1.64328421f, 1.66670845f, 1.69014426f, 1.71359066f, 1.73704675f, 1.76051166f, 1.7839846f,
    1.80746481f, 1.83095158f, 1.85444426f, 1.87794223f, 1.90144492f, 1.9249518f, 1.94846237f, 1.97197619f,
    1.99549283f, 2.01901192f, 2.0425331f, 2.06605605f, 2.0895805f, 2.11310617f, 2.13663285f, 2.16016032f,
    2.18368842f, 2.20721698f, 2.23074589f, 2.25427502f, 2.27780431f, 2.30133367f, 2.32486307f, 2.34839248f,
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
    v->shaper.xp = 0.0f;  // fresh note: first sample computed against silence
    v->shaper.Fp = 0.0f;
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

        // Compute drive_in_use per part and reset the shaper state on a
        // false -> true enable edge. A ramped enable (depth ~= 0) needs no
        // reset, but a jumped enable (preset load, CC 0 -> 100) would resume
        // from stale ADAA state and click (output-stage arch-design §7).
        for (int p = 0; p < kNumParts; ++p) {
            bool in_use =
                g_parts[p].params[static_cast<std::size_t>(ParamId::kDrive)] !=
                0.0f;
            if (!in_use) {
                for (int slot = 0; slot < kModSlots; ++slot) {
                    const ModRoute &r = g_parts[p].routes[slot];
                    if (r.source != ModSourceId::kNone &&
                        r.destination == ParamId::kDrive) {
                        in_use = true;
                        break;
                    }
                }
            }
            g_drive_in_use[p] = in_use;
            if (in_use && !g_prev_drive_in_use[p]) {
                for (int v = 0; v < kNumVoices; ++v) {
                    if (g_voices[v].part == p) {
                        g_voices[v].shaper.xp = 0.0f;
                        g_voices[v].shaper.Fp = 0.0f;
                    }
                }
            }
            g_prev_drive_in_use[p] = in_use;
        }

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

                // Destination -> accumulator. The fold operator is read from
                // the destination's combination class so g_params stays the
                // single source of truth for how a destination combines
                // (arch-design §5.3); the switch only selects which accumulator
                // a destination folds into.
                float *acc;
                switch (r.destination) {
                case ParamId::kAmp:         acc = &amp_eff; break;
                case ParamId::kCutoff:      acc = &cutoff_eff; break;
                case ParamId::kPitchCoarse: acc = &pitch_route; break;
                case ParamId::kDrive:       acc = &drive_eff; break;
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
                if (g_drive_in_use[voice->part]) {
                    const float wet = ShaperProcess(voice, lp * gain);
                    g_buses[0].L[start + i] += Lerp(lp, wet, depth) * amp_eff;
                } else {
                    g_buses[0].L[start + i] += lp * amp_eff;
                }
            }
            voice->inc = base_inc;
        }
    }

    // Bus protection (output-stage arch-design §6.2): bus gain -> soft-saturation
    // saturator -> hard clamp, tracking the pre-saturator peak into the meter.
    float block_peak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float s = g_buses[0].L[i] * kBusGain;
        const float a = std::fabs(s);
        if (a > block_peak) block_peak = a;
        out[i] = Clamp(CurveEval(CurveShape::kSoftSat, s));
    }
    // Block end: merge the pre-saturator peak into the shared meter with a
    // relaxed CAS-max. The `block_peak > cur` guard is NaN-safe: a NaN peak
    // fails the comparison and is dropped (output-stage arch-design §8).
    float cur = Shared().meter.load(std::memory_order_relaxed);
    while (block_peak > cur &&
           !Shared().meter.compare_exchange_weak(cur, block_peak,
                                                 std::memory_order_relaxed)) {
    }
}

}  // namespace

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

float EngineGetMeter() {
    return Shared().meter.exchange(0.0f, std::memory_order_relaxed);
}

void EngineInit() {
    for (int i = 0; i < kNumParts; ++i) g_parts[i] = Part{};
    for (int i = 0; i < kNumVoices; ++i) g_voices[i] = Voice{};  // shaper {0,0}
    for (int p = 0; p < kNumParts; ++p) {
        g_drive_in_use[p] = false;
        g_prev_drive_in_use[p] = false;
    }
    Shared().meter.store(0.0f, std::memory_order_relaxed);
    g_alloc.Reset();
    g_events.Reset();
    g_param_block.Reset(g_params);

    // Pre-populate the 5 default routes (arch-design §5.4). Slots 0-2 absorb
    // today's hardcoded modulation (velocity->amp, env0->amp, env1->cutoff);
    // velocity->amp uses full depth (1.0). kAmp is a pure level (default 1.0)
    // — the polyphony headroom now lives on the bus (kBusGain 0.125), not in
    // kAmp. Slot 3 (key follow) is enabled at half depth (0.5) and slot 4
    // (pitchbend) is off (amount 0) so it contributes nothing at rest. Slot 3's
    // amount is a seed only — key-follow depth is read from the named
    // Part::key_follow_depth field, not this route's amount.
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
    // dst must be a destination the matrix folds. Named fields
    // (kKeyFollowDepth), performance inputs (kPitchBend), and kCount are not
    // destinations; reject them so a stored route can never silently do nothing.
    switch (dst) {
    case ParamId::kCutoff:
    case ParamId::kAmp:
    case ParamId::kPitchCoarse:
    case ParamId::kDrive:
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
