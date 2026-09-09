/// @file audio_out_miniaudio.cc
/// @brief miniaudio backend for the audio output abstraction.
///
/// Mirrors the device setup in tools/live_render.cc: a mono float32 playback
/// device whose data callback forwards to the abstraction's render callback.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <new>

#include "audio_out.h"

namespace {

struct Backend {
    ma_device device;
    audio::Output::Callback callback;
    void *user;
};

void MiniaudioDataCallback(ma_device *device, void *output, const void *,
                           ma_uint32 frame_count) {
    auto *backend = static_cast<Backend *>(device->pUserData);
    backend->callback(backend->user, static_cast<float *>(output),
                      static_cast<int>(frame_count));
}

}  // namespace

namespace audio {

bool Output::Start(int sample_rate, Callback callback, void *user) {
    Stop();

    Backend *backend = new (std::nothrow) Backend{};
    if (!backend) return false;
    backend->callback = callback;
    backend->user = user;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;  // matches the engine's float out
    config.playback.channels = 1;
    config.sampleRate = static_cast<ma_uint32>(sample_rate);
    config.dataCallback = MiniaudioDataCallback;
    config.pUserData = backend;

    if (ma_device_init(nullptr, &config, &backend->device) != MA_SUCCESS) {
        delete backend;
        return false;
    }
    if (ma_device_start(&backend->device) != MA_SUCCESS) {
        ma_device_uninit(&backend->device);
        delete backend;
        return false;
    }
    impl_ = backend;
    return true;
}

void Output::Stop() {
    if (!impl_) return;
    auto *backend = static_cast<Backend *>(impl_);
    ma_device_uninit(&backend->device);
    delete backend;
    impl_ = nullptr;
}

Output::~Output() { Stop(); }

}  // namespace audio
