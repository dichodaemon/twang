#include <cmath>
#include <cstdio>
#include <vector>

#include "engine.h"
#include "params.h"

using namespace engine;

static int g_fail = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_fail;
    }
}

// Peak magnitude across a rendered buffer.
static float Peak(const std::vector<float> &buf) {
    float p = 0.0f;
    for (float s : buf) {
        const float a = std::fabs(s);
        if (a > p) p = a;
    }
    return p;
}

// Standard part setup: fast attack so a note reaches full level quickly.
static void SetupPart() {
    EngineSetParamDisp(0, ParamId::kAttack, 0.01f);
    EngineSetParamDisp(0, ParamId::kDecay, 0.2f);
    EngineSetParam(0, ParamId::kSustain, 0.7f);
    EngineSetParamDisp(0, ParamId::kRelease, 0.2f);
}

int main() {
    constexpr int kN = kSampleRate;  // 1 second

    /* Basic render + release. */
    {
        std::vector<float> buf(kN);
        EngineInit();
        SetupPart();
        EngineNoteOn(0, 440.0f, 127);
        Render(buf.data(), kN);

        float peak = Peak(buf);
        float sum_sq = 0.0f;
        for (float s : buf) sum_sq += s * s;
        const float rms = std::sqrt(sum_sq / kN);
        int bad = 0;
        for (float s : buf)
            if (!std::isfinite(s) || s > 1.0f || s < -1.0f) ++bad;
        Check(bad == 0, "basic: output finite and in [-1,1]");
        Check(peak > 0.1f, "basic: peak above silence");
        Check(rms > 0.01f, "basic: rms above silence");

        EngineNoteOff(0, 440.0f);
        Render(buf.data(), kN);
        float tail = 0.0f;
        for (int i = kN - kBlockSize; i < kN; ++i) {
            const float a = std::fabs(buf[i]);
            if (a > tail) tail = a;
        }
        Check(tail < 0.01f, "basic: silent after release");
    }

    /* Velocity: a full-velocity note is louder than a soft one. */
    {
        std::vector<float> buf(kN);
        EngineInit(); SetupPart(); EngineNoteOn(0, 440.0f, 127);
        Render(buf.data(), kN); const float pf = Peak(buf);
        EngineInit(); SetupPart(); EngineNoteOn(0, 440.0f, 32);
        Render(buf.data(), kN); const float ps = Peak(buf);
        Check(pf > ps, "velocity: 127 louder than 32");
    }

    /* Bypass: a silent drive route (amount 0) is bit-identical to no route —
     * the shaper blends to exactly lp at depth 0. */
    {
        std::vector<float> a(kN), b(kN);
        EngineInit(); SetupPart(); EngineNoteOn(0, 440.0f, 127);
        Render(a.data(), kN);
        EngineInit(); SetupPart();
        EngineSetRoute(0, 5, ModSourceId::kConstant, ParamId::kDrive, 0.0f);
        EngineNoteOn(0, 440.0f, 127);
        Render(b.data(), kN);
        bool identical = true;
        for (int i = 0; i < kN; ++i)
            if (a[i] != b[i]) { identical = false; break; }
        Check(identical, "bypass: depth-0 drive route bit-identical to no route");
    }

    /* Drive effect: kDrive = 1 alters the output vs kDrive = 0. */
    {
        std::vector<float> dry(kN), wet(kN);
        EngineInit(); SetupPart(); EngineNoteOn(0, 440.0f, 127);
        Render(dry.data(), kN);
        EngineInit(); SetupPart(); EngineSetParam(0, ParamId::kDrive, 1.0f);
        EngineNoteOn(0, 440.0f, 127);
        Render(wet.data(), kN);
        bool differs = false;
        for (int i = 0; i < kN; ++i)
            if (dry[i] != wet[i]) { differs = true; break; }
        Check(differs, "drive: kDrive=1 alters the output");
    }

    /* Bus protection: 24 coherent voices drive the bus over the rail; the
     * output stays in [-1,1] and the meter reads > 1 (pre-saturator). */
    {
        EngineInit(); SetupPart(); EngineSetParam(0, ParamId::kSustain, 1.0f);
        for (int i = 0; i < kNumVoices; ++i) EngineNoteOn(0, 440.0f, 127);
        std::vector<float> buf(kN);
        Render(buf.data(), kN);
        bool bounded = true;
        for (float s : buf)
            if (!std::isfinite(s) || s > 1.0f || s < -1.0f) { bounded = false; break; }
        Check(bounded, "bus: output stays in [-1,1] over the rail");
        Check(EngineGetMeter() > 1.0f, "bus: meter > 1 when rail driven");
        Check(EngineGetMeter() == 0.0f, "bus: meter read-and-clear");
    }

    /* Migration: at the shipped kBusGain, kAmp 0.25 vs 1.0 differ by ~4x below
     * the rail (headroom is on the bus, not the level). */
    {
        std::vector<float> buf(kN);
        EngineInit(); SetupPart(); EngineSetParam(0, ParamId::kAmp, 0.25f);
        EngineNoteOn(0, 440.0f, 127);
        Render(buf.data(), kN); const float p025 = Peak(buf);
        EngineInit(); SetupPart(); EngineSetParam(0, ParamId::kAmp, 1.0f);
        EngineNoteOn(0, 440.0f, 127);
        Render(buf.data(), kN); const float p100 = Peak(buf);
        const float ratio = p100 / p025;
        Check(ratio > 3.9f && ratio < 4.1f, "migration: kAmp 1.0 ~4x kAmp 0.25 below rail");
    }

    /* Discontinuous enable + voice steal smoke: the shaper state reset keeps
     * the output finite and bounded through both transitions. */
    {
        EngineInit(); SetupPart();
        for (int i = 0; i < kNumVoices; ++i) EngineNoteOn(0, 440.0f + i, 127);
        EngineSetParam(0, ParamId::kDrive, 1.0f);    // discontinuous enable
        EngineNoteOn(0, 440.0f + kNumVoices, 127);   // 25th note -> steal
        std::vector<float> buf(kN);
        Render(buf.data(), kN);
        bool ok = true;
        for (float s : buf)
            if (!std::isfinite(s) || s > 1.0f || s < -1.0f) { ok = false; break; }
        Check(ok, "steal+enable: finite bounded output");
    }

    if (g_fail) {
        std::printf("%d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("PASS: engine render\n");
    return 0;
}
