// glcdc_backend.cc — GLCDC framebuffer + FT5336 touch presentation.
//
// Maps the GLCDC's SDRAM scan-out buffer (ext-ram) into a spike::FrameBuffer;
// the Panel draws into it and the hardware scans it out continuously. Touch
// (FT5336 on iic1) arrives through the Zephyr input subsystem and is forwarded
// to PanelPointer as press/move/release transitions.

#include "glcdc_backend.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>

#include "panel.h"

namespace spike {

namespace {

// 1024x600 RGB565 scan-out buffer (ext-ram / SDRAM), stride 1024 pixels.
constexpr int kFrameW = 1024;
constexpr int kFrameH = 600;

// Latest touch state: written by the input callback (input thread) and read
// by the main loop. `dirty` marks a transition still to be forwarded. The
// FT5336 driver reports BTN_TOUCH (press/release) and ABS_X/ABS_Y in panel
// coordinates (swap/rotation applied by the driver's touch config).
struct TouchState {
  bool dirty = false;
  bool pressed = false;
  int x = 0;
  int y = 0;
};

void TouchCallback(struct input_event *evt, void *user_data) {
  auto *ts = static_cast<TouchState *>(user_data);
  switch (evt->type) {
  case INPUT_EV_KEY:
    if (evt->code == INPUT_BTN_TOUCH) {
      ts->pressed = evt->value != 0;
      ts->dirty = true;
    }
    break;
  case INPUT_EV_ABS:
    if (evt->code == INPUT_ABS_X) {
      ts->x = evt->value;
    } else if (evt->code == INPUT_ABS_Y) {
      ts->y = evt->value;
    }
    ts->dirty = true;
    break;
  default:
    break;
  }
}

TouchState g_touch;
// Single input device on the target: bind to the chosen touch panel.
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), TouchCallback,
                      &g_touch);

}  // namespace

bool GlcdcBackend::Init() {
  const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(dev)) {
    return false;
  }
  void *px = display_get_framebuffer(dev);
  if (!px) {
    return false;
  }
  fb = FrameBuffer{static_cast<std::uint16_t *>(px), kFrameW, kFrameH, kFrameW,
                   Rect{0, 0, kFrameW, kFrameH}};
  return true;
}

void GlcdcBackend::PollTouch(Panel *panel) {
  if (!g_touch.dirty) {
    return;
  }
  g_touch.dirty = false;
  const PointerKind kind =
      g_touch.pressed ? PointerKind::kPress : PointerKind::kRelease;
  PanelPointer(panel, PointerEvent{kind, g_touch.x, g_touch.y});
}

}  // namespace spike
