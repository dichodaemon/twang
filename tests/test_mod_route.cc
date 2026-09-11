#include <cmath>
#include <cstdio>
#include <vector>

#include "engine.h"
#include "params.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

// A flat envelope (instant attack, full sustain, instant release) so the
// render reaches a steady sawtooth with no transient, for deterministic
// frequency / amplitude measurements.
static void FlatEnvelope() {
    EngineSetParamDisp(0, ParamId::kAttack, 0.0f);
    EngineSetParamDisp(0, ParamId::kDecay, 0.0f);
    EngineSetParam(0, ParamId::kSustain, 1.0f);
    EngineSetParamDisp(0, ParamId::kRelease, 0.0f);
}

// Queue one note and render `samples` frames. The engine must already be
// initialized and the patch set up.
static std::vector<float> RenderNote(int samples, float freq,
                                     std::uint8_t velocity) {
    std::vector<float> buf(samples);
    EngineNoteOn(0, freq, velocity);
    Render(buf.data(), samples);
    return buf;
}

static float Peak(const std::vector<float> &buf) {
    float peak = 0.0f;
    for (float s : buf) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    return peak;
}

static float Rms(const std::vector<float> &buf) {
    double sum = 0.0;
    for (float s : buf) sum += static_cast<double>(s) * s;
    return static_cast<float>(std::sqrt(sum / buf.size()));
}

// Fundamental frequency via zero crossings. A polyBLEP sawtooth crosses zero
// twice per cycle (the smooth -/+ crossing and the wrap +/ - jump), so
// frequency = crossings * sample_rate / (2 * samples).
static float ZeroCrossFreq(const std::vector<float> &buf) {
    int crossings = 0;
    for (std::size_t i = 1; i < buf.size(); ++i)
        if ((buf[i - 1] < 0.0f) != (buf[i] < 0.0f)) ++crossings;
    return static_cast<float>(crossings) * kSampleRate /
           (2.0f * static_cast<float>(buf.size()));
}

int main() {
    constexpr int kDur = kSampleRate;  // 1 second

    // 1. Velocity -> amp (default route): a soft note is quieter than a full
    //    velocity note.
    {
        EngineInit();
        FlatEnvelope();
        std::vector<float> full = RenderNote(kDur, 440.0f, 127);
        EngineInit();
        FlatEnvelope();
        std::vector<float> soft = RenderNote(kDur, 440.0f, 32);
        Check(Peak(full) > Peak(soft),
              "velocity->amp: full-velocity louder than soft");
    }

    // 2. Empty slot (kNone) and a zero-amount route both contribute nothing
    //    (additive cutoff: amount 0 adds exactly 0, so the render is
    //    bit-identical to the default-route baseline).
    {
        EngineInit();
        FlatEnvelope();
        std::vector<float> base = RenderNote(kDur, 440.0f, 127);

        EngineInit();
        FlatEnvelope();
        EngineSetRoute(0, 5, ModSourceId::kEnv0, ParamId::kCutoff, 0.0f);
        std::vector<float> zero = RenderNote(kDur, 440.0f, 127);

        EngineInit();
        FlatEnvelope();
        EngineSetRoute(0, 5, ModSourceId::kNone, ParamId::kCutoff, 0.0f);
        std::vector<float> empty = RenderNote(kDur, 440.0f, 127);

        Check(base == zero, "zero-amount route contributes nothing");
        Check(base == empty, "empty (kNone) route contributes nothing");
    }

    // 3. Key follow: depth 1.0 doubles the cutoff one octave up. A C5 sawtooth
    //    (523 Hz) through a low cutoff (~112 Hz at norm 0.25) is heavily
    //    attenuated; doubling the cutoff to ~224 Hz roughly quadruples the
    //    passed energy, so the RMS rises markedly.
    {
        float rms0, rms1;
        EngineInit();
        FlatEnvelope();
        EngineSetParam(0, ParamId::kCutoff, 0.25f);
        EngineSetParam(0, ParamId::kKeyFollowDepth, 0.0f);
        rms0 = Rms(RenderNote(kDur, 523.2511f, 127));

        EngineInit();
        FlatEnvelope();
        EngineSetParam(0, ParamId::kCutoff, 0.25f);
        EngineSetParam(0, ParamId::kKeyFollowDepth, 1.0f);
        rms1 = Rms(RenderNote(kDur, 523.2511f, 127));

        Check(rms1 > rms0 * 1.5f,
              "key follow: depth 1 raises the cutoff (higher RMS)");
    }

    // 4. Pitchbend -> pitch coarse (exponential): amount 2 gives +/-2
    //    semitones; the default amount 0 means a full bend changes nothing.
    {
        float f;

        EngineInit();
        FlatEnvelope();
        EngineSetParam(0, ParamId::kPitchBend, 1.0f);  // full bend up
        f = ZeroCrossFreq(RenderNote(kDur, 440.0f, 127));
        Check(std::fabs(f - 440.0f) < 2.0f,
              "pitchbend amount 0: full bend changes nothing");

        EngineInit();
        FlatEnvelope();
        EngineSetRoute(0, 4, ModSourceId::kPitchBend, ParamId::kPitchCoarse,
                       2.0f);
        EngineSetParam(0, ParamId::kPitchBend, 1.0f);
        f = ZeroCrossFreq(RenderNote(kDur, 440.0f, 127));
        Check(std::fabs(f - 493.88f) < 3.0f,
              "pitchbend amount 2: full bend +2 semitones");

        EngineInit();
        FlatEnvelope();
        EngineSetRoute(0, 4, ModSourceId::kPitchBend, ParamId::kPitchCoarse,
                       2.0f);
        EngineSetParam(0, ParamId::kPitchBend, 0.0f);
        f = ZeroCrossFreq(RenderNote(kDur, 440.0f, 127));
        Check(std::fabs(f - 392.0f) < 3.0f,
              "pitchbend amount 2: full bend -2 semitones");
    }

    // 5. Source gating: inert in phase 1 (the only stateful sources, LFOs and
    //    env2, do not exist yet; there is nothing to advance). Lands as an
    //    observable check when LFOs arrive in phase 2.

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: mod route\n");
    return 0;
}
