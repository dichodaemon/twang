// glcdc_backend.cc — GLCDC framebuffer + FT5336 touch presentation.
//
// Owns two SDRAM scan-out buffers (double buffering). The Panel draws into the
// back buffer; Present() flips it to the GLCDC via display_write, which
// performs a buffer change and blocks until the next line-detect (vsync) —
// the hardware scans out the front buffer while the panel draws the next frame
// into the other. Touch (FT5336 on iic1) arrives through the Zephyr input
// subsystem and is forwarded to PanelPointer as press/move/release.

#include "glcdc_backend.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>

#include "panel.h"

namespace spike {

namespace {

// 1024x600 RGB565 scan-out buffers, stride 1024 pixels. Each is 1024*600*2 =
// 1,228,800 bytes (~1.17 MiB) — far too large for the M33's 640 KB SRAM, so
// both live in SDRAM (0x68000000..0x6c000000). The driver's own ext-ram frame
// buffers occupy the start of SDRAM; the IPC block is at 0x68400000 and the
// Panel at 0x68500000 (see spike/panel.cc), so the two buffers land at +6 MiB
// and +8 MiB — clear of everything.
constexpr int kFrameW = 1024;
constexpr int kFrameH = 600;
constexpr std::uintptr_t kFbAddr[2] = {0x68600000UL, 0x68800000UL};

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
  dev_ = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(dev_)) {
    return false;
  }
  back_ = 0;
  fb = FrameBuffer{reinterpret_cast<std::uint16_t *>(kFbAddr[back_]), kFrameW,
                   kFrameH, kFrameW, Rect{0, 0, kFrameW, kFrameH}};
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

void GlcdcBackend::Present() {
  // No cache maintenance is required before the flip: the cm33 image runs on
  // the Cortex-M33 control core, which has no data cache (Zephyr selects
  // CPU_HAS_DCACHE only for M52/M55/M85), so CPU writes to the SDRAM
  // framebuffers are already visible to the GLCDC DMA. If the panel ever moves
  // to a cached core, add sys_cache_data_flush_range(fb.px, w*h*2) here.

  // Flip: display the buffer the Panel just drew into, then point `fb` at the
  // other buffer for the next frame. display_write (display_renesas_ra.c, with
  // CONFIG_RENESAS_RA_GLCDC_FB_NUM=0) takes the full-frame path: it sets the
  // pending buffer to our pointer and calls R_GLCDC_BufferChange — a hardware
  // flip, NOT a memcpy — then blocks on the line-detect semaphore (vsync).
  // The memcpy path only exists for partial writes, which FB_NUM=0 rejects
  // with -ENOTSUP (the app must always write full frames).
  const struct display_buffer_descriptor desc = {
      .buf_size = static_cast<std::uint32_t>(kFrameW * kFrameH * 2),
      .width = kFrameW,
      .height = kFrameH,
      .pitch = kFrameW,
      .frame_incomplete = false,
  };
  display_write(dev_, 0, 0, &desc, fb.px);
  back_ ^= 1;
  fb.px = reinterpret_cast<std::uint16_t *>(kFbAddr[back_]);
}

}  // namespace spike
