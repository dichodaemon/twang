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
    EngineSetRoute(0, 2, ModSourceId::kEnv1, ParamId::kCutoff, 0.5f);
    EngineSetParamDisp(0, ParamId::kAttack, 0.01f);
    EngineSetParamDisp(0, ParamId::kDecay, 0.3f);
    EngineSetParam(0, ParamId::kSustain, 0.6f);
    EngineSetParamDisp(0, ParamId::kRelease, 0.4f);
}

// Transposed Direct Form II biquad. Coefficients are a representative 2-pole
// lowpass; the benchmark measures per-tick cost, not the filter's response.
struct Biquad {
    float s1 = 0.0f;
    float s2 = 0.0f;
};
static inline float BiquadTick(Biquad *b, float x) {
    constexpr float b0 = 0.29289322f, b1 = 0.58578644f, b2 = 0.29289322f;
    constexpr float a1 = 0.0f, a2 = -0.17157288f;
    const float y = b0 * x + b->s1;
    b->s1 = b1 * x - a1 * y + b->s2;
    b->s2 = b2 * x - a2 * y;
    return y;
}

// Rational tanh approximation (the study §5.7 "plain rational tanh" baseline).
// tanh(x) ~= x * (27 + x^2) / (27 + 9 x^2), max relative error ~0.3% for
// |x| < ~3. This is the cheap per-sample curve the CPU analysis assumes; the
// engine's CurveEval uses libm tanhf, which is ~9x this cost (see the
// discovered-from note on the §5.7 baseline).
static inline float TanhRational(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Per-evaluation microbenchmark of the anti-aliasing variants: plain tanh,
// ADAA 1D table, ADAA closed-form (log1p/exp), and 2x oversampling (2 tanh +
// 4 biquad ticks per input sample). Emitted by --breakdown.
static void RunAaVariants(int seconds) {
    const int frames = seconds * kSampleRate;
    constexpr float kLn2 = 0.6931471805599453f;
    // Cheap varying input (sawtooth in [-1,1]) so the ADAA quotient path is
    // exercised (dx ~ 0.0078 > epsilon).
    auto Input = [](int i) {
        return static_cast<float>(i & 0xFF) * (1.0f / 128.0f) - 1.0f;
    };

    /* plain tanh (rational approximation) */
    double t0 = NowNs();
    for (int i = 0; i < frames; ++i) g_sink += TanhRational(Input(i));
    double t1 = NowNs();
    const double plain_ns = (t1 - t0) / frames;

    /* ADAA, 1D F-table (ShaperProcess) */
    Voice v{};
    t0 = NowNs();
    for (int i = 0; i < frames; ++i) g_sink += ShaperProcess(&v, Input(i));
    t1 = NowNs();
    const double table_ns = (t1 - t0) / frames;

    /* ADAA, closed-form (log1p/exp) */
    float xp = 0.0f, Fp = 0.0f;
    t0 = NowNs();
    for (int i = 0; i < frames; ++i) {
        const float x = Input(i);
        const float dx = x - xp;
        const float ax = ::fabsf(x);
        const float fx = ax + ::log1pf(::expf(-2.0f * ax)) - kLn2;
        g_sink += (::fabsf(dx) < 1e-3f) ? TanhRational((x + xp) * 0.5f)
                                        : (fx - Fp) / dx;
        xp = x;
        Fp = fx;
    }
    t1 = NowNs();
    const double closed_ns = (t1 - t0) / frames;

    /* 2x oversampling: 2 tanh evaluations + 4 biquad ticks per input sample */
    Biquad up{}, down{};
    t0 = NowNs();
    for (int i = 0; i < frames; ++i) {
        const float x = Input(i);
        const float y0 = BiquadTick(&up, x);
        const float y1 = BiquadTick(&up, 0.0f);
        g_sink += BiquadTick(&down, TanhRational(y0)) + BiquadTick(&down, TanhRational(y1));
    }
    t1 = NowNs();
    const double oversamp_ns = (t1 - t0) / frames;

    std::printf("\nper-evaluation (ns / x-plain):\n");
    std::printf("  plain tanh:      %.2f ns (1.00x)\n", plain_ns);
    std::printf("  ADAA 1D table:   %.2f ns (%.2fx)\n", table_ns,
                table_ns / plain_ns);
    std::printf("  ADAA closed-form: %.2f ns (%.2fx)\n", closed_ns,
                closed_ns / plain_ns);
    std::printf("  2x oversampling: %.2f ns (%.2fx)\n", oversamp_ns,
                oversamp_ns / plain_ns);
}


static void RunFull(int seconds, int voices) {
    int frames = seconds * kSampleRate;
    std::vector<float> buf(frames);

    EngineInit();
    PatchPluck();
    EngineNoteOn(0, 440.0f, 127);

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

    /* oscillator + filter + shaper (ADAA, full drive) */
    sv.phase = 0.0f;
    sv.ic1eq = 0.0f;
    sv.ic2eq = 0.0f;
    sv.shaper.xp = 0.0f;
    sv.shaper.Fp = 0.0f;
    t0 = NowNs();
    for (int i = 0; i < frames; ++i) {
        const float lp = DspSvfTick(&sv, DspOscTick(&sv));
        g_sink += ShaperProcess(&sv, lp * 10.0f);
    }
    t1 = NowNs();
    double osfs_ns = (t1 - t0) / frames;

    /* full voice (osc + filter + envelope + coeffs), via Render() */
    EngineInit();
    PatchPluck();
    EngineNoteOn(0, 440.0f, 127);
    Render(buf.data(), kBlockSize);
    t0 = NowNs();
    Render(buf.data(), frames);
    t1 = NowNs();
    double full_ns = (t1 - t0) / frames;

    double filter_ns = osf_ns - osc_ns;
    double shaper_ns = osfs_ns - osf_ns;
    double rest_ns = full_ns - osf_ns;

    std::printf("oscillator:    %.2f ns/sample\n", osc_ns);
    std::printf("filter:        %.2f ns/sample\n", filter_ns);
    std::printf("osc+filter:    %.2f ns/sample\n", osf_ns);
    std::printf("shaper (ADAA): %.2f ns/sample\n", shaper_ns);
    std::printf("env+coeff+ovh: %.2f ns/sample\n", rest_ns);
    std::printf("full voice:    %.2f ns/sample\n", full_ns);
    if (osc_ns > 0.0f) std::printf("filter/osc ratio: %.2fx\n", filter_ns / osc_ns);

    RunAaVariants(seconds);
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
