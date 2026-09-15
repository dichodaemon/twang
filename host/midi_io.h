/// @file midi_io.h
/// @brief RtMidi transport: external controller ↔ engine control.
///
/// Sim-only (desktop): opens the X-Touch Compact's MIDI input and output,
/// maps incoming CCs to logical controls via the SurfaceProfile and feeds the
/// interaction layer (Interaction::OnInput); notes route through PanelNoteOn/Off.
/// Feedback (fader positions + LED rings) is sent back. The target swaps this
/// transport for Zephyr's MIDI stack; the surface map (nostromo/surface.h) is
/// shared.
///
/// All transport state lives in one MidiIo object owned by main(); there is no
/// file-scope or static mutable state.
#pragma once

#include "RtMidi.h"

namespace nostromo {
struct Panel;
struct Interaction;
}

namespace engine {
class EngineControl;
}

/// MIDI transport state: the open ports and the feedback dedup cache.
struct MidiIo {
    rt::midi::RtMidiIn *in = nullptr;
    rt::midi::RtMidiOut *out = nullptr;
    int last_sent[128];  ///< per feedback CC; -1 = never sent
    bool trace = false;  ///< dump every message to stderr (TWANG_MIDI_TRACE=1)
    engine::EngineControl *control = nullptr;  ///< set by main; Feedback reads it

    /// @brief Open the X-Touch Compact MIDI input and output; print status.
    /// No-op if no MIDI device is present.
    void Init();

    /// @brief Drain pending MIDI messages (control thread): CCs become
    /// Interaction::OnInput events via the surface map; notes route through
    /// PanelNoteOn/Off.
    /// @param panel Panel context (notes route through PanelNoteOn/Off).
    /// @param interaction Interaction layer to feed logical control events.
    void Poll(nostromo::Panel *panel, nostromo::Interaction *interaction);

    /// @brief Send current parameter values back to the controller (fader
    /// positions + LED rings). Call periodically from the main loop.
    void Feedback();
};
