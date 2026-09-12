/// @file png.h
/// @brief Minimal PNG writer (stored deflate; no zlib).
///
/// Serialization, not rendering: dumps pixels to a portable image format so a
/// framebuffer can be inspected without a display. General and reusable — the
/// encoder takes raw RGB bytes and knows nothing about this project.
///
/// Host-side convenience used by the screenshot/mockup tools; never called on
/// the target (it is stripped out of that image by the linker when unused).

#pragma once

#include <cstdint>
#include <vector>

#include "fb.h"

namespace spike {

/// @brief Write an RGB888 buffer as a PNG (truecolour, stored deflate).
///
/// @param path Output file.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @param raw h rows of (1 filter byte + w*3 RGB bytes).
/// @return true on success.
bool WritePng(const char *path, int w, int h,
              const std::vector<std::uint8_t> &raw);

/// @brief Write a framebuffer as a PNG, expanding RGB565 to RGB888.
///
/// @param fb Framebuffer to serialize.
/// @param path Output file.
/// @param scale Integer pixel scale (1 = native size).
/// @return true on success.
bool WriteFramePng(const FrameBuffer &fb, const char *path, int scale = 1);

}  // namespace spike
