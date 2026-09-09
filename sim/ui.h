/// @file ui.h
/// @brief LVGL panel UI (signal-flow mockup port).
#pragma once

#include "lvgl/lvgl.h"

/// @brief Build the full panel UI onto the given screen and start the
/// animation timer. Call once after the SDL drivers are created.
/// @param screen The LVGL screen to build onto.
void ui_create(lv_obj_t *screen);

/// @brief A note-on from any controller (keyboard, MIDI, …): drives the
/// engine and starts the envelope playhead.
/// @param freq_hz Note frequency in Hz.
void ui_note_on(float freq_hz);

/// @brief A note-off from any controller: releases the note and starts the
/// playhead's release segment.
/// @param freq_hz Note frequency in Hz.
void ui_note_off(float freq_hz);

/// @brief Feed rendered audio samples into the scope display.
///
/// Called from the audio thread (the audio-output callback) after the engine
/// renders each buffer. The samples are tapped into a lock-free ring that the
/// UI reads when drawing the scope; this is display-only and adds no audio
/// latency.
/// @param samples Rendered samples (holds at least `n` floats).
/// @param n Number of samples.
void ui_audio_tap(const float *samples, int n);
