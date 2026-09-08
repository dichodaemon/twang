#include <math.h>
#include <stdio.h>

#include "engine.h"

int main(void) {
    enum { N = ENGINE_SAMPLE_RATE };  /* 1 second */
    static float buf[N];

    engine_init();
    engine_set_adsr(0.01f, 0.2f, 0.7f, 0.2f);
    engine_note_on(440.0f);
    render(buf, N);

    float peak = 0.0f, sum_sq = 0.0f;
    int bad = 0;
    for (int i = 0; i < N; ++i) {
        float s = buf[i];
        if (!isfinite(s) || s > 1.0f || s < -1.0f) ++bad;
        float a = fabsf(s);
        if (a > peak) peak = a;
        sum_sq += s * s;
    }
    float rms = sqrtf(sum_sq / N);

    if (bad) { printf("FAIL: %d samples out of range or non-finite\n", bad); return 1; }
    if (peak < 0.1f) { printf("FAIL: peak %.3f too low (silent)\n", peak); return 1; }
    if (rms < 0.01f) { printf("FAIL: rms %.3f too low (silent)\n", rms); return 1; }

    /* release: after one second the envelope must have decayed to silence */
    engine_note_off();
    render(buf, N);
    float tail = 0.0f;
    for (int i = N - ENGINE_BLOCK_SIZE; i < N; ++i) {
        float a = fabsf(buf[i]);
        if (a > tail) tail = a;
    }
    if (tail > 0.01f) { printf("FAIL: tail %.3f not silent after release\n", tail); return 1; }

    printf("PASS: peak=%.3f rms=%.3f tail=%.4f\n", peak, rms, tail);
    return 0;
}
