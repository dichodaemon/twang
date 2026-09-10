/// @file panel.h
/// @brief The Nostromo controller panel: chrome, plots, and input.
///
/// The spike renderer's top layer. A Panel owns the signal-flow layout
/// (titlebar, four modules, keyboard, nav tabs), the four dynamic plot
/// regions, the damage tracker, and the audio-tap scope ring. The backend
/// owns the framebuffer(s) and calls PanelDraw once per buffer; PanelPointer
/// is the only mutation path for touch/drag, and PanelNoteOn/PanelNoteOff
/// drive the engine and the envelope playhead.
#pragma once

#include <cstdint>

#include "fb.h"

namespace spike {

/// @brief Output-module display mode.
enum class ScopeMode : int { kScope = 0, kCycle, kSpectrum };

/// @brief Pointer event kind.
enum class PointerKind : int { kPress, kMove, kRelease };

/// @brief A pointer event in absolute framebuffer coordinates.
struct PointerEvent {
  PointerKind kind;  ///< Press / move / release.
  int x;             ///< Horizontal coordinate.
  int y;             ///< Vertical coordinate.
};

/// @brief A dynamic region: a rect plus a draw function and invalidation flag.
///
/// "Chrome says where; C says what." The draw function renders the live
/// content into the region; it runs only while `dirty` is set.
struct DynRegion {
  Rect rect;                                        ///< Region bounds.
  void (*draw)(FrameBuffer &fb, const Rect &r, void *state);  ///< Draw hook.
  void *state;                                      ///< Opaque state (Panel*).
  bool dirty;                                       ///< Needs a redraw.
};

/// @brief Per-plot column trace (previous vertical span), for column updates.
struct TraceState {
  std::uint8_t y0[230];  ///< Lower bound per column.
  std::uint8_t y1[230];  ///< Upper bound per column.
};

/// @brief Opaque panel state (owned by the caller; never freed in practice).
struct Panel;

/// @brief Draws a dynamic region if its invalidation flag is set.
///
/// @param d Region to possibly redraw.
/// @param fb Framebuffer to draw into.
void DrawDyn(DynRegion &d, FrameBuffer &fb);

/// @brief Allocates the panel (fixed SDRAM placement on the target).
///
/// @return The panel context (owned by the caller; never freed in practice).
Panel *PanelCreate();

/// @brief Repaints the panel into the current framebuffer.
///
/// Repaints only damage[n] ∪ damage[n−1] (the two-frame rule); the first draw
/// of a buffer draws the full chrome and all plots. The backend owns the
/// buffer swap and passes the back-buffer index (0 or 1) so the panel has a
/// single source of truth — it never toggles its own index.
///
/// @param p Panel context.
/// @param fb Framebuffer to draw into.
/// @param buffer_index Index of `fb` among the backend's two buffers (0 or 1).
void PanelDraw(Panel *p, FrameBuffer &fb, int buffer_index);

/// @brief Handles a touch/mouse event (filter XY pad, envelope handles,
/// keyboard, output-mode buttons).
///
/// @param p Panel context.
/// @param e Pointer event in framebuffer coordinates.
void PanelPointer(Panel *p, PointerEvent e);

/// @brief A note-on: drives the engine and starts the envelope playhead.
///
/// @param p Panel context.
/// @param freq_hz Note frequency in Hz.
void PanelNoteOn(Panel *p, float freq_hz);

/// @brief A note-off: releases the note and starts the playhead release.
///
/// @param p Panel context.
/// @param freq_hz Note frequency in Hz.
void PanelNoteOff(Panel *p, float freq_hz);

/// @brief Feeds rendered samples into the scope ring (audio thread).
///
/// @param p Panel context (holds the scope ring).
/// @param samples Rendered samples (at least `n` floats).
/// @param n Number of samples.
void PanelAudioTap(Panel *p, const float *samples, int n);

/// @brief How many times plot `idx` has been drawn (test/debug hook).
///
/// @param p Panel context.
/// @param idx Plot index (0 osc, 1 filter, 2 envelope, 3 output).
/// @return Draw count.
int PanelPlotDraws(const Panel *p, int idx);

}  // namespace spike
