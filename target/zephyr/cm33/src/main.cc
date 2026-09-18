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
#include "midi.h"       // engine::MidiNoteToFreq
#include "midi_ring.h"
#include "loss_counters.h"
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

/// Drain the cross-core MIDI ring and feed the interaction layer exactly as the
/// desktop host does (host/midi_io.cc Poll): CCs map through the surface
/// profile to logical controls (Interaction::OnInput); notes route through
/// PanelNoteOn/Off. This is the normative path — engine::MidiMessage and
/// engine::kXtouchCompact are the stale pre-surface mapping and are not used.
void DrainMidi(nostromo::Panel *panel, nostromo::Interaction *interaction,
               MidiRing *ring)
{
    const nostromo::SurfaceProfile &surface = nostromo::Surface();
    uint32_t word;
    while (ring->Pop(&word)) {
        struct midi_ump ump = {};
        ump.data[0] = word;
        if (UMP_MT(ump) != UMP_MT_MIDI1_CHANNEL_VOICE) {
            reinterpret_cast<LossCounters *>(kLossCountersAddr)
                ->mt_reject.fetch_add(1, std::memory_order_relaxed);
            continue;  // SysEx / MIDI 2.0 (multi-word) not handled yet
        }
        const uint8_t status = UMP_MIDI_STATUS(ump);
        const uint8_t d1 = UMP_MIDI1_P1(ump);
        const uint8_t d2 = UMP_MIDI1_P2(ump);
        if ((status & 0x0F) != 0) {
            reinterpret_cast<LossCounters *>(kLossCountersAddr)
                ->channel_reject.fetch_add(1, std::memory_order_relaxed);
            continue;  // wrong channel (X-Touch speaks on channel 1)
        }
        switch (status & 0xF0) {
        case 0xB0: {  // Control Change → logical control via the surface map
            const nostromo::ControlMap *m = nostromo::FindControl(surface, d1);
            if (!m) {
                break;  // unmapped CC (faders and surplus controls)
            }
            nostromo::InputEvent ev{};
            ev.control = m->logical;
            ev.t_ms = static_cast<std::uint32_t>(k_uptime_get());
            if (m->turn) {
                ev.detents = static_cast<std::int8_t>(
                    nostromo::DecodeEnc(d2, m->enc));
                ev.edge = nostromo::Edge::kNone;
            } else {
                ev.detents = 0;
                ev.edge = (d2 > 0) ? nostromo::Edge::kDown
                                   : nostromo::Edge::kUp;
            }
            interaction->OnInput(ev);
            break;
        }
        case 0x90:  // Note On (velocity 0 = note off)
            if (d2 == 0) {
                nostromo::PanelNoteOff(panel, engine::MidiNoteToFreq(d1));
            } else {
                nostromo::PanelNoteOn(panel, engine::MidiNoteToFreq(d1), d2);
            }
            break;
        case 0x80:  // Note Off
            nostromo::PanelNoteOff(panel, engine::MidiNoteToFreq(d1));
            break;
        default:
            break;
        }
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
    reinterpret_cast<LossCounters *>(kLossCountersAddr)->Reset();

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

    for (;;) {
        DrainMidi(panel, &interaction, midi_ring);  // USB MIDI -> surface map

        backend.PollTouch(panel);
        nostromo::PanelDraw(panel, backend.fb, backend.back_);
        backend.Present();  // flip (blocks on vsync; double buffering)
    }
    return 0;
}
