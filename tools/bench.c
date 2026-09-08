/**
 * Cycle harness: measure engine render cost.
 *
 * Usage:
 *   bench [seconds] [voices]      full voice: ns/sample/voice
 *   bench --breakdown [seconds]   per-stage: oscillator vs filter vs rest
 *
 * Absolutes are desktop-only; ratios are what transfer to the target.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "dsp.h"

static volatile float g_sink;  /* defeats dead-code elimination */

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void run_full(int seconds, int voices) {
    int frames = seconds * ENGINE_SAMPLE_RATE;
    float *buf = malloc((size_t)frames * sizeof(float));
    if (!buf) return;

    engine_init();
    engine_set_cutoff(0.4f);
    engine_set_resonance(0.25f);
    engine_set_filter_env(0.5f);
    engine_set_adsr(0.01f, 0.3f, 0.6f, 0.4f);
    engine_note_on(440.0f);

    render(buf, ENGINE_BLOCK_SIZE);  /* warm caches */

    double t0 = now_ns();
    for (int v = 0; v < voices; ++v) render(buf, frames);
    double t1 = now_ns();

    double ns_per = (t1 - t0) / (double)frames / (double)voices;
    printf("voices=%d  seconds=%d  samples=%d\n", voices, seconds, frames);
    printf("total: %.2f ms\n", (t1 - t0) / 1e6);
    printf("ns/sample/voice: %.1f\n", ns_per);

    free(buf);
}

static void run_breakdown(int seconds) {
    int frames = seconds * ENGINE_SAMPLE_RATE;
    float *buf = malloc((size_t)frames * sizeof(float));
    if (!buf) return;

    /* standalone voice for isolated stage timing */
    Voice v = {0};
    v.inc = 440.0f / ENGINE_SAMPLE_RATE;
    dsp_svf_set_f_q(&v, 2000.0f, 2.0f);

    /* warm up */
    for (int i = 0; i < ENGINE_BLOCK_SIZE; ++i) {
        g_sink += dsp_osc_tick(&v);
        g_sink += dsp_svf_tick(&v, 0.5f);
    }
    g_sink = 0.0f;

    /* oscillator only */
    v.phase = 0.0f;
    double t0 = now_ns();
    for (int i = 0; i < frames; ++i) g_sink += dsp_osc_tick(&v);
    double t1 = now_ns();
    double osc_ns = (t1 - t0) / frames;

    /* oscillator + filter */
    v.phase = 0.0f;
    v.ic1eq = 0.0f;
    v.ic2eq = 0.0f;
    t0 = now_ns();
    for (int i = 0; i < frames; ++i) g_sink += dsp_svf_tick(&v, dsp_osc_tick(&v));
    t1 = now_ns();
    double osf_ns = (t1 - t0) / frames;

    /* full voice (osc + filter + envelope + coeffs), via render() */
    engine_init();
    engine_set_cutoff(0.4f);
    engine_set_resonance(0.25f);
    engine_set_filter_env(0.5f);
    engine_set_adsr(0.01f, 0.3f, 0.6f, 0.4f);
    engine_note_on(440.0f);
    render(buf, ENGINE_BLOCK_SIZE);
    t0 = now_ns();
    render(buf, frames);
    t1 = now_ns();
    double full_ns = (t1 - t0) / frames;

    double filter_ns = osf_ns - osc_ns;
    double rest_ns = full_ns - osf_ns;

    printf("oscillator:   %.2f ns/sample\n", osc_ns);
    printf("filter:       %.2f ns/sample\n", filter_ns);
    printf("osc+filter:   %.2f ns/sample\n", osf_ns);
    printf("env+coeff+ovh: %.2f ns/sample\n", rest_ns);
    printf("full voice:   %.2f ns/sample\n", full_ns);
    if (osc_ns > 0.0f) printf("filter/osc ratio: %.2fx\n", filter_ns / osc_ns);

    free(buf);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--breakdown") == 0) {
        int seconds = (argc > 2) ? atoi(argv[2]) : 5;
        if (seconds < 1) seconds = 1;
        run_breakdown(seconds);
        return 0;
    }
    int seconds = (argc > 1) ? atoi(argv[1]) : 5;
    int voices = (argc > 2) ? atoi(argv[2]) : 1;
    if (seconds < 1) seconds = 1;
    if (voices < 1) voices = 1;
    run_full(seconds, voices);
    return 0;
}
