/// @file descriptor.h
/// @brief Static-chrome descriptor interpreter (engine-recommendations.md §5).
///
/// A screen's static chrome is a tagged variable-length byte stream (§5.2):
/// RECT / HLINE / VLINE / TEXT / CALL / DYN / END. The interpreter walks the
/// stream and emits the same primitive calls a hand-written draw function
/// would; it knows nothing about the Nostromo vocabulary — the palette, fonts,
/// templates and DYN slots are all supplied by the caller.
///
/// The governing split (§5.1): descriptors say *where* (static layout as
/// data); C says *what* (the DYN slots are C function pointers). Nothing here
/// binds a rect to a parameter — that path is explicitly rejected in §5.4.
#pragma once

#include <cstdint>

#include "fb.h"
#include "font.h"

namespace spike {

/// Descriptor opcodes (§5.2).
enum DescriptorOp : std::uint8_t {
  kOpEnd = 0x00,    ///< Terminate the stream (1 B).
  kOpRect = 0x01,   ///< Filled rect: x:u16 y:u16 w:u16 h:u16 c:u8 (10 B).
  kOpHLine = 0x02,  ///< Horizontal rule: x:u16 y:u16 w:u16 c:u8 (8 B).
  kOpVLine = 0x03,  ///< Vertical rule: x:u16 y:u16 h:u16 c:u8 (8 B).
  kOpText = 0x04,   ///< Glyph run: x:u16 y:u16 font:u8 c:u8 len:u8 bytes (8+len B).
  kOpCall = 0x05,   ///< Template call: tmpl:u16 dx:u16 dy:u16 (7 B).
  kOpDyn = 0x06,    ///< Reserve a slot: slot:u8 x:u16 y:u16 w:u16 h:u16 (10 B).
};

/// A DYN slot (§5.4). The descriptor supplies the rect; C supplies the hook.
struct DynSlot {
  Rect rect;                                            ///< Bounds (from DYN).
  void (*draw)(FrameBuffer &fb, const Rect &r, void *state);  ///< Draw hook.
  void *state;                                          ///< Opaque state.
  bool dirty;                                           ///< Needs a redraw.
};

/// The named tables the interpreter needs to resolve indices.
struct DescriptorCtx {
  const Color *palette;               ///< Palette (index 0..n_palette-1).
  int n_palette;                      ///< Palette entry count.
  const Font *fonts;                  ///< >= 2 fonts (0 = primary, 1 = secondary).
  const std::uint8_t *const *templates;  ///< CALL template table (may be null).
  DynSlot *slots;                     ///< DYN slot array (may be null if unused).
  int n_slots;                        ///< DYN slot capacity.
};

/// Interprets a descriptor into `fb`, stopping at END. A DYN op writes the
/// slot's rect and does nothing else (the slot is drawn later, only if dirty).
void Interpret(const std::uint8_t *bytes, FrameBuffer &fb,
               const DescriptorCtx &ctx);

}  // namespace spike
