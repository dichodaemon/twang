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

#include "engine_control.h"
#include "glcdc_backend.h"
#include "interaction.h"
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

int main(void) {
    engine::EngineControl control;
    control.Init(*reinterpret_cast<engine::SharedIpc *>(engine::kSharedIpcAddr));

    spike::GlcdcBackend backend;
    if (!backend.Init()) {
        printk("glcdc backend init failed\n");
        return -1;
    }

    // The Panel is placement-new'd into SDRAM by PanelCreate (TWANG_UI_SDRAM).
    nostromo::Panel *panel = nostromo::PanelCreate();
    nostromo::PanelSetEngine(panel, &control);

    // The panel reads navigation state (Nav()) and output/feel settings
    // through the interaction layer; without it PanelDraw dereferences a null
    // interaction pointer. Same wiring as the desktop host (host/main.cc).
    nostromo::Interaction interaction;
    interaction.Init(panel, nostromo::Surface(), &control);

    // Smoke: a held A4 note drives the envelope playhead so the panel has
    // something to draw (the audio core renders it from the shared ring).
    nostromo::PanelNoteOn(panel, 440.0f, 127);

    for (;;) {
        backend.PollTouch(panel);
        nostromo::PanelDraw(panel, backend.fb, backend.back_);
        backend.Present();  // flip (blocks on vsync; double buffering)
    }
    return 0;
}
