/// @file ui.h
/// @brief LVGL panel UI (signal-flow mockup port).
#pragma once

#include "lvgl/lvgl.h"

/// @brief Build the full panel UI onto the given screen and start the
/// animation timer. Call once after the SDL drivers are created.
/// @param screen The LVGL screen to build onto.
void ui_create(lv_obj_t *screen);

/// @brief Feed rendered audio samples into the scope display.
///
/// Called from the audio thread (the audio-output callback) after the engine
/// renders each buffer. The samples are tapped into a lock-free ring that the
/// UI reads when drawing the scope; this is display-only and adds no audio
/// latency.
/// @param samples Rendered samples (holds at least `n` floats).
/// @param n Number of samples.
void ui_audio_tap(const float *samples, int n);
