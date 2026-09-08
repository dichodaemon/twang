/**
 * Live audio: stream the engine to the default playback device, retriggering
 * a note periodically so the ADSR envelope is audible.
 *
 * Usage: live_render   (press Enter to stop)
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>

#include "engine.h"

/* Note pattern (samples @ ENGINE_SAMPLE_RATE): 0.9 s held, 0.6 s released. */
#define NOTE_HELD_SAMPLES   ((ma_uint64)ENGINE_SAMPLE_RATE * 9 / 10)
#define NOTE_PERIOD_SAMPLES ((ma_uint64)ENGINE_SAMPLE_RATE * 3 / 2)

static ma_uint64 g_frame = 0;  /* audio-thread-only sample counter */

static void audio_callback(ma_device *device, void *output, const void *input,
                           ma_uint32 frame_count) {
    (void)device;
    (void)input;

    float *dst = (float *)output;
    while (frame_count > 0) {
        ma_uint64 pos = g_frame % NOTE_PERIOD_SAMPLES;
        ma_uint64 boundary = (pos < NOTE_HELD_SAMPLES) ? NOTE_HELD_SAMPLES
                                                       : NOTE_PERIOD_SAMPLES;
        ma_uint32 n = (ma_uint32)((boundary - pos) < frame_count
                                      ? (boundary - pos)
                                      : frame_count);

        if (pos == 0) engine_note_on(440.0f);
        render(dst, (int)n);
        g_frame += n;
        dst += n;
        frame_count -= n;

        if (g_frame % NOTE_PERIOD_SAMPLES == NOTE_HELD_SAMPLES)
            engine_note_off();
    }
}

int main(void) {
    engine_init();
    engine_set_cutoff(0.4f);
    engine_set_resonance(0.25f);
    engine_set_filter_env(0.5f);
    engine_set_adsr(0.01f, 0.3f, 0.6f, 0.4f);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;   /* matches render()'s float out */
    cfg.playback.channels = 1;
    cfg.sampleRate = ENGINE_SAMPLE_RATE;
    cfg.dataCallback = audio_callback;

    ma_device device;
    if (ma_device_init(NULL, &cfg, &device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to open playback device\n");
        return 1;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        fprintf(stderr, "Failed to start playback\n");
        return 1;
    }

    printf("Retriggering note every %.1f s. Press Enter to stop.\n",
           (float)NOTE_PERIOD_SAMPLES / ENGINE_SAMPLE_RATE);
    (void)getchar();

    ma_device_uninit(&device);
    return 0;
}
