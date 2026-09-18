// twang cm33 — control-core image: spike panel UI on the GLCDC display.
//
// Runs the portable panel (nostromo/panel.cc) against the GLCDC's SDRAM
// framebuffer and the FT5336 touch panel. Owns the control-core engine; note/
// param events are queued into the shared SDRAM IPC ring and a mailbox signal
// notifies the audio (M85) core.

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/audio/midi.h>  // UMP struct + extraction macros

#include <cstdint>

#include "engine_control.h"
#include "glcdc_backend.h"
#include "interaction.h"
#include "midi.h"       // engine::MidiMessage + kXtouchCompact
#include "midi_ring.h"
#include "panel.h"
#include "surface.h"

namespace engine {

// Override the engine's weak notification hook: ping the audio core (M85) on
// mbox0 after a note-on/note-off is queued into the shared SDRAM ring.
void EngineEventsPending() {
    const struct device *mbox = DEVICE_DT_GET(DT_NODELABEL(mbox0));
    if (mbox && device_is_ready(mbox)) {
        mbox_send(mbox, 0, NULL);  // signal channel 0 (msg == NULL)
    }
}

}  // namespace engine

namespace {

/// Drain the cross-core MIDI ring (USB MIDI rx on the audio core) and dispatch
/// each MIDI 1.0 channel-voice message into the engine's control side.
void DrainMidi(engine::EngineControl &control, MidiRing *ring)
{
    uint32_t word;
    while (ring->Pop(&word)) {
        struct midi_ump ump = {};
        ump.data[0] = word;
        if (UMP_MT(ump) != UMP_MT_MIDI1_CHANNEL_VOICE) {
            continue;  // SysEx / MIDI 2.0 (multi-word) not handled yet
        }
        const uint8_t status = UMP_MIDI_STATUS(ump);
        const uint8_t d1 = UMP_MIDI1_P1(ump);
        const uint8_t d2 = UMP_MIDI1_P2(ump);
        engine::MidiMessage(control, engine::kXtouchCompact, 0, status, d1, d2);
    }
}

}  // namespace

int main(void) {
    engine::EngineControl control;
    control.Init(*reinterpret_cast<engine::SharedIpc *>(engine::kSharedIpcAddr));

    // MIDI input ring: reset before any traffic (the SDRAM backing is
    // uninitialized; the audio core also resets it, idempotently).
    MidiRing *midi_ring = reinterpret_cast<MidiRing *>(kMidiRingAddr);
    midi_ring->Reset();

    spike::GlcdcBackend backend;
    if (!backend.Init()) {
        printk("glcdc backend init failed\n");
        return -1;
    }

    // The Panel is placement-new'd into SDRAM by PanelCreate (TWANG_UI_SDRAM).
    nostromo::Panel *panel = nostromo::PanelCreate();

    // Bind the interaction layer + control engine through Interaction::Init —
    // the single wiring point shared with the desktop host (host/main.cc). The
    // panel reads navigation/output state through the interaction layer, so a
    // panel drawn without it null-dereferences on the first frame.
    nostromo::Interaction interaction;
    interaction.Init(panel, nostromo::Surface(), &control);

    // Smoke: a held A4 note drives the envelope playhead so the panel has
    // something to draw (the audio core renders it from the shared ring).
    nostromo::PanelNoteOn(panel, 440.0f, 127);

    for (;;) {
        DrainMidi(control, midi_ring);  // USB MIDI -> engine notes/params

        backend.PollTouch(panel);
        nostromo::PanelDraw(panel, backend.fb, backend.back_);
        backend.Present();  // flip (blocks on vsync; double buffering)
    }
    return 0;
}
