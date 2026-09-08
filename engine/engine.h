#ifndef ENGINE_H
#define ENGINE_H

#define ENGINE_SAMPLE_RATE 48000
#define ENGINE_BLOCK_SIZE 64          // samples per processing block
#define ENGINE_CONTROL_DECIMATION 16  // 1 control step per N audio samples

// One synthesizer voice. Plain old data: memcpy-able, no heap pointers, no
// virtual table. This is what enables TCM placement on the target.
struct Voice {
    // oscillator
    float phase;   // [0, 1)
    float inc;     // phase increment per sample

    // TPT SVF (Zavalishin/Simper)
    float g, k, a1, a2, a3;
    float ic1eq, ic2eq;

    // envelope state
    float env;      // current level [0,1]
    float env_inc;  // per-sample signed increment
    int stage;      // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release

    // parameters (all normalized 0..1; see params.h)
    float cutoff;
    float resonance;
    float filter_env_amount;
    float attack;
    float decay;
    float sustain;
    float release;

    int gate;  // 1 held, 0 released
};

void  engine_init();
void  engine_note_on(float freq_hz);
void  engine_note_off();
Voice *engine_voice();

// Render `frames` mono samples into `out` (finite, clamped to [-1,1]).
void render(float *out, int frames);

#endif  // ENGINE_H
