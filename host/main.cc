// host/main.cc — desktop simulator for the spike renderer.
//
// SDL window at the EK-RA8D2 in-box panel resolution (1024x600, RGB565),
// rendering the signal-flow panel (nostromo/panel.cc) — a port of the HTML
// mockup (ui/mockup) — through the SDL backend. Mouse drives PanelPointer.
//
// Audio runs through the audio-output abstraction: the UI (control thread)
// queues note events and parameter changes into the control engine, and the
// audio thread renders the audio engine out the device, the same split
// live_render exercises.
#include <cstdio>

#include "audio_out.h"
#include "engine_audio.h"
#include "engine_control.h"
#include "interaction.h"
#include "midi_io.h"
#include "panel.h"
#include "sdl_backend.h"
#include "surface.h"

constexpr int kHorRes = 1024;
constexpr int kVerRes = 600;

namespace {

// Audio-thread context: the audio engine to render, and the panel to tap.
struct AudioCtx {
    engine::EngineAudio *audio;
    nostromo::Panel *panel;
};

// Audio thread: render the engine into the device's output buffer, then tap
// the samples into the panel's lock-free scope ring (display only, no
// latency). `user` is an AudioCtx (see main).
void AudioCallback(void *user, float *out, int frames) {
    auto *ctx = static_cast<AudioCtx *>(user);
    engine::Render(*ctx->audio, out, frames);
    nostromo::PanelAudioTap(ctx->panel, out, frames);
}

}  // namespace

int main() {
    spike::SdlBackend backend;
    if (!backend.Init(kHorRes, kVerRes)) {
        std::fprintf(stderr, "host: SDL init failed\n");
        return 1;
    }

    // Shared transport + the two engine halves. The control side is driven by
    // the UI/main thread; the audio side is rendered by the audio thread.
    engine::SharedIpc ipc;
    engine::EngineControl control;
    control.Init(ipc);
    engine::EngineAudio audio{};
    audio.ipc = &ipc;

    // Build the panel first: it owns the scope ring the audio thread taps.
    nostromo::Panel *panel = nostromo::PanelCreate();

    // Bind the interaction layer to the panel, the X-Touch surface map, and
    // the control engine.
    nostromo::Interaction interaction;
    interaction.Init(panel, nostromo::Surface(), &control);

    AudioCtx ctx{&audio, panel};
    audio::Output audio_out;
    if (!audio_out.Start(engine::kSampleRate, AudioCallback, &ctx))
        std::fprintf(stderr, "host: no playback device, running silent\n");

    MidiIo midi;
    midi.Init();
    midi.control = &control;

    while (!backend.quit) {
        backend.PollEvents(panel, &interaction);
        midi.Poll(panel, &interaction);
        midi.Feedback();
        nostromo::PanelDraw(panel, backend.fb, backend.BackIndex());
        backend.Present();
    }

    return 0;
}
