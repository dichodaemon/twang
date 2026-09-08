#pragma once

#include <cstdint>

namespace engine {

inline constexpr int kSampleRate = 48000;          // Hz
inline constexpr int kBlockSize = 64;              // samples per block
inline constexpr int kControlDecimation = 16;      // 1 control step per N samples

// One synthesizer voice. Plain old data: memcpy-able, no heap pointers, no
// virtual table. This is what enables TCM placement on the target.
struct Voice {
    enum class Stage : std::uint8_t { kIdle, kAttack, kDecay, kSustain, kRelease };

    // oscillator
    float phase;   // [0, 1)
    float inc;     // phase increment per sample

    // TPT SVF (Zavalishin/Simper)
    float g, k, a1, a2, a3;
    float ic1eq, ic2eq;

    // envelope state
    float env;      // current level [0,1]
    float env_inc;  // per-sample signed increment
    Stage stage;    // envelope stage

    // parameters (all normalized 0..1; see params.h)
    float cutoff;
    float resonance;
    float filter_env_amount;
    float attack;
    float decay;
    float sustain;
    float release;

    bool gate;  // true held, false released
};

void  engine_init();
void  engine_note_on(float freq_hz);
void  engine_note_off();
Voice *engine_voice();

// Render `frames` mono samples into `out` (finite, clamped to [-1,1]).
void render(float *out, int frames);

}  // namespace engine
