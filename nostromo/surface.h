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

/// One physical→logical mapping entry. `turn` distinguishes a relative
/// encoder byte (decoded by DecodeEnc to detents) from a button (press/release
/// edge). An encoder has two entries — the rotary CC (turn) and its push
/// button CC (+100, a button) — so the same logical control appears twice.
/// `enc` is the control's own encoding, declarable per-control: a mixed
/// surface (quadrature GPIO on some controls, two's-complement on others) can
/// state it here rather than assuming one encoding for the whole surface.
/// Meaningful only when `turn` is true.
struct ControlMap {
  std::uint16_t physical;   ///< MIDI CC (host) or scan index (target)
  Control       logical;
  bool          turn;       ///< true = relative encoder; false = button
  EncEncoding   enc = EncEncoding::kTwosComplement;
};

/// A named physical surface: its control map and what it physically has.
struct SurfaceProfile {
  const char       *name;        ///< "xtouch-compact", "proto-a", "noisetromo-v1"
  const ControlMap *map;
  std::uint8_t      n_map;
  std::uint8_t      n_encoders;  ///< parameter encoders physically present
  std::uint8_t      n_buttons;
  bool              has_rings;   ///< LED rings on the parameter encoders
};

/// The shipped X-Touch Compact control map (the only prototype). Header-
/// declared so the table stays visible/configurable — swapping prototypes
/// swaps this table, not code. Encoders (10-14) and nav rotaries (18, 20) send
/// relative two's-complement bytes; each encoder also has a push button at +100
/// (110-114). Buttons are momentary (value 127 = down, 0 = up).
inline constexpr ControlMap kXtouchMap[] = {
    {10, Enc(0), true},  {11, Enc(1), true},  {12, Enc(2), true},
    {13, Enc(3), true},  {14, Enc(4), true},
    {18, Control::kNav1, true}, {20, Control::kNav2, true},
    {110, Enc(0), false}, {111, Enc(1), false}, {112, Enc(2), false},
    {113, Enc(3), false}, {114, Enc(4), false},
    {83, Control::kPart0, false}, {84, Control::kPart1, false},
    {85, Control::kPart2, false}, {86, Control::kPart3, false},
    {50, Control::kMod, false},   {51, Control::kPerf, false},
    {52, Control::kGroup, false}, {53, Control::kOut, false},
};

/// The shipped surface profile (the only prototype). Swapping prototypes
/// swaps the table returned here, not a runtime pointer.
inline const SurfaceProfile kXtouchCompact = {
    "xtouch-compact",
    kXtouchMap,
    static_cast<std::uint8_t>(sizeof(kXtouchMap) / sizeof(kXtouchMap[0])),
    5,     // n_encoders (parameter encoders)
    13,    // n_buttons (4 parts + 4 mode + 5 encoder pushes)
    true,  // has_rings (LED rings on the parameter encoders)
};

const SurfaceProfile &Surface();

}  // namespace nostromo
