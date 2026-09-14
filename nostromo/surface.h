/// @file surface.h
/// @brief Physical→logical control surface mapping for the interaction layer.
///
/// A prototype is a ControlMap table and nothing else (arch-design §7.9): the
/// physical address (MIDI CC on the host, scan index on the target) maps to a
/// logical Control, and the profile declares what the surface physically has.
/// Swapping prototypes swaps a table. Encoders use one of three stateless
/// MIDI relative encodings, decoded by the pure DecodeEnc.
#pragma once

#include <cstdint>

#include "interaction.h"

namespace nostromo {

/// The three stateless MIDI relative-encoder encodings. Quadrature is NOT an
/// EncEncoding member: a 2-bit Gray-code transition needs previous-state
/// memory, so it gets its own stateful reader (the next panel's GPIO concern),
/// not a broken "pure" decode.
enum class EncEncoding : std::uint8_t {
  kSignedBit,      ///< bit 6 = sign, bits 0-5 = magnitude
  kTwosComplement, ///< 7-bit two's complement
  kBinaryOffset,   ///< center 0x40, offset wraps
};

/// Pure decode of one relative-encoder byte to −1 / 0 / +1 (tick direction,
/// magnitude clamped). A wraparound or aggregate byte (binary-offset raw = 0 →
/// −64, signed-bit raw = 0x7F → magnitude 63) decodes to ±1, never a 64-detent
/// delta, so one event cannot jump a parameter end-to-end.
int DecodeEnc(std::uint8_t raw, EncEncoding enc);

/// One physical→logical mapping entry.
struct ControlMap {
  std::uint16_t physical;   ///< MIDI CC (host) or scan index (target)
  Control       logical;
};

/// A named physical surface: its control map and what it physically has.
struct SurfaceProfile {
  const char       *name;        ///< "xtouch-compact", "proto-a", "noisetromo-v1"
  const ControlMap *map;
  std::uint8_t      n_map;
  std::uint8_t      n_encoders;  ///< parameter encoders physically present
  std::uint8_t      n_buttons;
  bool              has_rings;   ///< LED rings on the parameter encoders
  EncEncoding       enc;         ///< the encoders' relative encoding
};

/// The active profile. Set once at startup; swapping prototypes swaps a table.
const SurfaceProfile &Surface();
void SetSurface(const SurfaceProfile &p);

}  // namespace nostromo
