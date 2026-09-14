// surface.cc — the physical control-surface profile and the relative-encoder
// decode.
//
// A prototype is a ControlMap table (arch-design §7.9): swapping prototypes
// swaps a table, not code. DecodeEnc is pure and stateless; quadrature is
// deliberately absent (it needs previous-state memory and is the GPIO panel's
// concern, not a per-byte decode).

#include "surface.h"

namespace nostromo {

namespace {

// X-Touch Compact mapping (standard MIDI mode, channel 1), confirmed against
// the hardware. Encoders (10-14) and nav rotaries (18, 20) send relative
// two's-complement bytes; each encoder also has a push button at +100
// (110-114). Buttons are momentary (value 127 = down, 0 = up).
constexpr ControlMap kXtouchMap[] = {
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

const SurfaceProfile kXtouchCompact = {
    "xtouch-compact",
    kXtouchMap,
    static_cast<std::uint8_t>(sizeof(kXtouchMap) / sizeof(kXtouchMap[0])),
    5,     // n_encoders (parameter encoders)
    13,    // n_buttons (4 parts + 4 mode + 5 encoder pushes)
    true,  // has_rings (LED rings on the parameter encoders)
};

const SurfaceProfile *g_surface = &kXtouchCompact;

}  // namespace

const SurfaceProfile &Surface() { return *g_surface; }

void SetSurface(const SurfaceProfile &p) { g_surface = &p; }

int DecodeEnc(std::uint8_t raw, EncEncoding enc) {
  switch (enc) {
    case EncEncoding::kSignedBit: {
      // bit 6 = sign, bits 0-5 = magnitude; clamp to the direction.
      if ((raw & 0x3F) == 0) return 0;
      return (raw & 0x40) ? -1 : +1;
    }
    case EncEncoding::kTwosComplement: {
      // 7-bit two's complement: 0x00-0x3F positive, 0x40-0x7F negative.
      // Clamp to the direction (0x40 = -64 reads as one detent).
      if (raw == 0) return 0;
      return (raw & 0x40) ? -1 : +1;
    }
    case EncEncoding::kBinaryOffset: {
      // center 0x40, offset wraps; clamp to the direction.
      const std::int8_t d = static_cast<std::int8_t>(raw - 0x40);
      return (d > 0) ? 1 : (d < 0) ? -1 : 0;
    }
  }
  return 0;
}

}  // namespace nostromo
