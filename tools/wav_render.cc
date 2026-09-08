#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine.h"
#include "params.h"

using namespace engine;

static void PutU16(FILE *f, std::uint16_t v) { std::fwrite(&v, 1, 2, f); }
static void PutU32(FILE *f, std::uint32_t v) { std::fwrite(&v, 1, 4, f); }

int main(int argc, char **argv) {
    int seconds = (argc > 1) ? std::atoi(argv[1]) : 1;
    const char *path = (argc > 2) ? argv[2] : "out.wav";

    int frames = seconds * kSampleRate;
    std::vector<float> buf(frames);

    /* plucky patch: saw -> envelope-swept SVF -> ADSR */
    EngineInit();
    EngineSetParam(ParamId::kCutoff, 0.4f);
    EngineSetParam(ParamId::kResonance, 0.25f);
    EngineSetParam(ParamId::kFilterEnvAmount, 0.5f);
    EngineSetParamDisp(ParamId::kAttack, 0.01f);
    EngineSetParamDisp(ParamId::kDecay, 0.3f);
    EngineSetParam(ParamId::kSustain, 0.6f);
    EngineSetParamDisp(ParamId::kRelease, 0.4f);

    int note_frames = frames * 8 / 10;  /* held 80%, release the rest */
    EngineNoteOn(440.0f);
    Render(buf.data(), note_frames);
    EngineNoteOff();
    Render(buf.data() + note_frames, frames - note_frames);

    FILE *f = std::fopen(path, "wb");
    if (!f) return 1;

    std::uint32_t data_bytes = (std::uint32_t)frames * 2;
    std::uint32_t riff_size = 36 + data_bytes;
    std::fwrite("RIFF", 1, 4, f); PutU32(f, riff_size); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); PutU32(f, 16);
    PutU16(f, 1);                      /* PCM */
    PutU16(f, 1);                      /* mono */
    PutU32(f, kSampleRate);
    PutU32(f, kSampleRate * 2);        /* byte rate */
    PutU16(f, 2);                      /* block align */
    PutU16(f, 16);                     /* bits/sample */
    std::fwrite("data", 1, 4, f); PutU32(f, data_bytes);

    for (int i = 0; i < frames; ++i) {
        float s = buf[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        std::int16_t pcm = (std::int16_t)(s * 32767.0f);
        std::fwrite(&pcm, 1, 2, f);
    }

    std::fclose(f);
    std::printf("Wrote %d s (%d samples) to %s\n", seconds, frames, path);
    return 0;
}
