#ifndef ENGINE_H
#define ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#define ENGINE_SAMPLE_RATE 48000
#define ENGINE_BLOCK_SIZE 64          /* samples per processing block */
#define ENGINE_CONTROL_DECIMATION 16  /* 1 control step per N audio samples */

/* One synthesizer voice. Plain old data: memcpy-able, no heap pointers. */
typedef struct {
    /* oscillator */
    float phase;   /* [0, 1) */
    float inc;     /* phase increment per sample */

    /* TPT SVF (Zavalishin/Simper) */
    float g, k, a1, a2, a3;
    float ic1eq, ic2eq;

    /* envelope */
    float env;      /* current level [0,1] */
    float env_inc;  /* per-sample signed increment */
    int   stage;    /* 0 idle, 1 attack, 2 decay, 3 sustain, 4 release */
    float sustain;  /* sustain level [0,1] */

    /* parameters */
    float cutoff;            /* normalized [0,1] -> 20 Hz .. 20 kHz */
    float resonance;         /* normalized [0,1] -> Q 0.5 .. 20.5 */
    float filter_env_amount; /* envelope -> cutoff amount [0,1] */
    float attack_s, decay_s, release_s;
    int   gate;              /* 1 held, 0 released */
} Voice;

void engine_init(void);
void engine_note_on(float freq_hz);
void engine_note_off(void);
void engine_set_cutoff(float normalized);    /* [0,1] */
void engine_set_resonance(float normalized); /* [0,1] */
void engine_set_filter_env(float amount);    /* [0,1] */
void engine_set_adsr(float attack_s, float decay_s, float sustain,
                     float release_s);

/* Render `frames` mono samples into `out` (finite, clamped to [-1,1]). */
void render(float *out, int frames);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_H */
