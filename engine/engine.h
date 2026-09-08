/// @file engine.h
/// @brief Single-voice audio engine: polyBLEP saw → TPT SVF → ADSR.
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

// Parameter identifier; defined in params.h.
enum class ParamId : std::uint8_t;

/// One synthesizer voice.
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

    // Parameters (all normalized 0..1; see params.h)
    float cutoff;             ///< Filter cutoff.
    float resonance;          ///< Filter resonance.
    float filter_env_amount;  ///< Filter envelope depth.
    float attack;             ///< Attack time.
    float decay;              ///< Decay time.
    float sustain;            ///< Sustain level.
    float release;            ///< Release time.

    bool gate;  ///< True while the note is held.
};

/// @brief Initialize the engine and reset the voice to its defaults.
/// Call from the control thread before the audio thread starts.
void EngineInit();

/// @brief Queue a note-on (control thread).
/// @param freq_hz Note frequency in Hz.
void EngineNoteOn(float freq_hz);

/// @brief Queue a note-off (control thread).
void EngineNoteOff();

/// @brief Set a parameter's normalized value (control thread).
/// @param id Parameter identifier.
/// @param norm Value in [0, 1].
void EngineSetParam(ParamId id, float norm);

/// @brief Set a parameter from display units (control thread).
/// @param id Parameter identifier.
/// @param disp Display value.
void EngineSetParamDisp(ParamId id, float disp);

/// @brief Render `frames` mono samples into `out` (audio thread).
///
/// Drains pending events and parameters at each block boundary. Output is
/// finite and clamped to [-1, 1].
/// @param out Destination buffer (holds at least `frames` floats).
/// @param frames Number of samples to render.
void Render(float *out, int frames);

}  // namespace engine
