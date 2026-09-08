#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include "engine.h"
#include "dsp.h"
#include "params.h"

using namespace engine;

static volatile float g_sink;  // defeats dead-code elimination

static double now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void patch_pluck(Voice *v) {
    param_set(v, ParamId::kCutoff, 0.4f);
    param_set(v, ParamId::kResonance, 0.25f);
    param_set(v, ParamId::kFilterEnvAmount, 0.5f);
    param_set_disp(v, ParamId::kAttack, 0.01f);
    param_set_disp(v, ParamId::kDecay, 0.3f);
    param_set(v, ParamId::kSustain, 0.6f);
    param_set_disp(v, ParamId::kRelease, 0.4f);
}

static void run_full(int seconds, int voices) {
    int frames = seconds * kSampleRate;
    std::vector<float> buf(frames);

    engine_init();
    patch_pluck(engine_voice());
    engine_note_on(440.0f);

    render(buf.data(), kBlockSize);  // warm caches

    double t0 = now_ns();
    for (int v = 0; v < voices; ++v) render(buf.data(), frames);
    double t1 = now_ns();

    double ns_per = (t1 - t0) / (double)frames / (double)voices;
    std::printf("voices=%d  seconds=%d  samples=%d\n", voices, seconds, frames);
    std::printf("total: %.2f ms\n", (t1 - t0) / 1e6);
    std::printf("ns/sample/voice: %.1f\n", ns_per);
}

static void run_breakdown(int seconds) {
    int frames = seconds * kSampleRate;
    std::vector<float> buf(frames);

    /* standalone voice for isolated stage timing */
    Voice sv = {};
    sv.inc = 440.0f / kSampleRate;
    dsp_svf_set_f_q(&sv, 2000.0f, 2.0f);

    for (int i = 0; i < kBlockSize; ++i) {
        g_sink += dsp_osc_tick(&sv);
        g_sink += dsp_svf_tick(&sv, 0.5f);
    }
    g_sink = 0.0f;

    /* oscillator only */
    sv.phase = 0.0f;
    double t0 = now_ns();
    for (int i = 0; i < frames; ++i) g_sink += dsp_osc_tick(&sv);
    double t1 = now_ns();
    double osc_ns = (t1 - t0) / frames;

    /* oscillator + filter */
    sv.phase = 0.0f;
    sv.ic1eq = 0.0f;
    sv.ic2eq = 0.0f;
    t0 = now_ns();
    for (int i = 0; i < frames; ++i)
        g_sink += dsp_svf_tick(&sv, dsp_osc_tick(&sv));
    t1 = now_ns();
    double osf_ns = (t1 - t0) / frames;

    /* full voice (osc + filter + envelope + coeffs), via render() */
    engine_init();
    patch_pluck(engine_voice());
    engine_note_on(440.0f);
    render(buf.data(), kBlockSize);
    t0 = now_ns();
    render(buf.data(), frames);
    t1 = now_ns();
    double full_ns = (t1 - t0) / frames;

    double filter_ns = osf_ns - osc_ns;
    double rest_ns = full_ns - osf_ns;

    std::printf("oscillator:    %.2f ns/sample\n", osc_ns);
    std::printf("filter:        %.2f ns/sample\n", filter_ns);
    std::printf("osc+filter:    %.2f ns/sample\n", osf_ns);
    std::printf("env+coeff+ovh: %.2f ns/sample\n", rest_ns);
    std::printf("full voice:    %.2f ns/sample\n", full_ns);
    if (osc_ns > 0.0f) std::printf("filter/osc ratio: %.2fx\n", filter_ns / osc_ns);
}

int main(int argc, char **argv) {
    if (argc > 1 && std::strcmp(argv[1], "--breakdown") == 0) {
        int seconds = (argc > 2) ? std::atoi(argv[2]) : 5;
        if (seconds < 1) seconds = 1;
        run_breakdown(seconds);
        return 0;
    }
    int seconds = (argc > 1) ? std::atoi(argv[1]) : 5;
    int voices = (argc > 2) ? std::atoi(argv[2]) : 1;
    if (seconds < 1) seconds = 1;
    if (voices < 1) voices = 1;
    run_full(seconds, voices);
    return 0;
}
