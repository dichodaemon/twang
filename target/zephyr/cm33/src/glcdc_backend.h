/// @file glcdc_backend.h
/// @brief GLCDC framebuffer + FT5336 touch presentation for spike.
///
/// The target-side display backend. It owns two SDRAM scan-out buffers (double
/// buffering): the Panel draws into the back buffer, Present() flips it to the
/// GLCDC via display_write, and the hardware scans it out while the panel draws
/// the next frame into the other buffer. FT5336 touch events (through the
/// Zephyr input subsystem) are forwarded to PanelPointer. This is the GLCDC
/// twin of the desktop SDL backend.
#pragma once

#include "fb.h"

struct device;  // Zephyr display device (forward declaration)

namespace nostromo {
struct Panel;
}

namespace spike {

/// @brief GLCDC double-buffered framebuffer backend (EK-RA8D2, 1024x600 RGB565).
struct GlcdcBackend {
  FrameBuffer fb{};  ///< Framebuffer over the current back buffer.

  /// @brief Resolves the display device and maps the two SDRAM framebuffers.
  ///
  /// @return True on success (device ready and framebuffer present).
  bool Init();

  /// @brief Forwards pending touch state as pointer events.
  ///
  /// @param panel Panel to feed pointer events.
  void PollTouch(nostromo::Panel *panel);

  /// @brief Flips the back buffer to the GLCDC scan-out (blocks on vsync).
  ///
  /// Displays the buffer the Panel just drew into, then points `fb` at the
  /// other buffer for the next frame (double buffering).
  void Present();

  const struct device *dev_ = nullptr;  ///< Resolved display device.
  int back_ = 0;                        ///< Which SDRAM buffer is the back buffer.
};

}  // namespace spike
