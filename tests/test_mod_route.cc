#include <cmath>
#include <cstdio>
#include <vector>

#include "engine_audio.h"
#include "engine_control.h"
#include "params.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

// A ready-to-render engine rig (fresh audio state per case).
struct Rig {
    SharedIpc ipc;
    EngineControl control;
    EngineAudio audio{};
    Rig() {
        control.Init(ipc);
        audio.ipc = &ipc;
    }
};

// A flat envelope (instant attack, full sustain, instant release) so the
// render reaches a steady sawtooth with no transient, for deterministic
// frequency / amplitude measurements.
static void FlatEnvelope(EngineControl &control) {
    control.SetParamDisp(0, ParamRef{0, ParamId::kAttack}, 0.0f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kDecay}, 0.0f);
    control.SetParam(0, ParamRef{0, ParamId::kSustain}, 1.0f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kRelease}, 0.0f);
}

// Queue one note and render `samples` frames.
static std::vector<float> RenderNote(EngineControl &control, EngineAudio &audio,
                                     int samples, float freq,
                                     std::uint8_t velocity) {
    std::vector<float> buf(samples);
    control.NoteOn(0, 69, freq, velocity);
    Render(audio, buf.data(), samples);
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

// Fundamental frequency via zero crossings.
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
        Rig r1;
        FlatEnvelope(r1.control);
        std::vector<float> full = RenderNote(r1.control, r1.audio, kDur, 440.0f, 127);
        Rig r2;
        FlatEnvelope(r2.control);
        std::vector<float> soft = RenderNote(r2.control, r2.audio, kDur, 440.0f, 32);
        Check(Peak(full) > Peak(soft),
              "velocity->amp: full-velocity louder than soft");
    }

    // 2. Empty slot (kNone) and a zero-amount route both contribute nothing.
    {
        Rig r1;
        FlatEnvelope(r1.control);
        std::vector<float> base = RenderNote(r1.control, r1.audio, kDur, 440.0f, 127);

        Rig r2;
        FlatEnvelope(r2.control);
        r2.control.SetRoute(0, 5, ModSourceId::kEnv0, ParamRef{0, ParamId::kCutoff}, 0.0f);
        std::vector<float> zero = RenderNote(r2.control, r2.audio, kDur, 440.0f, 127);

        Rig r3;
        FlatEnvelope(r3.control);
        r3.control.SetRoute(0, 5, ModSourceId::kNone, ParamRef{0, ParamId::kCutoff}, 0.0f);
        std::vector<float> empty = RenderNote(r3.control, r3.audio, kDur, 440.0f, 127);

        Check(base == zero, "zero-amount route contributes nothing");
        Check(base == empty, "empty (kNone) route contributes nothing");
    }

    // 3. Key follow: depth 1.0 doubles the cutoff one octave up.
    {
        float rms0, rms1;
        Rig r1;
        FlatEnvelope(r1.control);
        r1.control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.25f);
        r1.control.SetParam(0, ParamRef{0, ParamId::kKeyFollowDepth}, 0.0f);
        rms0 = Rms(RenderNote(r1.control, r1.audio, kDur, 523.2511f, 127));

        Rig r2;
        FlatEnvelope(r2.control);
        r2.control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.25f);
        r2.control.SetParam(0, ParamRef{0, ParamId::kKeyFollowDepth}, 1.0f);
        rms1 = Rms(RenderNote(r2.control, r2.audio, kDur, 523.2511f, 127));

        Check(rms1 > rms0 * 1.5f,
              "key follow: depth 1 raises the cutoff (higher RMS)");
    }

    // 4. Pitchbend -> pitch coarse (exponential): amount 2 gives +/-2 semitones.
    {
        float f;

        Rig r1;
        FlatEnvelope(r1.control);
        r1.control.SetParam(0, ParamRef{0, ParamId::kPitchBend}, 1.0f);
        f = ZeroCrossFreq(RenderNote(r1.control, r1.audio, kDur, 440.0f, 127));
        Check(std::fabs(f - 440.0f) < 2.0f,
              "pitchbend amount 0: full bend changes nothing");

        Rig r2;
        FlatEnvelope(r2.control);
        r2.control.SetRoute(0, 4, ModSourceId::kPitchBend, ParamRef{0, ParamId::kPitchCoarse}, 2.0f);
        r2.control.SetParam(0, ParamRef{0, ParamId::kPitchBend}, 1.0f);
        f = ZeroCrossFreq(RenderNote(r2.control, r2.audio, kDur, 440.0f, 127));
        Check(std::fabs(f - 493.88f) < 3.0f,
              "pitchbend amount 2: full bend +2 semitones");

        Rig r3;
        FlatEnvelope(r3.control);
        r3.control.SetRoute(0, 4, ModSourceId::kPitchBend, ParamRef{0, ParamId::kPitchCoarse}, 2.0f);
        r3.control.SetParam(0, ParamRef{0, ParamId::kPitchBend}, 0.0f);
        f = ZeroCrossFreq(RenderNote(r3.control, r3.audio, kDur, 440.0f, 127));
        Check(std::fabs(f - 392.0f) < 3.0f,
              "pitchbend amount 2: full bend -2 semitones");
    }

    // 5. Source gating: inert in phase 1 (nothing to advance yet).

    // 6. A zero-amount route to a MULTIPLICATIVE destination (amp) is neutral.
    {
        Rig r1;
        FlatEnvelope(r1.control);
        std::vector<float> base = RenderNote(r1.control, r1.audio, kDur, 440.0f, 127);

        Rig r2;
        FlatEnvelope(r2.control);
        r2.control.SetRoute(0, 5, ModSourceId::kEnv0, ParamRef{0, ParamId::kAmp}, 0.0f);
        std::vector<float> zero = RenderNote(r2.control, r2.audio, kDur, 440.0f, 127);

        Check(base == zero, "zero-amount amp route is neutral (bit-exact)");
    }

    // 7. A bipolar source into a multiplicative destination tremolos around
    //    the base.
    {
        Rig r1;
        FlatEnvelope(r1.control);
        r1.control.SetRoute(0, 5, ModSourceId::kPitchBend, ParamRef{0, ParamId::kAmp}, 1.0f);
        r1.control.SetParam(0, ParamRef{0, ParamId::kPitchBend}, 0.5f);
        std::vector<float> center = RenderNote(r1.control, r1.audio, kDur, 440.0f, 127);

        Rig r2;
        FlatEnvelope(r2.control);
        r2.control.SetRoute(0, 5, ModSourceId::kPitchBend, ParamRef{0, ParamId::kAmp}, 1.0f);
        r2.control.SetParam(0, ParamRef{0, ParamId::kPitchBend}, 1.0f);
        std::vector<float> up = RenderNote(r2.control, r2.audio, kDur, 440.0f, 127);

        Rig r3;
        FlatEnvelope(r3.control);
        std::vector<float> base = RenderNote(r3.control, r3.audio, kDur, 440.0f, 127);

        Check(base == center, "bipolar center is neutral");
        Check(Peak(up) > Peak(center), "bipolar full-up tremolos above center");
    }

    // 8. Route-read round-trip.
    {
        Rig r;
        ModRoute rt{};

        Check(!r.control.GetRoute(0, 5, &rt), "empty slot reports false");
        Check(!r.control.GetRoute(0, kModSlots, &rt), "slot == kModSlots reports false");
        Check(!r.control.GetRoute(kNumParts, 0, &rt), "part == kNumParts reports false");

        r.control.SetRoute(0, 6, ModSourceId::kLfo2, ParamRef{0, ParamId::kCutoff}, -0.25f);
        Check(r.control.GetRoute(0, 6, &rt), "set route reads back true");
        Check(rt.source == ModSourceId::kLfo2 && rt.dst.id == ParamId::kCutoff &&
                  rt.dst.instance == 0 && rt.amount == -0.25f,
              "route round-trips (source, dst, amount)");

        r.control.SetRoute(0, 6, ModSourceId::kNone, ParamRef{0, ParamId::kCutoff}, 0.0f);
        Check(!r.control.GetRoute(0, 6, &rt), "cleared slot reports false");
    }

    // 9. Additive destination at extremes: kConstant -> cutoff moves the
    //    filter up (positive amount) and down (negative amount).
    {
        Rig base;
        FlatEnvelope(base.control);
        base.control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.5f);
        const float rms_base =
            Rms(RenderNote(base.control, base.audio, kDur, 440.0f, 127));

        Rig up;
        FlatEnvelope(up.control);
        up.control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.5f);
        up.control.SetRoute(0, 5, ModSourceId::kConstant,
                            ParamRef{0, ParamId::kCutoff}, 0.5f);
        const float rms_up =
            Rms(RenderNote(up.control, up.audio, kDur, 440.0f, 127));

        Rig down;
        FlatEnvelope(down.control);
        down.control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.5f);
        down.control.SetRoute(0, 5, ModSourceId::kConstant,
                              ParamRef{0, ParamId::kCutoff}, -0.5f);
        const float rms_down =
            Rms(RenderNote(down.control, down.audio, kDur, 440.0f, 127));

        Check(rms_up > rms_base, "additive: positive amount raises cutoff");
        Check(rms_base > rms_down, "additive: negative amount lowers cutoff");
    }

    // 10. Unipolar multiplicative sub-form: a unipolar source at full
    //     (src = 1.0) contributes nothing to a multiplicative destination
    //     (1 + amount*(src-1) = 1), distinguishing it from the bipolar form
    //     (1 + amount*src = 1 + amount).
    {
        Rig base;
        FlatEnvelope(base.control);
        std::vector<float> b =
            RenderNote(base.control, base.audio, kDur, 440.0f, 127);

        Rig r;
        FlatEnvelope(r.control);
        r.control.SetRoute(0, 5, ModSourceId::kConstant,
                           ParamRef{0, ParamId::kAmp}, 1.0f);
        std::vector<float> u =
            RenderNote(r.control, r.audio, kDur, 440.0f, 127);

        Check(b == u, "unipolar multiplicative: full source is neutral");
    }

    // 11. Resonance is modulatable: a constant route into it raises the
    //     filter resonance (higher Q -> a louder peak at the cutoff).
    {
        Rig base;
        FlatEnvelope(base.control);
        base.control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.5f);
        const float rms_base =
            Rms(RenderNote(base.control, base.audio, kDur, 440.0f, 127));

        Rig res;
        FlatEnvelope(res.control);
        res.control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.5f);
        res.control.SetRoute(0, 5, ModSourceId::kConstant,
                             ParamRef{0, ParamId::kResonance}, 1.0f);
        const float rms_res =
            Rms(RenderNote(res.control, res.audio, kDur, 440.0f, 127));

        Check(rms_res > rms_base, "resonance route raises the filter output");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: mod route\n");
    return 0;
}
