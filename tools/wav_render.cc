#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine_audio.h"
#include "engine_control.h"
#include "params.h"

using namespace engine;

static void PutU16(FILE *f, std::uint16_t v) { std::fwrite(&v, 1, 2, f); }
static void PutU32(FILE *f, std::uint32_t v) { std::fwrite(&v, 1, 4, f); }

int main(int argc, char **argv) {
    int seconds = (argc > 1) ? std::atoi(argv[1]) : 1;
    const char *path = (argc > 2) ? argv[2] : "out.wav";

    int frames = seconds * kSampleRate;
    std::vector<float> buf(frames);

    SharedIpc ipc;
    EngineControl control;
    control.Init(ipc);
    EngineAudio audio{};
    audio.ipc = &ipc;

    /* plucky patch: saw -> envelope-swept SVF -> ADSR */
    control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.4f);
    control.SetParam(0, ParamRef{0, ParamId::kResonance}, 0.25f);
    control.SetRoute(0, 2, ModSourceId::kEnv1, ParamRef{0, ParamId::kCutoff}, 0.5f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kAttack}, 0.01f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kDecay}, 0.3f);
    control.SetParam(0, ParamRef{0, ParamId::kSustain}, 0.6f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kRelease}, 0.4f);

    int note_frames = frames * 8 / 10;  /* held 80%, release the rest */
    control.NoteOn(0, 440.0f, 127);
    Render(audio, buf.data(), note_frames);
    control.NoteOff(0, 440.0f);
    Render(audio, buf.data() + note_frames, frames - note_frames);

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
