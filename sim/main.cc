/**
 * Desktop LVGL simulator.
 *
 * SDL window at the EK-RA8D2 in-box panel resolution (1024x600, 24-bit RGB
 * parallel). Shows a minimal screen; replace with the real UI later.
 */
#include <cstdint>

#include "lvgl/lvgl.h"

constexpr int kHorRes = 1024;
constexpr int kVerRes = 600;

int main() {
    lv_init();

    lv_sdl_window_create(kHorRes, kVerRes);
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();
    lv_sdl_mousewheel_create();

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Synth");
    lv_obj_center(label);

    for (;;) {
        std::uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY) delay = LV_DEF_REFR_PERIOD;
        lv_delay_ms(delay);
    }

    lv_deinit();
    return 0;
}
