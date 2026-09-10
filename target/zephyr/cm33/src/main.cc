// twang cm33 — control-core image: spike panel UI on the GLCDC display.
//
// Runs the portable panel (spike/panel.cc) against the GLCDC's SDRAM
// framebuffer and the FT5336 touch panel. Note/param events are queued into
// the shared SDRAM IPC ring; a mailbox signal notifies the audio (M85) core.

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "engine.h"
#include "glcdc_backend.h"
#include "panel.h"

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

int main(void) {
    engine::EngineInit();

    spike::GlcdcBackend backend;
    if (!backend.Init()) {
        printk("glcdc backend init failed\n");
        return -1;
    }

    // The Panel is placement-new'd into SDRAM by PanelCreate (TWANG_UI_SDRAM).
    spike::Panel *panel = spike::PanelCreate();

    // Smoke: a held A4 note drives the envelope playhead so the panel has
    // something to draw. (The scope tap is fed by the audio core over IPC,
    // which is not wired yet.)
    spike::PanelNoteOn(panel, 440.0f, 127);

    for (;;) {
        backend.PollTouch(panel);
        spike::PanelDraw(panel, backend.fb, backend.back_);
        backend.Present();  // flip (blocks on vsync; double buffering)
    }
    return 0;
}
