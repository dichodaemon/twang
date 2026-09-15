/// @file engine.h
/// @brief Multi-voice audio engine: polyBLEP saw → TPT SVF → ADSR, across a
/// fixed pool of voices bound to independent parts.
///
/// The engine is split across two threads. The control thread drives an
/// EngineControl (notes, parameters, routes); the audio thread calls
/// Render(EngineAudio&, ...), which drains the pending events and parameters
/// at each block boundary. The inter-thread transport (event ring +
/// double-buffered parameters, in ipc.h) is swapped for the M33↔M85 mailbox on
/// the target; the boundary contract stays the same.

#pragma once

#include <cstdint>

namespace engine {

/// Audio sample rate, in Hz.
inline constexpr int kSampleRate = 48000;

/// Samples per processing block.
inline constexpr int kBlockSize = 64;

/// One control step per this many audio samples.
inline constexpr int kControlDecimation = 16;

/// Number of timbre slots (independent parameter banks).
inline constexpr int kNumParts = 4;

/// Number of concurrent voices (the fixed, statically-allocated pool).
inline constexpr int kNumVoices = 24;

/// Identifies a synthesizer parameter (see params.h for the descriptor table).
///
/// The first kNumParams members map 1:1 to Part::params[] indices (the flat
/// normalized parameter bank). kKeyFollowDepth addresses a named Part field
/// via offsetof and is NOT part of params[].
enum class ParamId : std::uint8_t {
    kCutoff = 0,       ///< Filter cutoff (params[0]).
    kResonance,        ///< Filter resonance (params[1]).
    kAttack,           ///< Attack time (params[2]).
    kDecay,            ///< Decay time (params[3]).
    kSustain,          ///< Sustain level (params[4]).
    kRelease,          ///< Release time (params[5]).
    kAmp,              ///< Amp/level base (params[6]).
    kPitchCoarse,      ///< Osc pitch coarse, bipolar ±24 semitones (params[7]).
    kPitchBend,        ///< Pitchbend performance input, 0.5 = center (params[8]).
    kDrive,            ///< Drive blend depth, [0,1] (params[9]).
    kKeyFollowDepth,   ///< Key-follow depth, [0,1] (named field; default 0.5).
    kCount,            ///< Parameter count (not a parameter).
};

/// A fully-qualified parameter address: the module instance and the parameter
/// kind. `ParamId` names a kind; the instance disambiguates per-module
/// occurrences (osc 0-3, env 0-2, lfo 0-2). Single-instance modules use 0.
///
/// Packs to 16 bits; this size is part of the persisted route-destination
/// format (`ModRoute.dst`), so it is asserted, not assumed.
struct ParamRef {
    std::uint8_t instance;  ///< 0 for single-instance modules
    ParamId      id;        ///< parameter kind
};
static_assert(sizeof(ParamRef) == 2,
              "ParamRef must stay 16-bit: it is the persisted route format");

/// How a destination combines its base value with accumulated modulation.
enum class CombinationClass : std::uint8_t {
    kAdditive,        ///< base + sum(amount*source).
    kMultiplicative,  ///< base * prod(1+amount*(src-1)) (unipolar) or prod(1+amount*src) (bipolar).
    kExponential,     ///< base * 2^(sum(amount*source)).
};

/// The single modulation-fold operator, shared by the audio path
/// (engine_audio.cc) and the panel (modulation band / summary lines). Folds
/// one route's contribution into a destination's accumulated value.
///
/// kAdditive and kExponential accumulate identically: `acc + amount*src`. The
/// classes diverge only at consumption — an additive destination feeds the
/// value straight to DSP, while an exponential destination linearizes it once
/// (`base * 2^sum`) at the DSP boundary. That linearization is NOT done here:
/// it is an `exp2f` that must run once per destination per voice, not once per
/// route, so it stays at the caller (pitch: `SemitonesToFactor` in
/// engine_audio.cc).
///
/// kMultiplicative selects its sub-form by source polarity: a unipolar source
/// attenuates (`× (1 + amount·(src−1))`); a bipolar source tremolos around the
/// base (`× (1 + amount·src)`).
inline float Fold(CombinationClass comb, float acc, float amount, float src,
                  bool src_bipolar) {
    switch (comb) {
        case CombinationClass::kMultiplicative:
            return acc * (src_bipolar ? (1.0f + amount * src)
                                      : (1.0f + amount * (src - 1.0f)));
        case CombinationClass::kAdditive:
        case CombinationClass::kExponential:
            return acc + amount * src;
    }
    return acc;  // unreachable; exhaustive over the 3-value enum
}

/// Identifies a modulation source.
enum class ModSourceId : std::uint8_t {
    kNone = 0,       ///< empty-slot sentinel; zero-init marks a slot empty.
    kVelocity,       ///< per-note velocity (velocity / 127).
    kNote,           ///< per-note key follow (octaves from middle C).
    kGate,           ///< per-note gate (1 held, 0 released).
    kLfo0, kLfo1,    ///< per-voice LFOs.
    kLfo2,           ///< global per-part LFO.
    kEnv0,           ///< amp envelope.
    kEnv1,           ///< filter envelope.
    kEnv2,           ///< free mod envelope.
    kModWheel, kAftertouch, kPitchBend, kExpression,  ///< per-part performance.
    kRandom,         ///< per-note latched random.
    kConstant,       ///< static 1.0.
    kCount,          ///< Source count (not a source).
};

/// Polarity of each modulation source: true = bipolar (centered at 0, range
/// [-1, 1]), false = unipolar (range [0, 1]). Indexed by ModSourceId — must
/// stay in sync with the enum order above. Drives the multiplicative fold's
/// sub-form: a unipolar source attenuates (x(1 + amount*(src-1))), a bipolar
/// source tremolos around the base (x(1 + amount*src)).
inline constexpr bool kSourceBipolar[] = {
    false,  // kNone
    false,  // kVelocity
    true,   // kNote
    false,  // kGate
    true,   // kLfo0
    true,   // kLfo1
    true,   // kLfo2
    false,  // kEnv0
    false,  // kEnv1
    false,  // kEnv2
    false,  // kModWheel
    false,  // kAftertouch
    true,   // kPitchBend
    false,  // kExpression
    false,  // kRandom
    false,  // kConstant
};

/// Display names for a modulation source: `long_name` (full form) and
/// `short_name` (routing token). Indexed by ModSourceId — must stay in sync
/// with the enum order above (like kSourceBipolar).
struct SourceDesc {
    const char *long_name;
    const char *short_name;
};
inline constexpr SourceDesc k_sources[] = {
    {"", ""},                // kNone
    {"VELOCITY", "VEL"},     // kVelocity
    {"KEY", "KEY"},          // kNote
    {"GATE", "GATE"},        // kGate
    {"LFO 1", "LFO1"},       // kLfo0
    {"LFO 2", "LFO2"},       // kLfo1
    {"LFO 3", "LFO3"},       // kLfo2
    {"ENVELOPE 1", "ENV1"},  // kEnv0
    {"ENVELOPE 2", "ENV2"},  // kEnv1
    {"ENVELOPE 3", "ENV3"},  // kEnv2
    {"MOD WHEEL", "MODW"},   // kModWheel
    {"AFTERTOUCH", "AT"},    // kAftertouch
    {"PITCH BEND", "BEND"},  // kPitchBend
    {"EXPRESSION", "EXPR"},  // kExpression
    {"RANDOM", "RAND"},      // kRandom
    {"CONSTANT", "CONST"},   // kConstant
};

/// One source->destination modulation route with a signed amount.
struct ModRoute {
    ModSourceId source = ModSourceId::kNone;  ///< kNone == empty slot.
    ParamRef dst;                              ///< destination; valid only when source != kNone.
    float amount = 0.0f;                       ///< signed; 0 == "present but silent".
};

/// Number of modulation route slots per part.
inline constexpr int kModSlots = 16;

/// Number of float params[] members per part.
inline constexpr int kNumParams = 10;

/// Number of output buses.
inline constexpr int kNumBuses = 1;

/// One part: the shared, per-part parameter bank addressed by the descriptor
/// table. Every voice bound to a part reads the same bank, so a part is a
/// timbre — one cutoff/resonance/envelope shaping a set of simultaneous notes.
///
/// Plain old data (memcpy-able); lives in a fixed array in TCM on the target.
struct Part {
    // Parameters (all normalized 0..1; see params.h). The first kNumParams
    // floats are the flat params[] bank: cutoff, resonance, attack, decay,
    // sustain, release, amp, pitch_coarse, pitchbend, drive.
    // key_follow_depth is a named field addressed via offsetof (not params[]).
    float params[kNumParams];
    float key_follow_depth;     ///< Key-follow depth for kNote→cutoff, [0,1], default 0.5.
    ModRoute routes[kModSlots]; ///< Modulation routes; zero-init == all empty.
};

/// Two floats of per-voice ADAA state for the drive shaper. Reset to {0, 0}
/// on note-on, steal, and drive enable so the shaper never resumes from stale
/// state (see the output-stage arch-design §10).
struct ShaperState {
    float xp;  ///< x[n-1]: previous shaper input.
    float Fp;  ///< F(x[n-1]): previous antiderivative value.
};

/// The fixed shaper curve and its antiderivative. The shape index is reserved
/// for a future curve set; only one shape ships (output-stage arch-design §6.3).
enum class CurveShape : std::uint8_t { kSoftSat = 0 };

/// f(x): the clamped Padé soft-saturation curve (output-stage arch-design §6.3).
float CurveEval(CurveShape s, float x);

/// F(x): the shifted antiderivative of CurveEval, via table + asymptote
/// (F(0) = 0).
float AntiderivativeEval(CurveShape s, float x);

/// Fixed bus headroom scale, −18 dB (output-stage arch-design §6.2).
inline constexpr float kBusGain = 0.125f;

/// Antiderivative table entries.
inline constexpr int kFTableSize = 256;

/// The F-table covers x ∈ [−kFTableMax, kFTableMax]; outside it the closed-form
/// asymptote |x| + kFTableAsym is used.
inline constexpr float kFTableMax = 3.0f;

/// Asymptote constant: F(x) = |x| + kFTableAsym for |x| > kFTableMax.
inline constexpr float kFTableAsym = -0.651607519f;

/// ADAA divide fallback threshold: one table cell (derived, not a magic
/// constant). Routes the |dx| < h staircase regime to the exact midpoint
/// fallback, and is CPU-optimal (the fallback is cheaper than the quotient).
/// Re-derive if the path moves to Q31 fixed point.
inline constexpr float kAdaaEps = 2.0f * kFTableMax / (kFTableSize - 1);

/// The shifted antiderivative F(x) = x²/18 + (4/3)·ln((x²+3)/3) over
/// x ∈ [−kFTableMax, kFTableMax] (F(0) = 0), 256 entries, linear
/// interpolation (output-stage arch-design §6.4). The values are authored
/// literals (C++17 has no constexpr log); test_shaper validates them
/// against a float64 reference.
inline constexpr float kFTable[kFTableSize] = {
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

/// How long a stolen voice ramps its envelope down before retriggering the
/// new note. A few ms: long enough to avoid a click, short enough to feel
/// instant.
inline constexpr float kStealTime = 0.005f;  // 5 ms

/// One synthesizer voice: the per-note DSP state, bound to a part.
///
/// Plain old data: trivially copyable and standard-layout, with no heap
/// pointers and no virtual table. This lets a whole voice live in
/// tightly-coupled memory (TCM) on the target and be copied with `memcpy`.
struct Voice {
    /// Envelope stage.
    enum class Stage : std::uint8_t {
        kIdle,      ///< No note.
        kAttack,    ///< Ramping up.
        kDecay,     ///< Ramping down to the sustain level.
        kSustain,   ///< Holding.
        kRelease,   ///< Ramping down to zero.
        kSteal,     ///< Fast ramp down, then retrigger a stolen voice.
    };

    // Oscillator
    float phase;  ///< Oscillator phase in [0, 1).
    float inc;    ///< Phase increment per sample.

    // TPT SVF (Zavalishin/Simper)
    float g, k, a1, a2, a3;  ///< Filter coefficients.
    float ic1eq, ic2eq;      ///< Filter integrator state.

    // Envelope
    float env;      ///< Current envelope level in [0, 1].
    float env_inc;  ///< Per-sample envelope increment.
    Stage stage;    ///< Envelope stage.

    float vel;          ///< Per-note velocity (raw 1..127); REPLACES gain.
    float note;         ///< Per-note MIDI note number (integer-valued float).
    float key_follow;   ///< Per-note octave offset from C4 (latched at note-on).
    float gate;         ///< Per-note gate (1 held, 0 released).
    float steal_freq;   ///< Pending note frequency while ramping down (kSteal).
    float steal_vel;    ///< Pending note velocity while ramping down (kSteal).
    std::uint8_t part;  ///< Owning part index (into the parts array).

    // Filter-coefficient dirtiness: the effective cutoff, key-follow factor,
    // and Q the SVF coefficients were last computed for. UpdateFilterCoeffs
    // skips the pow/tan recompute when all are unchanged. -1 on the cutoff is
    // the "never matches" sentinel StartNote sets to force the first recompute
    // of a note.
    float last_env_cutoff;
    float last_key_follow;
    float last_q;

    ShaperState shaper;  ///< Drive-shaper ADAA state; reset on note-on/steal/enable.
};

/// @brief Run one sample of the drive shaper (first-order ADAA) for a voice.
///
/// Precondition: x = lp × gain (the driven input); v->shaper holds valid state
/// (reset on note-on/steal/enable). Postcondition: returns the ADAA-anti-aliased
/// f(x) and advances v->shaper to {x, F(x)}. The caller skips this entirely when
/// drive is not in use for the part (output-stage arch-design §9).
/// @param v Voice whose ADAA state to use and advance.
/// @param x Driven input sample.
/// @return Anti-aliased shaper output.
float ShaperProcess(Voice *v, float x);

}  // namespace engine
