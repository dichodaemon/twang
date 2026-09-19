/**
 * Live audio: stream the engine to the default playback device, retriggering
 * a note periodically so the ADSR envelope is audible.
 *
 * Usage: live_render   (press Enter to stop)
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstdio>

#include "engine_audio.h"
#include "engine_control.h"
#include "params.h"

using namespace engine;

/* Note pattern (samples @ kSampleRate): 0.9 s held, 0.6 s released. */
constexpr ma_uint64 kNoteHeldSamples =
    static_cast<ma_uint64>(kSampleRate) * 9 / 10;
constexpr ma_uint64 kNotePeriodSamples =
    static_cast<ma_uint64>(kSampleRate) * 3 / 2;

// Audio-thread context, handed through ma_device::pUserData.
struct LiveCtx {
    EngineControl *control;
    EngineAudio *audio;
    ma_uint64 frame = 0;  // audio-thread-only sample counter
};

static void AudioCallback(ma_device *device, void *output, const void *,
                          ma_uint32 frame_count) {
    LiveCtx *ctx = static_cast<LiveCtx *>(device->pUserData);
    float *dst = static_cast<float *>(output);
    while (frame_count > 0) {
        ma_uint64 pos = ctx->frame % kNotePeriodSamples;
        ma_uint64 boundary = (pos < kNoteHeldSamples) ? kNoteHeldSamples
                                                      : kNotePeriodSamples;
        ma_uint32 n = static_cast<ma_uint32>(
            (boundary - pos) < frame_count ? (boundary - pos) : frame_count);

        if (pos == 0) ctx->control->NoteOn(0, 69, 440.0f, 127);
        Render(*ctx->audio, dst, static_cast<int>(n));
        ctx->frame += n;
        dst += n;
        frame_count -= n;

        if (ctx->frame % kNotePeriodSamples == kNoteHeldSamples)
            ctx->control->NoteOff(0, 69);
    }
}

int main() {
    SharedIpc ipc;
    EngineControl control;
    control.Init(ipc);
    EngineAudio audio{};
    audio.ipc = &ipc;

    control.SetParam(0, ParamRef{0, ParamId::kCutoff}, 0.4f);
    control.SetParam(0, ParamRef{0, ParamId::kResonance}, 0.25f);
    control.SetRoute(0, 2, ModSourceId::kEnv1, ParamRef{0, ParamId::kCutoff}, 0.5f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kAttack}, 0.01f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kDecay}, 0.3f);
    control.SetParam(0, ParamRef{0, ParamId::kSustain}, 0.6f);
    control.SetParamDisp(0, ParamRef{0, ParamId::kRelease}, 0.4f);

    LiveCtx ctx{&control, &audio, 0};

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;   // matches Render()'s float out
    cfg.playback.channels = 1;
    cfg.sampleRate = kSampleRate;
    cfg.dataCallback = AudioCallback;
    cfg.pUserData = &ctx;

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
