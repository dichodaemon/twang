/// @file glcdc_backend.h
/// @brief GLCDC framebuffer + FT5336 touch presentation for spike.
///
/// The target-side display backend: maps the GLCDC's SDRAM scan-out buffer
/// into a spike::FrameBuffer the Panel draws into, and feeds FT5336 touch
/// events (through the Zephyr input subsystem) to PanelPointer. The hardware
/// scans the buffer out continuously, so there is no explicit present step —
/// this is the GLCDC twin of the desktop SDL backend.
#pragma once

#include "fb.h"

namespace spike {

struct Panel;

/// @brief GLCDC direct-framebuffer backend (EK-RA8D2, 1024x600 RGB565).
struct GlcdcBackend {
  FrameBuffer fb{};  ///< Framebuffer over the GLCDC scan-out buffer.

  /// @brief Resolves the display device and maps its SDRAM framebuffer.
  ///
  /// @return True on success (device ready and framebuffer present).
  bool Init();

  /// @brief Forwards pending touch state as pointer events.
  ///
  /// @param panel Panel to feed pointer events.
  void PollTouch(Panel *panel);
};

}  // namespace spike
