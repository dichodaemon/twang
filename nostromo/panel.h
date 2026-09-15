/// @file panel.h
/// @brief The Nostromo controller panel: chrome, plots, and input.
///
/// The Nostromo GUI layer, built on the spike renderer. A Panel owns the
/// signal-flow layout (titlebar, four modules, keyboard, nav tabs), the four
/// dynamic plot regions, the damage tracker, and the audio-tap scope ring. The
/// backend owns the framebuffer(s) and calls PanelDraw once per buffer;
/// PanelPointer is the only mutation path for touch/drag, and
/// PanelNoteOn/PanelNoteOff drive the engine and the envelope playhead.
#pragma once

#include <cstdint>

#include "descriptor.h"
#include "fb.h"
#include "geom.h"
#include "screens.h"

namespace engine {
class EngineControl;  ///< defined in engine_control.h; the control-core engine
}

namespace nostromo {

using spike::FrameBuffer;
using spike::Rect;

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
/// content into the region; it runs only while `dirty` is set. This is the
/// descriptor interpreter's DYN slot (§5.4), supplied with the rect by the
/// screen descriptor and the hook/state by the panel.
using DynRegion = spike::DynSlot;

/// @brief Per-plot column trace (previous vertical span), for column updates.
///
/// Sized to the derived plot width; the element is uint16_t because kPlotH
/// (404) exceeds uint8_t's range. The sizes derive from geom, so changing the
/// plot width re-sizes the trace automatically — no silent size coupling.
struct TraceState {
  std::uint16_t y0[geom::kPlotW];  ///< Lower bound per column.
  std::uint16_t y1[geom::kPlotW];  ///< Upper bound per column.
};

/// @brief Opaque panel state (owned by the caller; never freed in practice).
struct Panel;

/// Forward decl: the interaction layer's state (nostromo/interaction.h).
struct Interaction;

/// @brief Allocates the panel (fixed SDRAM placement on the target).
///
/// @return The panel context (owned by the caller; never freed in practice).
Panel *PanelCreate();

/// @brief Bind the interaction layer to the panel (control thread, once).
///
/// The panel reads navigation state through this back-pointer to render the
/// pane/header chrome and DYN plots; the interaction layer sets it in
/// Interaction::Init.
///
/// @param p Panel context.
/// @param it Interaction context (owned by the caller).
void PanelSetInteraction(Panel *p, const Interaction *it);

/// @brief Bind the control-core engine to the panel (control thread, once).
///
/// The panel drives notes and parameters through this back-pointer (keyboard,
/// filter/env drag, and the per-frame engine sync). The interaction layer sets
/// it in Interaction::Init; a host without an interaction (the cm33 smoke) sets
/// it directly.
///
/// @param p Panel context.
/// @param control Control-core engine (owned by the caller).
void PanelSetEngine(Panel *p, engine::EngineControl *control);

/// @brief Marks a slot dirty, repainting it into both buffers.
///
/// The single invalidation entry point: the slot's plot and its readout band
/// are repainted together. `idx` names a plot slot (kSlotOsc..kSlotOut); the
/// mode slot (kSlotMode) is drawn by DrawChrome, not through this path.
///
/// @param p Panel context.
/// @param idx Plot slot to invalidate.
void MarkDirty(Panel *p, SlotIdx idx);

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
/// @param velocity MIDI velocity in [1, 127].
void PanelNoteOn(Panel *p, float freq_hz, std::uint8_t velocity);

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

}  // namespace nostromo
