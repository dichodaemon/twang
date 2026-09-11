/// @file engine.h
/// @brief Multi-voice audio engine: polyBLEP saw → TPT SVF → ADSR, across a
/// fixed pool of voices bound to independent parts.
///
/// The engine is split across two threads. The control thread calls
/// EngineNoteOn / EngineNoteOff / EngineSetParam; the audio thread calls
/// Render, which drains the pending events and parameters at each block
/// boundary. The inter-thread transport (event ring + double-buffered
/// parameters, in ipc.h) is swapped for the M33↔M85 mailbox on the target;
/// the boundary contract stays the same.

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
    kKeyFollowDepth,   ///< Key-follow depth, [0,1] (named field).
    kCount,            ///< Parameter count (not a parameter).
};

/// How a destination combines its base value with accumulated modulation.
enum class CombinationClass : std::uint8_t {
    kAdditive,        ///< base + sum(amount*source).
    kMultiplicative,  ///< base * prod(amount*source) (unipolar) or prod(1+amount*source) (bipolar).
    kExponential,     ///< base * 2^(sum(amount*source)).
};

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
};

/// One source->destination modulation route with a signed amount.
struct ModRoute {
    ModSourceId source = ModSourceId::kNone;  ///< kNone == empty slot.
    ParamId destination;                      ///< valid only when source != kNone.
    float amount = 0.0f;                      ///< signed; 0 == "present but silent".
};

/// Number of modulation route slots per part.
inline constexpr int kModSlots = 16;

/// Number of float params[] members per part (grows in phases 2-4).
inline constexpr int kNumParams = 9;

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
    // sustain, release, amp, pitch_coarse, pitchbend. key_follow_depth is a
    // named field addressed via offsetof (not params[]).
    float params[kNumParams];
    float key_follow_depth;     ///< Key-follow depth for kNote→cutoff, [0,1], default 0.
    ModRoute routes[kModSlots]; ///< Modulation routes; zero-init == all empty.
};

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
};

/// @brief Initialize the engine: reset the parts to their defaults and the
/// voices to idle. Call from the control thread before the audio thread starts.
void EngineInit();

/// @brief Weak hook called after the control side queues events.
///
/// Defaults to a no-op. The target overrides it to notify the audio core that
/// events are pending (e.g. a mailbox signal on the M33→M85 channel). The
/// audio core drains the shared ring at its block boundary regardless; this is
/// the interrupt-driven notification path.
void EngineEventsPending();

/// @brief Queue a note-on (control thread).
/// @param part Part index in [0, kNumParts).
/// @param freq_hz Note frequency in Hz.
/// @param velocity MIDI velocity in [1, 127] (0 = note-off, handled upstream).
void EngineNoteOn(int part, float freq_hz, std::uint8_t velocity);

/// @brief Queue a note-off (control thread), releasing the voice playing
/// `freq_hz` in `part`.
/// @param part Part index in [0, kNumParts).
/// @param freq_hz Note frequency in Hz.
void EngineNoteOff(int part, float freq_hz);

/// @brief Set a parameter's normalized value for a part (control thread).
/// @param part Part index in [0, kNumParts).
/// @param id Parameter identifier.
/// @param norm Value in [0, 1].
void EngineSetParam(int part, ParamId id, float norm);

/// @brief Set a parameter from display units for a part (control thread).
/// @param part Part index in [0, kNumParts).
/// @param id Parameter identifier.
/// @param disp Display value.
void EngineSetParamDisp(int part, ParamId id, float disp);

/// @brief Read a parameter's current normalized value for a part (control
/// thread).
/// @param part Part index in [0, kNumParts).
/// @param id Parameter identifier.
/// @return Value in [0, 1].
float EngineGetParam(int part, ParamId id);

/// @brief Set one modulation route for a part (control thread).
/// @param part Part index in [0, kNumParts).
/// @param slot Route slot in [0, kModSlots).
/// @param src Modulation source; kNone clears the slot.
/// @param dst Destination parameter (phase-1 set: kCutoff, kAmp, kPitchCoarse).
/// @param amount Signed normalized amount in [-1, 1] (key follow [0, 1]).
void EngineSetRoute(int part, int slot, ModSourceId src, ParamId dst,
                    float amount);

/// @brief Render `frames` mono samples into `out` (audio thread).
///
/// Drains pending events and parameters at each block boundary. Output is
/// the sum of all active voices, clamped to [-1, 1].
/// @param out Destination buffer (holds at least `frames` floats).
/// @param frames Number of samples to render.
void Render(float *out, int frames);

}  // namespace engine
