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

#include "clock.h"
#include "engine_control.h"
#include "glcdc_backend.h"
#include "interaction.h"
#include "midi_ring.h"
#include "loss_counters.h"
#include "panel.h"
#include "sdram_map.h"
#include "surface.h"

// Touch event queue: PollTouch (UI loop) posts PointerEvents; the control
// thread drains them (the sole PanelPointer/param producer). cm33-local, so a
// plain kernel msgq suffices (no fixed SDRAM address).
K_MSGQ_DEFINE(touch_events, sizeof(nostromo::PointerEvent), 8, 4);

// Signal the audio core (M85) on mbox0 after a note event is queued into the
// shared SDRAM ring. Passed to EngineControl::Init as the EventNotify notifier.
static void SignalAudioCore() {
    const struct device *mbox = DEVICE_DT_GET(DT_NODELABEL(mbox0));
    if (mbox && device_is_ready(mbox)) {
        mbox_send(mbox, 0, NULL);  // signal channel 0 (msg == NULL)
    }
}

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
    LossCounters *loss = reinterpret_cast<LossCounters *>(kLossCountersAddr);
    if (UMP_MT(ump) != UMP_MT_MIDI1_CHANNEL_VOICE) {
        loss->last_reject_word.store(word, std::memory_order_relaxed);
        loss->mt_reject.fetch_add(1, std::memory_order_relaxed);
        return;  // SysEx / MIDI 2.0 (multi-word) not handled yet
    }
    if (UMP_GROUP(ump) != 0) {
        loss->group_reject.fetch_add(1, std::memory_order_relaxed);
        return;  // one group terminal declared — a stray group is not ours
    }
    const uint8_t status = UMP_MIDI_STATUS(ump);
    const uint8_t d1 = UMP_MIDI1_P1(ump);
    const uint8_t d2 = UMP_MIDI1_P2(ump);
    // CC 123 (All Notes Off) panic path: handled before the channel filter so
    // a note stuck by a channel mismatch is still releasable; every part.
    if ((status & 0xF0) == 0xB0 && d1 == 123) {
        for (int p = 0; p < engine::kNumParts; ++p) {
            interaction->control->AllNotesOff(p);
        }
        return;
    }
    if ((status & 0x0F) != 0) {
        loss->channel_reject.fetch_add(1, std::memory_order_relaxed);
        return;  // wrong channel (X-Touch speaks on channel 1)
    }
    switch (status & 0xF0) {
    case 0xB0: {  // Control Change → logical control via the surface map
        const nostromo::ControlMap *m = nostromo::FindControl(surface, d1);
        if (!m) {
            return;  // unmapped CC (faders and surplus controls)
        }
        nostromo::InputEvent ev{};
        ev.control = m->logical;
        ev.t_ms = nostromo::NowMs();
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
            loss->note_off_count.fetch_add(1, std::memory_order_relaxed);
            loss->last_note_off_note.store(d1, std::memory_order_relaxed);
            nostromo::PanelNoteOff(panel, d1);
        } else {
            loss->note_on_count.fetch_add(1, std::memory_order_relaxed);
            nostromo::PanelNoteOn(panel, d1, d2);
        }
        return;
    case 0x80:  // Note Off
        loss->note_off_count.fetch_add(1, std::memory_order_relaxed);
        loss->last_note_off_note.store(d1, std::memory_order_relaxed);
        nostromo::PanelNoteOff(panel, d1);
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

    // Wait for the producer (cm85) to finish resetting the MIDI rings before
    // the first drain. The boot magic replaces the former implicit assumption
    // that GLCDC init on this core outlasts cm85's early reset.
    LossCounters *loss = reinterpret_cast<LossCounters *>(kLossCountersAddr);
    constexpr int kBootWaitMs = 1000;
    int waited = 0;
    while (waited < kBootWaitMs &&
           loss->boot_magic.load(std::memory_order_acquire) != kBootMagic) {
        k_msleep(1);
        ++waited;
    }
    if (loss->boot_magic.load(std::memory_order_acquire) != kBootMagic) {
        printk("boot: cm85 ring reset not observed; draining anyway\n");
    }

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
    control.Init(*reinterpret_cast<engine::SharedIpc *>(kSharedIpcAddr),
                 SignalAudioCore);

    // MIDI input rings: reset by the producer (cm85) before USB init; this
    // core's control thread waits on the boot magic before its first drain.
    NoteRing *note_ring = reinterpret_cast<NoteRing *>(kNoteRingAddr);
    CcRing *cc_ring = reinterpret_cast<CcRing *>(kCcRingAddr);

    spike::GlcdcBackend backend;
    if (!backend.Init()) {
        printk("glcdc backend init failed\n");
        return -1;
    }

    // The Panel is placement-new'd into SDRAM at the scope-tap base, so the
    // audio core reaches its scope ring at the fixed address it also uses.
    nostromo::Panel *panel =
        nostromo::PanelCreateAt(reinterpret_cast<void *>(kScopeTapAddr));

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
