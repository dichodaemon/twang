/**
 * Desktop LVGL simulator.
 *
 * SDL window at the EK-RA8D2 in-box panel resolution (1024x600, 24-bit RGB
 * parallel). Builds the signal-flow panel UI (controller/ui.cc) — a port of the
 * HTML mockup (ui/mockup) — and drives it with the SDL mouse/touch drivers.
 *
 * Audio runs through the audio-output abstraction: the UI (control thread)
 * queues note events and parameter changes into the engine, and the audio
 * thread renders them out the device, the same split live_render exercises.
 */
#include <cstdint>
#include <cstdio>

#include "lvgl/lvgl.h"

#include "audio_out.h"
#include "engine.h"
#include "midi_io.h"
#include "ui.h"

constexpr int kHorRes = 1024;
constexpr int kVerRes = 600;

namespace {

// Audio thread: render the engine into the device's output buffer, then tap
// the samples into the scope's lock-free ring (display only, no latency).
// `user` is the Ui context (see main), so the tap routes into its scope ring.
void AudioCallback(void *user, float *out, int frames) {
    engine::Render(out, frames);
    ui_audio_tap(static_cast<Ui *>(user), out, frames);
}

}  // namespace

int main() {
    lv_init();

    lv_sdl_window_create(kHorRes, kVerRes);
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();
    lv_sdl_mousewheel_create();

    engine::EngineInit();

    // Build the UI first: it owns the scope ring the audio thread taps into.
    Ui *ui = ui_create(lv_screen_active());

    audio::Output audio_out;
    if (!audio_out.Start(engine::kSampleRate, AudioCallback, ui))
        std::fprintf(stderr, "host: no playback device, running silent\n");

    MidiIo midi;
    midi.Init();

    for (;;) {
        std::uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY) delay = LV_DEF_REFR_PERIOD;
        midi.Poll(ui);
        midi.Feedback();
        lv_delay_ms(delay);
    }

    lv_deinit();
    return 0;
}
