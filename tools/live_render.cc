/**
 * Live audio: stream the engine to the default playback device, retriggering
 * a note periodically so the ADSR envelope is audible.
 *
 * Usage: live_render   (press Enter to stop)
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstdio>

#include "engine.h"
#include "params.h"

using namespace engine;

/* Note pattern (samples @ kSampleRate): 0.9 s held, 0.6 s released. */
constexpr ma_uint64 kNoteHeldSamples =
    static_cast<ma_uint64>(kSampleRate) * 9 / 10;
constexpr ma_uint64 kNotePeriodSamples =
    static_cast<ma_uint64>(kSampleRate) * 3 / 2;

static ma_uint64 g_frame = 0;  // audio-thread-only sample counter

static void AudioCallback(ma_device *, void *output, const void *,
                          ma_uint32 frame_count) {
    float *dst = static_cast<float *>(output);
    while (frame_count > 0) {
        ma_uint64 pos = g_frame % kNotePeriodSamples;
        ma_uint64 boundary = (pos < kNoteHeldSamples) ? kNoteHeldSamples
                                                      : kNotePeriodSamples;
        ma_uint32 n = static_cast<ma_uint32>(
            (boundary - pos) < frame_count ? (boundary - pos) : frame_count);

        if (pos == 0) EngineNoteOn(440.0f);
        Render(dst, static_cast<int>(n));
        g_frame += n;
        dst += n;
        frame_count -= n;

        if (g_frame % kNotePeriodSamples == kNoteHeldSamples)
            EngineNoteOff();
    }
}

int main() {
    EngineInit();
    EngineSetParam(ParamId::kCutoff, 0.4f);
    EngineSetParam(ParamId::kResonance, 0.25f);
    EngineSetParam(ParamId::kFilterEnvAmount, 0.5f);
    EngineSetParamDisp(ParamId::kAttack, 0.01f);
    EngineSetParamDisp(ParamId::kDecay, 0.3f);
    EngineSetParam(ParamId::kSustain, 0.6f);
    EngineSetParamDisp(ParamId::kRelease, 0.4f);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;   // matches Render()'s float out
    cfg.playback.channels = 1;
    cfg.sampleRate = kSampleRate;
    cfg.dataCallback = AudioCallback;

    ma_device device;
    if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) {
        std::fprintf(stderr, "Failed to open playback device\n");
        return 1;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        std::fprintf(stderr, "Failed to start playback\n");
        return 1;
    }

    std::printf("Retriggering note every %.1f s. Press Enter to stop.\n",
                static_cast<float>(kNotePeriodSamples) / kSampleRate);
    std::getchar();

    ma_device_uninit(&device);
    return 0;
}
