/// @file engine.h
/// @brief Single-voice audio engine: polyBLEP saw → TPT SVF → ADSR.

#pragma once

#include <cstdint>

namespace engine {

/// Audio sample rate, in Hz.
inline constexpr int kSampleRate = 48000;

/// Samples per processing block.
inline constexpr int kBlockSize = 64;

/// One control step per this many audio samples.
inline constexpr int kControlDecimation = 16;

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
void EngineInit();

/// @brief Start a note at the given frequency.
/// @param freq_hz Note frequency in Hz.
void EngineNoteOn(float freq_hz);

/// @brief Release the currently held note.
void EngineNoteOff();

/// @brief Return the single engine voice.
/// @return Pointer to the mutable voice state.
Voice *EngineVoice();

/// @brief Render `frames` mono samples into `out`.
///
/// Output is finite and clamped to [-1, 1].
/// @param out Destination buffer (holds at least `frames` floats).
/// @param frames Number of samples to render.
void Render(float *out, int frames);

}  // namespace engine
