// host/main.cc — desktop simulator for the spike renderer.
//
// SDL window at the EK-RA8D2 in-box panel resolution (1024x600, RGB565),
// rendering the signal-flow panel (spike/panel.cc) — a port of the HTML
// mockup (ui/mockup) — through the SDL backend. Mouse drives PanelPointer.
//
// Audio runs through the audio-output abstraction: the UI (control thread)
// queues note events and parameter changes into the engine, and the audio
// thread renders them out the device, the same split live_render exercises.
#include <cstdio>

#include "audio_out.h"
#include "engine.h"
#include "midi_io.h"
#include "panel.h"
#include "sdl_backend.h"

constexpr int kHorRes = 1024;
constexpr int kVerRes = 600;

namespace {

// Audio thread: render the engine into the device's output buffer, then tap
// the samples into the panel's lock-free scope ring (display only, no
// latency). `user` is the Panel context (see main).
void AudioCallback(void *user, float *out, int frames) {
    engine::Render(out, frames);
    spike::PanelAudioTap(static_cast<spike::Panel *>(user), out, frames);
}

}  // namespace

int main() {
    spike::SdlBackend backend;
    if (!backend.Init(kHorRes, kVerRes)) {
        std::fprintf(stderr, "host: SDL init failed\n");
        return 1;
    }

    engine::EngineInit();

    // Build the panel first: it owns the scope ring the audio thread taps.
    spike::Panel *panel = spike::PanelCreate();

    audio::Output audio_out;
    if (!audio_out.Start(engine::kSampleRate, AudioCallback, panel))
        std::fprintf(stderr, "host: no playback device, running silent\n");

    MidiIo midi;
    midi.Init();

    while (!backend.quit) {
        backend.PollEvents(panel);
        midi.Poll(panel);
        midi.Feedback();
        spike::PanelDraw(panel, backend.fb, backend.BackIndex());
        backend.Present();
    }

    return 0;
}
