/// @file midi.h
/// @brief MIDI note → frequency mapping for external surfaces.
///
/// CC → parameter mapping lives in the surface profile (nostromo/surface.h)
/// and the interaction layer (nostromo/interaction.h); this header carries
/// only the note → frequency conversion shared by the host and the target.

#pragma once

#include <cstdint>

namespace engine {

/// @brief MIDI note number → frequency in Hz (A4 = note 69 = 440 Hz).
/// @param note MIDI note number (0..127).
/// @return Frequency in Hz.
float MidiNoteToFreq(std::uint8_t note);

}  // namespace engine
