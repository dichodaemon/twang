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

// Provisional X-Touch Compact mapping (standard MIDI mode, channel 1). The
// encoder CCs (10-14) extend the existing kXtouchCompact layout (midi.cc maps
// CC 10-11 as relative); the button CCs are placeholders. The exact encoder
// encoding and the full button map are confirmed against the hardware in
// task 7.1.
constexpr ControlMap kXtouchMap[] = {
    {10, Enc(0)}, {11, Enc(1)}, {12, Enc(2)},
    {13, Enc(3)}, {14, Enc(4)},
    {15, Control::kNav1}, {16, Control::kNav2},
    {20, Control::kPart0}, {21, Control::kPart1},
    {22, Control::kPart2}, {23, Control::kPart3},
    {24, Control::kMod},   {25, Control::kPerf},
    {26, Control::kGroup}, {27, Control::kOut},
};

const SurfaceProfile kXtouchCompact = {
    "xtouch-compact",
    kXtouchMap,
    static_cast<std::uint8_t>(sizeof(kXtouchMap) / sizeof(kXtouchMap[0])),
    5,     // n_encoders (parameter encoders)
    8,     // n_buttons
    true,  // has_rings (LED rings on the parameter encoders)
    EncEncoding::kTwosComplement,  // provisional; confirmed in task 7.1
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
