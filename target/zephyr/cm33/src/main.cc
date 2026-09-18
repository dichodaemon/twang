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

// Touch event queue: PollTouch (UI loop) posts PointerEvents; the control
// thread drains them (the sole PanelPointer/param producer). cm33-local, so a
// plain kernel msgq suffices (no fixed SDRAM address).
K_MSGQ_DEFINE(touch_events, sizeof(nostromo::PointerEvent), 8, 4);

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

/// Process one UMP word exactly as the desktop host does (host/midi_io.cc
/// Poll): CCs map through the surface profile to logical controls
/// (Interaction::OnInput); notes route through PanelNoteOn/Off. This is the
/// normative path — engine::MidiMessage and engine::kXtouchCompact are the
/// stale pre-surface mapping and are not used.
void HandleMidiWord(nostromo::Panel *panel, nostromo::Interaction *interaction,
                    const nostromo::SurfaceProfile &surface, uint32_t word)
{
    struct midi_ump ump = {};
    ump.data[0] = word;
    if (UMP_MT(ump) != UMP_MT_MIDI1_CHANNEL_VOICE) {
        reinterpret_cast<LossCounters *>(kLossCountersAddr)
            ->mt_reject.fetch_add(1, std::memory_order_relaxed);
        return;  // SysEx / MIDI 2.0 (multi-word) not handled yet
    }
    const uint8_t status = UMP_MIDI_STATUS(ump);
    const uint8_t d1 = UMP_MIDI1_P1(ump);
    const uint8_t d2 = UMP_MIDI1_P2(ump);
    if ((status & 0x0F) != 0) {
        reinterpret_cast<LossCounters *>(kLossCountersAddr)
            ->channel_reject.fetch_add(1, std::memory_order_relaxed);
        return;  // wrong channel (X-Touch speaks on channel 1)
    }
    switch (status & 0xF0) {
    case 0xB0: {  // Control Change → logical control via the surface map
        if (d1 == 123) {  // CC 123 (All Notes Off) — panic path
            interaction->control->AllNotesOff(0);
            return;
        }
        const nostromo::ControlMap *m = nostromo::FindControl(surface, d1);
        if (!m) {
            return;  // unmapped CC (faders and surplus controls)
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
        return;
    }
    case 0x90:  // Note On (velocity 0 = note off)
        if (d2 == 0) {
            nostromo::PanelNoteOff(panel, engine::MidiNoteToFreq(d1));
        } else {
            nostromo::PanelNoteOn(panel, engine::MidiNoteToFreq(d1), d2);
        }
        return;
    case 0x80:  // Note Off
        nostromo::PanelNoteOff(panel, engine::MidiNoteToFreq(d1));
        return;
    default:
        return;
    }
}

/// Drain the two cross-core MIDI rings, note ring first — a held note's off
/// must not wait behind a fader sweep in the CC ring.
void DrainMidi(nostromo::Panel *panel, nostromo::Interaction *interaction,
               NoteRing *note, CcRing *cc)
{
    const nostromo::SurfaceProfile &surface = nostromo::Surface();
    uint32_t word;
    while (note->Pop(&word)) {
        HandleMidiWord(panel, interaction, surface, word);
    }
    while (cc->Pop(&word)) {
        HandleMidiWord(panel, interaction, surface, word);
    }
}

// Control-thread context: the objects the control thread drives.
struct ControlCtx {
    nostromo::Panel *panel;
    nostromo::Interaction *interaction;
    NoteRing *note_ring;
    CcRing *cc_ring;
    struct k_msgq *touch_events;
};

constexpr int kControlStackBytes = 4096;
// Cooperative priority: higher than the render loop (main thread, prio 0), so
// the control thread preempts PanelDraw's spectrum FFT — MIDI is not gated by
// the display frame rate.
constexpr int kControlPrio = -1;

// The control thread is the sole producer on the IPC rings: it drains the MIDI
// ring (OnInput -> SetParam/SetRoute; PanelNoteOn/Off -> events.Push) and the
// touch queue (PanelPointer -> SetParam).
void ControlThread(void *arg1, void *, void *) {
    auto *ctx = static_cast<ControlCtx *>(arg1);
    for (;;) {
        DrainMidi(ctx->panel, ctx->interaction, ctx->note_ring, ctx->cc_ring);
        nostromo::PointerEvent e;
        while (k_msgq_get(ctx->touch_events, &e, K_NO_WAIT) == 0) {
            nostromo::PanelPointer(ctx->panel, e);
        }
        k_msleep(1);  // 1 ms poll; the drain rate is far above the input rate
    }
}

}  // namespace

int main(void) {
    engine::EngineControl control;
    control.Init(*reinterpret_cast<engine::SharedIpc *>(engine::kSharedIpcAddr));

    // MIDI input rings: reset before any traffic (the SDRAM backing is
    // uninitialized; the audio core also resets them, idempotently).
    NoteRing *note_ring = reinterpret_cast<NoteRing *>(kNoteRingAddr);
    CcRing *cc_ring = reinterpret_cast<CcRing *>(kCcRingAddr);
    note_ring->Reset();
    cc_ring->Reset();
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

    // Start the control thread (the sole IPC producer) after the setup above.
    static ControlCtx cctx{panel, &interaction, note_ring, cc_ring, &touch_events};
    static K_THREAD_STACK_DEFINE(control_stack, kControlStackBytes);
    static struct k_thread control_thread;
    k_thread_create(&control_thread, control_stack,
                    K_THREAD_STACK_SIZEOF(control_stack),
                    ControlThread, &cctx, NULL, NULL, kControlPrio, 0,
                    K_NO_WAIT);

    for (;;) {
        backend.PollTouch(panel, &touch_events);  // post (control thread drains)
        nostromo::PanelDraw(panel, backend.fb, backend.back_);
        backend.Present();  // flip (blocks on vsync; double buffering)
    }
    return 0;
}
