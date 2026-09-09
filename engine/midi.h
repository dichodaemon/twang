/// @file midi.h
/// @brief MIDI message → engine control mapping for external surfaces.
///
/// A controller layout is a swappable table of CC bindings; the handler is
/// layout-agnostic. Swap the layout to change control surfaces (X-Touch
/// Compact now, a different controller later) without touching the handler.
/// Notes use the standard MIDI note → frequency mapping. Control-side only
/// (the M33 on the target); never in the audio path.

#pragma once

#include <cstdint>

#include "engine.h"

namespace engine {

/// How a MIDI CC value maps to a parameter.
enum class MidiMode : std::uint8_t {
    kAbsolute,  ///< 7-bit absolute: value 0..127 → normalized 0..1.
    kRelative,  ///< endless encoder: two's-complement delta per detent.
};

/// One CC → parameter binding.
struct MidiBinding {
    std::uint8_t cc;      ///< MIDI CC number (0..127).
    std::uint8_t mode;    ///< MidiMode (kAbsolute or kRelative).
    ParamId param;        ///< Target parameter.
    std::uint8_t ring_cc; ///< LED-ring CC for feedback (relative; 0 = none).
};

/// A control-surface layout: the set of CC bindings plus the relative-encoder
/// sensitivity. A different controller is a different layout; the handler is
/// layout-agnostic.
struct MidiLayout {
    const char *name;             ///< Human-readable name (status display).
    const MidiBinding *bindings;  ///< CC → parameter table.
    int count;                    ///< Number of bindings.
    float rel_step;               ///< Normalized increment per relative detent.
    std::uint8_t channel;         ///< MIDI channel (0-based; 0 = channel 1).
};

/// Default layout: X-Touch Compact, Layer A "Mixer Control", channel 1.
/// Faders (CC 1–4) are absolute; encoders (CC 10–11) are relative.
extern const MidiLayout kXtouchCompact;

/// @brief MIDI note number → frequency in Hz (A4 = note 69 = 440 Hz).
/// @param note MIDI note number (0..127).
/// @return Frequency in Hz.
float MidiNoteToFreq(std::uint8_t note);

/// @brief Find the binding for a CC in a layout.
/// @param layout Controller layout.
/// @param cc CC number.
/// @return The binding, or nullptr if `cc` is unmapped.
const MidiBinding *MidiFind(const MidiLayout &layout, std::uint8_t cc);

/// @brief Apply one MIDI CC message to `part` (control thread).
/// @param layout Controller layout.
/// @param part Part index in [0, kNumParts).
/// @param cc CC number.
/// @param value 7-bit CC value.
void MidiCc(const MidiLayout &layout, int part, std::uint8_t cc,
            std::uint8_t value);

/// @brief Apply a MIDI note-on (control thread).
/// @param part Part index in [0, kNumParts).
/// @param note MIDI note number (0..127).
void MidiNoteOn(int part, std::uint8_t note);

/// @brief Apply a MIDI note-off (control thread).
/// @param part Part index in [0, kNumParts).
/// @param note MIDI note number (0..127).
void MidiNoteOff(int part, std::uint8_t note);

/// @brief Dispatch one raw MIDI message (status + two data bytes) to `part`.
///
/// Handles Control Change, Note On (velocity 0 treated as note-off), and
/// Note Off; other message types are ignored. Channel is ignored for now.
/// @param layout Controller layout (for CC messages).
/// @param part Part index in [0, kNumParts).
/// @param status MIDI status byte.
/// @param d1 First data byte (CC/note number).
/// @param d2 Second data byte (value/velocity).
void MidiMessage(const MidiLayout &layout, int part, std::uint8_t status,
                 std::uint8_t d1, std::uint8_t d2);

}  // namespace engine
