/// @file midi_io.h
/// @brief RtMidi transport: external controller ↔ engine control.
///
/// Sim-only (desktop): opens the X-Touch Compact's MIDI input and output,
/// forwards incoming messages through engine::MidiMessage, and sends feedback
/// (fader positions + LED rings) back. The target swaps this transport for
/// Zephyr's MIDI stack; the handler (engine/midi.h) is shared.
///
/// All transport state lives in one MidiIo object owned by main(); there is no
/// file-scope or static mutable state.
#pragma once

#include "RtMidi.h"

namespace spike {
struct Panel;
}

/// MIDI transport state: the open ports and the feedback dedup cache.
struct MidiIo {
    rt::midi::RtMidiIn *in = nullptr;
    rt::midi::RtMidiOut *out = nullptr;
    int last_sent[128];  ///< per feedback CC; -1 = never sent

    /// @brief Open the X-Touch Compact MIDI input and output; print status.
    /// No-op if no MIDI device is present.
    void Init();

    /// @brief Drain pending MIDI messages into the engine (control thread).
    /// @param panel Panel context (notes route through PanelNoteOn/Off).
    void Poll(spike::Panel *panel);

    /// @brief Send current parameter values back to the controller (fader
    /// positions + LED rings). Call periodically from the main loop.
    void Feedback();
};
