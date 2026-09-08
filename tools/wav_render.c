/**
 * Command-line WAV renderer: play one note through the engine to a WAV file.
 *
 * Usage: wav_render [seconds] [out.wav]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "engine.h"

static void put_u16(FILE *f, uint16_t v) { fwrite(&v, 1, 2, f); }
static void put_u32(FILE *f, uint32_t v) { fwrite(&v, 1, 4, f); }

int main(int argc, char **argv) {
    int seconds = (argc > 1) ? atoi(argv[1]) : 1;
    const char *path = (argc > 2) ? argv[2] : "out.wav";

    int frames = seconds * ENGINE_SAMPLE_RATE;
    float *buf = malloc((size_t)frames * sizeof(float));
    if (!buf) return 1;

    /* plucky patch: saw -> envelope-swept SVF -> ADSR */
    engine_init();
    engine_set_cutoff(0.4f);
    engine_set_resonance(0.25f);
    engine_set_filter_env(0.5f);
    engine_set_adsr(0.01f, 0.3f, 0.6f, 0.4f);

    int note_frames = frames * 8 / 10;  /* held 80%, release the rest */
    engine_note_on(440.0f);
    render(buf, note_frames);
    engine_note_off();
    render(buf + note_frames, frames - note_frames);

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return 1; }

    uint32_t data_bytes = (uint32_t)frames * 2;
    uint32_t riff_size = 36 + data_bytes;
    fwrite("RIFF", 1, 4, f); put_u32(f, riff_size); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put_u32(f, 16);
    put_u16(f, 1);                      /* PCM */
    put_u16(f, 1);                      /* mono */
    put_u32(f, ENGINE_SAMPLE_RATE);
    put_u32(f, ENGINE_SAMPLE_RATE * 2); /* byte rate */
    put_u16(f, 2);                      /* block align */
    put_u16(f, 16);                     /* bits/sample */
    fwrite("data", 1, 4, f); put_u32(f, data_bytes);

    for (int i = 0; i < frames; ++i) {
        float s = buf[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t pcm = (int16_t)(s * 32767.0f);
        fwrite(&pcm, 1, 2, f);
    }

    fclose(f);
    free(buf);
    printf("Wrote %d s (%d samples) to %s\n", seconds, frames, path);
    return 0;
}
