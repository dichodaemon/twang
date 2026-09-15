// surface.cc — the physical control-surface profile and the relative-encoder
// decode.
//
// A prototype is a ControlMap table (arch-design §7.9): swapping prototypes
// swaps a table, not code. DecodeEnc is pure and stateless; quadrature is
// deliberately absent (it needs previous-state memory and is the GPIO panel's
// concern, not a per-byte decode).

#include "surface.h"

namespace nostromo {

const SurfaceProfile &Surface() { return kXtouchCompact; }

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
