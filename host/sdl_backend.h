/// @file sdl_backend.h
/// @brief Desktop SDL2 window + framebuffer presentation for spike.
///
/// The sim-side display backend: owns the RGB565 framebuffer the Panel draws
/// into, presents it through an SDL streaming texture, and maps SDL mouse
/// events to nostromo::PointerEvent. The target swaps this for the GLCDC
/// backend (target/zephyr/cm33/src/glcdc_backend.cc).
#pragma once

#include "fb.h"

namespace nostromo {
struct Panel;
}

namespace spike {

/// @brief SDL2 presentation of an RGB565 framebuffer.
///
/// SDL handles (window/renderer/texture) live behind `impl` so this header
/// stays free of SDL types; the pixel buffer is owned by the backend and
/// exposed as `fb`.
struct SdlBackend {
  FrameBuffer fb{};   ///< Framebuffer the Panel draws into (the back buffer; Present() flips it).
  bool quit = false;  ///< SDL_QUIT or ESC seen.

  /// @brief Opens the window and allocates the framebuffer + texture.
  ///
  /// @param w Framebuffer width.
  /// @param h Framebuffer height.
  /// @return True on success; false on any SDL failure.
  bool Init(int w, int h);

  /// @brief Uploads the framebuffer to the texture and presents it.
  void Present();

  /// @brief Index of the back buffer `fb` currently points at (0 or 1).
  int BackIndex() const;

  /// @brief Drains SDL events: quit handling + mouse → PanelPointer.
  ///
  /// @param panel Panel to feed pointer events.
  void PollEvents(nostromo::Panel *panel);

  /// @brief Tears down SDL and frees the framebuffer.
  ~SdlBackend();

  struct Impl;
  Impl *impl = nullptr;  ///< SDL handles + pixel storage.
};

}  // namespace spike
