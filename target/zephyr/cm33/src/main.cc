// twang cm33 — control-core image: LVGL panel UI on the GLCDC display.
//
// Runs the portable controller (controller/ui.cc) against the Zephyr LVGL
// module and the EK-RA8D2's GLCDC + 7" 1024x600 RGB panel. The engine is
// linked so the UI's parameter readouts and note path compile and run; IPC to
// the audio (M85) core is the next step.

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "lvgl/lvgl.h"

#include "engine.h"
#include "ui.h"

int main(void) {
    // LVGL is auto-initialized (LV_Z_AUTO_INIT) against the Zephyr display
    // (zephyr,display = &lcdif), so no lv_init()/SDL setup is needed here.
    engine::EngineInit();

    Ui *ui = ui_create(lv_screen_active());
    if (!ui) {
        printk("ui_create failed\n");
        return -1;
    }

    // Smoke: a held A4 note drives the envelope playhead so the panel has
    // something to draw. (The scope tap is fed by the audio core over IPC,
    // which is not wired yet.)
    ui_note_on(ui, 440.0f);

    for (;;) {
        uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY) {
            delay = LV_DEF_REFR_PERIOD;
        }
        k_sleep(K_MSEC(delay));
    }
    return 0;
}
