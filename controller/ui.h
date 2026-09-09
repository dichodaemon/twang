/// @file ui.h
/// @brief LVGL panel UI (signal-flow mockup port).
///
/// All mutable UI state lives in one `Ui` object, created by ui_create() and
/// owned by main(). Draw callbacks, event handlers, and the audio tap receive
/// the Ui* explicitly (through LVGL user_data / miniaudio pUserData); there is
/// no file-scope or static mutable state.
#pragma once

#include <cstdint>

#include "lvgl/lvgl.h"
#include "scope_ring.h"

/// Plot module index (also the index into Ui::plots / Ui::plot_objs).
enum class PlotKind : int { kOsc = 0, kFilter, kEnv, kOut, kCount };

/// Output-module display mode.
enum class ScopeMode : int { kScope = 0, kCycle, kSpectrum, kCount };

/// Cached mirror of engine state + note/drag transients (control thread only).
struct UiState {
    float freq = 440.0f;
    float cutoff = 0.0f;
    float resonance = 0.0f;
    float attack = 0.0f;
    float decay = 0.0f;
    float sustain = 0.0f;
    float release = 0.0f;

    bool note_on = false;
    std::uint32_t note_at = 0;     ///< lv_tick_get() at note-on (ms)
    std::uint32_t release_at = 0;  ///< lv_tick_get() at note-off (ms)
    float release_from = 0.0f;     ///< envelope level when released

    float phase = 0.0f;            ///< shared oscillator phase [0,1)
    float scope_peak = 0.0f;       ///< peak amplitude of the scope window
};

/// Sizes for the output-module draw scratch (owned by Ui, see below).
inline constexpr int kCycleBufSize = 4096;
inline constexpr int kFftSize = 8192;

struct Ui;

/// Per-plot data hung off each plot object's user_data.
struct PlotData {
    PlotKind kind;
    Ui *ui;          ///< owning context
    int drag_handle; ///< env drag: -1 none, 0 attack, 1 decay, 2 release
};

/// Single owner of all UI state, widgets, and draw scratch. One instance per
/// process; created by ui_create(), never copied or moved.
struct Ui {
    UiState state;
    PlotData plots[static_cast<int>(PlotKind::kCount)];
    lv_obj_t *plot_objs[static_cast<int>(PlotKind::kCount)] = {};
    lv_obj_t *readouts[static_cast<int>(PlotKind::kCount)] = {};
    ScopeRing scope_ring;  ///< audio thread → UI scope tap
    ScopeMode scope_mode = ScopeMode::kScope;
    lv_obj_t *mode_btns[static_cast<int>(ScopeMode::kCount)] = {};
    lv_obj_t *mode_labels[static_cast<int>(ScopeMode::kCount)] = {};

    // UI-thread draw scratch: owned here rather than file-scope statics or
    // per-frame stack allocations. Single-threaded (draw runs on the UI
    // thread only); do not make the draw path concurrent without reworking.
    float cycle_buf[kCycleBufSize];
    float fft_re[kFftSize];
    float fft_im[kFftSize];
    float fft_mag[kFftSize / 2];
};

/// @brief Build the full panel UI onto the given screen and start the
/// animation timer. Call once after the SDL drivers are created.
/// @param screen The LVGL screen to build onto.
/// @return The Ui context (owned by the caller; never freed in practice).
Ui *ui_create(lv_obj_t *screen);

/// @brief A note-on from any controller (keyboard, MIDI, …): drives the
/// engine and starts the envelope playhead.
/// @param ui UI context.
/// @param freq_hz Note frequency in Hz.
void ui_note_on(Ui *ui, float freq_hz);

/// @brief A note-off from any controller: releases the note and starts the
/// playhead's release segment.
/// @param ui UI context.
/// @param freq_hz Note frequency in Hz.
void ui_note_off(Ui *ui, float freq_hz);

/// @brief Feed rendered audio samples into the scope display.
///
/// Called from the audio thread (the audio-output callback) after the engine
/// renders each buffer. The samples are tapped into a lock-free ring that the
/// UI reads when drawing the scope; this is display-only and adds no audio
/// latency.
/// @param ui UI context (holds the scope ring).
/// @param samples Rendered samples (holds at least `n` floats).
/// @param n Number of samples.
void ui_audio_tap(Ui *ui, const float *samples, int n);
