// host/sdl_backend.cc — SDL2 presentation of the spike framebuffer.
//
// Creates the 1024x600 RGB565 window, uploads the Panel's framebuffer each
// frame through a streaming texture, and translates SDL mouse events into
// nostromo::PointerEvent for PanelPointer. This is the desktop twin of the
// target's GLCDC backend.

#include "sdl_backend.h"

#include <cstdint>
#include <cstdio>

#include <SDL2/SDL.h>

#include "panel.h"

namespace spike {

using nostromo::Panel;
using nostromo::PanelPointer;
using nostromo::PointerEvent;
using nostromo::PointerKind;

struct SdlBackend::Impl {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  // Two RGB565 framebuffers: the panel draws into px[current]; Present()
  // displays it and flips to the other buffer (double buffering).
  std::uint16_t *px[2] = {nullptr, nullptr};
  int current = 0;
  int w = 0;
  int h = 0;
};

bool SdlBackend::Init(int w, int h) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "sdl: SDL_Init: %s\n", SDL_GetError());
    return false;
  }
  impl = new Impl;
  impl->w = w;
  impl->h = h;

  impl->window = SDL_CreateWindow(
      "twang — spike", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
  if (!impl->window) {
    std::fprintf(stderr, "sdl: SDL_CreateWindow: %s\n", SDL_GetError());
    return false;
  }

  // Prefer a hardware-accelerated, vsync'd renderer; fall back to software
  // for headless/virtualized environments (dummy/offscreen drivers).
  impl->renderer = SDL_CreateRenderer(
      impl->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!impl->renderer)
    // Keep vsync on the fallback: without it the host loop free-runs and
    // anything paced by the frame rate is driven far faster than intended.
    impl->renderer = SDL_CreateRenderer(
        impl->window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
    if (!impl->renderer)
      impl->renderer = SDL_CreateRenderer(impl->window, -1, SDL_RENDERER_SOFTWARE);
  if (!impl->renderer) {
    std::fprintf(stderr, "sdl: SDL_CreateRenderer: %s\n", SDL_GetError());
    return false;
  }

  impl->texture = SDL_CreateTexture(
      impl->renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
  if (!impl->texture) {
    std::fprintf(stderr, "sdl: SDL_CreateTexture: %s\n", SDL_GetError());
    return false;
  }

  impl->px[0] = new std::uint16_t[w * h];
  impl->px[1] = new std::uint16_t[w * h];
  fb = FrameBuffer{impl->px[0], w, h, w, Rect{0, 0, w, h}};
  return true;
}

int SdlBackend::BackIndex() const { return impl ? impl->current : 0; }

void SdlBackend::Present() {
  // Display the buffer the panel just drew into, then flip fb to the other
  // buffer for the next frame (double buffering).
  const std::uint16_t *front = impl->px[impl->current];
  SDL_UpdateTexture(impl->texture, nullptr, front, impl->w * 2);
  SDL_RenderClear(impl->renderer);
  SDL_RenderCopy(impl->renderer, impl->texture, nullptr, nullptr);
  SDL_RenderPresent(impl->renderer);
  impl->current ^= 1;
  fb.px = impl->px[impl->current];
}

void SdlBackend::PollEvents(Panel *panel) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
    case SDL_QUIT:
      quit = true;
      break;
    case SDL_KEYDOWN:
      if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (e.button.button == SDL_BUTTON_LEFT)
        PanelPointer(panel,
                     PointerEvent{PointerKind::kPress, e.button.x, e.button.y});
      break;
    case SDL_MOUSEMOTION:
      if (e.motion.state & SDL_BUTTON_LMASK)
        PanelPointer(panel,
                     PointerEvent{PointerKind::kMove, e.motion.x, e.motion.y});
      break;
    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT)
        PanelPointer(panel,
                     PointerEvent{PointerKind::kRelease, e.button.x, e.button.y});
      break;
    default:
      break;
    }
  }
}

SdlBackend::~SdlBackend() {
  if (!impl) return;
  if (impl->texture) SDL_DestroyTexture(impl->texture);
  if (impl->renderer) SDL_DestroyRenderer(impl->renderer);
  if (impl->window) SDL_DestroyWindow(impl->window);
  delete[] impl->px[0];
  delete[] impl->px[1];
  delete impl;
  impl = nullptr;
  SDL_Quit();
}

}  // namespace spike
