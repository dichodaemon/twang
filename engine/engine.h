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

// Parameter identifier; defined in params.h.
enum class ParamId : std::uint8_t;

/// One part: the shared, per-part parameter bank addressed by the descriptor
/// table. Every voice bound to a part reads the same bank, so a part is a
/// timbre — one cutoff/resonance/envelope shaping a set of simultaneous notes.
///
/// Plain old data (memcpy-able); lives in a fixed array in TCM on the target.
struct Part {
    // Parameters (all normalized 0..1; see params.h)
    float cutoff;             ///< Filter cutoff.
    float resonance;          ///< Filter resonance.
    float filter_env_amount;  ///< Filter envelope depth.
    float attack;             ///< Attack time.
    float decay;              ///< Decay time.
    float sustain;            ///< Sustain level.
    float release;            ///< Release time.
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

    float steal_freq;   ///< Pending note frequency while ramping down (kSteal).
    std::uint8_t part;  ///< Owning part index (into the parts array).
};

/// @brief Initialize the engine and reset the voice to its defaults.
/// Call from the control thread before the audio thread starts.
void EngineInit();

/// @brief Queue a note-on (control thread).
/// @param part Part index in [0, kNumParts).
/// @param freq_hz Note frequency in Hz.
void EngineNoteOn(int part, float freq_hz);

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

/// @brief Render `frames` mono samples into `out` (audio thread).
///
/// Drains pending events and parameters at each block boundary. Output is
/// the sum of all active voices, clamped to [-1, 1].
/// @param out Destination buffer (holds at least `frames` floats).
/// @param frames Number of samples to render.
void Render(float *out, int frames);

}  // namespace engine
