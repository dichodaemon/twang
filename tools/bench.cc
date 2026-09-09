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

static double NowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void PatchPluck() {
    EngineSetParam(0, ParamId::kCutoff, 0.4f);
    EngineSetParam(0, ParamId::kResonance, 0.25f);
    EngineSetParam(0, ParamId::kFilterEnvAmount, 0.5f);
    EngineSetParamDisp(0, ParamId::kAttack, 0.01f);
    EngineSetParamDisp(0, ParamId::kDecay, 0.3f);
    EngineSetParam(0, ParamId::kSustain, 0.6f);
    EngineSetParamDisp(0, ParamId::kRelease, 0.4f);
}

static void RunFull(int seconds, int voices) {
    int frames = seconds * kSampleRate;
    std::vector<float> buf(frames);

    EngineInit();
    PatchPluck();
    EngineNoteOn(0, 440.0f);

    Render(buf.data(), kBlockSize);  // warm caches

    double t0 = NowNs();
    for (int v = 0; v < voices; ++v) Render(buf.data(), frames);
    double t1 = NowNs();

    double ns_per = (t1 - t0) / (double)frames / (double)voices;
    std::printf("voices=%d  seconds=%d  samples=%d\n", voices, seconds, frames);
    std::printf("total: %.2f ms\n", (t1 - t0) / 1e6);
    std::printf("ns/sample/voice: %.1f\n", ns_per);
}

static void RunBreakdown(int seconds) {
    int frames = seconds * kSampleRate;
    std::vector<float> buf(frames);

    /* standalone voice for isolated stage timing */
    Voice sv = {};
    sv.inc = 440.0f / kSampleRate;
    DspSvfSetFq(&sv, 2000.0f, 2.0f);

    for (int i = 0; i < kBlockSize; ++i) {
        g_sink += DspOscTick(&sv);
        g_sink += DspSvfTick(&sv, 0.5f);
    }
    g_sink = 0.0f;

    /* oscillator only */
    sv.phase = 0.0f;
    double t0 = NowNs();
    for (int i = 0; i < frames; ++i) g_sink += DspOscTick(&sv);
    double t1 = NowNs();
    double osc_ns = (t1 - t0) / frames;

    /* oscillator + filter */
    sv.phase = 0.0f;
    sv.ic1eq = 0.0f;
    sv.ic2eq = 0.0f;
    t0 = NowNs();
    for (int i = 0; i < frames; ++i)
        g_sink += DspSvfTick(&sv, DspOscTick(&sv));
    t1 = NowNs();
    double osf_ns = (t1 - t0) / frames;

    /* full voice (osc + filter + envelope + coeffs), via Render() */
    EngineInit();
    PatchPluck();
    EngineNoteOn(0, 440.0f);
    Render(buf.data(), kBlockSize);
    t0 = NowNs();
    Render(buf.data(), frames);
    t1 = NowNs();
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
        RunBreakdown(seconds);
        return 0;
    }
    int seconds = (argc > 1) ? std::atoi(argv[1]) : 5;
    int voices = (argc > 2) ? std::atoi(argv[2]) : 1;
    if (seconds < 1) seconds = 1;
    if (voices < 1) voices = 1;
    RunFull(seconds, voices);
    return 0;
}
