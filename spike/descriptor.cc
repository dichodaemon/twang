// spike/descriptor.cc — static-chrome descriptor interpreter (§5.2).
//
// Walks a tagged byte stream and emits primitive calls in stream order. The
// only stateful behaviour is CALL (which re-enters a template at an origin
// offset) and DYN (which records a slot rect). Everything else is a 1:1 map
// onto FillRect / DrawHLine / DrawVLine / DrawGlyphRun.
//
// Coordinates are u16 (the panel is 1024x600). Operands are read
// little-endian, explicitly, so the stream is portable between the x86 host
// and the little-endian ARM target without a serialised struct.

#include "descriptor.h"

namespace spike {
namespace {

std::uint16_t Read16(const std::uint8_t *p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

// Walks `p` until END, translating every coordinate by (dx, dy). `dx`/`dy` are
// non-zero only inside a CALL — a template is authored in a local frame and
// placed by its caller. Nested CALLs accumulate the offset.
void Walk(const std::uint8_t *p, FrameBuffer &fb, const DescriptorCtx &ctx,
          int dx, int dy) {
  for (;;) {
    const std::uint8_t op = *p++;
    switch (op) {
      case kOpEnd:
        return;

      case kOpRect: {
        const int x = Read16(p);
        const int y = Read16(p + 2);
        const int w = Read16(p + 4);
        const int h = Read16(p + 6);
        const std::uint8_t c = p[8];
        p += 9;
        FillRect(fb, x + dx, y + dy, w, h, ctx.palette[c]);
        break;
      }

      case kOpHLine: {
        const int x = Read16(p);
        const int y = Read16(p + 2);
        const int w = Read16(p + 4);
        const std::uint8_t c = p[6];
        p += 7;
        DrawHLine(fb, x + dx, y + dy, w, ctx.palette[c]);
        break;
      }

      case kOpVLine: {
        const int x = Read16(p);
        const int y = Read16(p + 2);
        const int h = Read16(p + 4);
        const std::uint8_t c = p[6];
        p += 7;
        DrawVLine(fb, x + dx, y + dy, h, ctx.palette[c]);
        break;
      }

      case kOpText: {
        const int x = Read16(p);
        const int y = Read16(p + 2);
        const std::uint8_t font = p[4];
        const std::uint8_t c = p[5];
        const std::uint8_t len = p[6];
        p += 7;
        DrawGlyphRun(fb, x + dx, y + dy, reinterpret_cast<const char *>(p),
                     len, ctx.fonts[font], ctx.palette[c], 0);
        p += len;
        break;
      }

      case kOpCall: {
        const int tmpl = Read16(p);
        const int tdx = Read16(p + 2);
        const int tdy = Read16(p + 4);
        p += 5;
        Walk(ctx.templates[tmpl], fb, ctx, dx + tdx, dy + tdy);
        break;
      }

      case kOpDyn: {
        const std::uint8_t slot = p[0];
        const int x = Read16(p + 1);
        const int y = Read16(p + 3);
        const int w = Read16(p + 5);
        const int h = Read16(p + 7);
        p += 9;
        if (slot < ctx.n_slots && ctx.slots != nullptr)
          ctx.slots[slot].rect = Rect{x + dx, y + dy, w, h};
        break;
      }

      default:
        // Unknown opcode: treat as END — do not run off the stream.
        return;
    }
  }
}

}  // namespace

void Interpret(const std::uint8_t *bytes, FrameBuffer &fb,
               const DescriptorCtx &ctx) {
  Walk(bytes, fb, ctx, 0, 0);
}

}  // namespace spike
