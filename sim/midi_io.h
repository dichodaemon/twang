/// @file midi_io.h
/// @brief RtMidi transport: external controller ↔ engine control.
///
/// Sim-only (desktop): opens the X-Touch Compact's MIDI input and output,
/// forwards incoming messages through engine::MidiMessage, and sends feedback
/// (fader positions + LED rings) back. The target swaps this transport for
/// Zephyr's MIDI stack; the handler (engine/midi.h) is shared.
#pragma once

/// @brief Open the X-Touch Compact MIDI input and output; print status.
/// No-op if no MIDI device is present.
void midi_init();

/// @brief Drain pending MIDI messages into the engine (control thread).
/// Call from the sim's main loop.
void midi_poll();

/// @brief Send current parameter values back to the controller (fader
/// positions + LED rings). Call periodically from the main loop.
void midi_feedback();
