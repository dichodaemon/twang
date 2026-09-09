/**
 * Minimal LVGL v9 configuration for the desktop simulator.
 * Every option not defined here uses the lv_conf_internal.h default
 * (asserts off, vector graphics off, software renderer on).
 */
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_USE_SDL 1  /* SDL window, mouse, keyboard, mousewheel drivers */

/* Fonts used by the panel UI: 10 px (axis labels, headers), 12 px
 * (readouts, titles). 14 px (Montserrat) is enabled by default. */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1

#endif /* LV_CONF_H */
#endif
