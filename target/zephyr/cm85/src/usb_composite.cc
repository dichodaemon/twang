// USB composite device: UAC2 capture (source-only instrument) + MIDI 2.0.
//
// Register order is load-bearing: UAC2 must own interface 0 (see the brief's
// class-order note) or snd-usb-audio exposes no PCM device. The UAC2 audio
// stream is source-only: the render loop pushes int16-stereo blocks into a
// FIFO, and the SOF callback drains ~6 frames per HS microframe into
// usbd_uac2_send() — an asynchronous IN stream with no feedback endpoint (the
// host adapts to the device rate). See the brief §7.

#include "usb_composite.h"

#include <zephyr/usb/usbd.h>
// usbd_uac2.h lacks extern "C" guards (its declarations get C++ linkage when
// included from C++ and no longer resolve against the C definitions in
// usbd_uac2.c). Wrap it so the unmangled C symbols resolve.
extern "C" {
#include <zephyr/usb/class/usbd_uac2.h>
}
#include <zephyr/usb/class/usbd_midi2.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <atomic>
#include <cstdint>

#include "midi_ring.h"
#include "loss_counters.h"

LOG_MODULE_REGISTER(usb_composite, LOG_LEVEL_INF);

namespace {

// Terminal IDs are auto-assigned from the uac2_synth children order: aclk=1,
// engine_input=2, usb_out=3 (see app.overlay).
constexpr uint8_t kUsbOutTerminalId = 3;

// HS async IN: 6 stereo frames per 125 us microframe (nominal 48 kHz).
constexpr int kChannels = 2;
constexpr int kNominalFrames = 6;
constexpr int kMaxSendFrames = 8;   // allow 5..7 for drift
constexpr int kSendBytes = kMaxSendFrames * kChannels * sizeof(int16_t);  // 32

// Render -> USB FIFO of int16 stereo frames. Producer = render loop (main
// thread); consumer = SOF callback. Lock-free SPSC ring (monotonic indices).
constexpr int kFifoFrames = 512;  // power of two, ~10.7 ms at 48 kHz
constexpr int kFifoMask = kFifoFrames - 1;

struct Fifo {
    std::atomic<uint32_t> write{0};
    std::atomic<uint32_t> read{0};
    int16_t data[kFifoFrames * kChannels];
};

Fifo audio_fifo;

int FifoLevel()
{
    return static_cast<int>(audio_fifo.write.load(std::memory_order_relaxed) -
                            audio_fifo.read.load(std::memory_order_relaxed));
}

void FifoPush(const int16_t *stereo, int frames)
{
    const uint32_t w = audio_fifo.write.load(std::memory_order_relaxed);
    const uint32_t r = audio_fifo.read.load(std::memory_order_acquire);
    const uint32_t free = kFifoFrames - (w - r);
    if (static_cast<uint32_t>(frames) > free) {
        reinterpret_cast<LossCounters *>(kLossCountersAddr)
            ->fifo_overflow_frames.fetch_add(
                static_cast<uint32_t>(frames) - free,
                std::memory_order_relaxed);
        frames = static_cast<int>(free);  // drop on overflow (render faster)
    }
    for (int i = 0; i < frames; i++) {
        const uint32_t idx = (w + i) & kFifoMask;
        audio_fifo.data[idx * kChannels] = stereo[i * kChannels];
        audio_fifo.data[idx * kChannels + 1] = stereo[i * kChannels + 1];
    }
    audio_fifo.write.store(w + frames, std::memory_order_release);
}

int FifoPop(int16_t *stereo, int max_frames)
{
    const uint32_t r = audio_fifo.read.load(std::memory_order_relaxed);
    const uint32_t w = audio_fifo.write.load(std::memory_order_acquire);
    const uint32_t avail = w - r;
    const int n = (avail < static_cast<uint32_t>(max_frames))
                      ? static_cast<int>(avail)
                      : max_frames;
    for (int i = 0; i < n; i++) {
        const uint32_t idx = (r + i) & kFifoMask;
        stereo[i * kChannels] = audio_fifo.data[idx * kChannels];
        stereo[i * kChannels + 1] = audio_fifo.data[idx * kChannels + 1];
    }
    audio_fifo.read.store(r + n, std::memory_order_release);
    return n;
}

// Send buffers: usbd_uac2_send() references (does not copy) the buffer until
// buf_release_cb fires, so each send needs its own UDC-aligned persistent
// block. The slab is ISR-safe (K_NO_WAIT alloc / free) and the alignment keeps
// the buffers DMA/cache safe.
K_MEM_SLAB_DEFINE(send_slab, kSendBytes, 4, 32);

int SamplesToSend()
{
    // Drift compensation: steer the FIFO toward ~32 frames (~0.7 ms). The
    // render timer (~48012 Hz) runs slightly fast vs the host's 48 kHz, so
    // the FIFO otherwise grows without bound.
    const int level = FifoLevel();
    if (level > 64) return 7;
    if (level < 8)  return 5;
    return kNominalFrames;
}

// The async-IN send path is driven by buf_release_cb — it fires once per
// packet the host reads — NOT by SOF (the FSP SOF is a one-shot resume
// detector, armed only while suspended, so USBD_EVENT_SOF never arrives in
// normal operation; see the brief §7 note) and NOT by a free-running timer
// (which would drift against the host's IN tokens). Each completed packet
// enqueues the next, so the send rate tracks the host's consumption exactly.
//
// Priming is deferred to a work item: the driver calls terminal_update_cb
// BEFORE it sets as_active (usbd_uac2.c), so sending synchronously here would
// hit usbd_uac2_send()'s "not active" path, which calls buf_release_cb
// synchronously — an infinite recursion. The work runs after the SET_INTERFACE
// handler finishes (as_active set).
std::atomic<bool> terminal_enabled{false};  // set/cleared in terminal_update_cb
const struct device *g_uac2_dev = NULL;
constexpr int kTargetInFlight = 2;  // packets kept in flight (== slab depth)
struct k_work prime_work;  // prime on enable
struct k_work send_work;   // top-up on completion

// Dedicated send work queue: serializes SendPacket (PrimeHandler + SendHandler)
// off the shared system workqueue, which also runs logging and the USB stack.
K_THREAD_STACK_DEFINE(audio_queue_stack, 1024);
struct k_work_q audio_queue;

// @return true if a packet was sent (its slab block is now in flight);
//         false on any failure (no block available, or send rejected).
bool SendPacket(const struct device *dev)
{
    if (!terminal_enabled.load(std::memory_order_relaxed)) {
        return false;  // host disabled the stream
    }

    void *buf;
    if (k_mem_slab_alloc(&send_slab, &buf, K_NO_WAIT) != 0) {
        return false;  // all send buffers in flight; the next buf_release_cb drives it
    }

    int16_t *p = static_cast<int16_t *>(buf);
    const int n = SamplesToSend();
    const int got = FifoPop(p, n);
    for (int i = got; i < n; i++) {  // underrun: pad with silence
        p[i * kChannels] = 0;
        p[i * kChannels + 1] = 0;
    }

    const int rc = usbd_uac2_send(dev, kUsbOutTerminalId, buf,
                                  n * kChannels * sizeof(int16_t));
    if (rc != 0) {
        k_mem_slab_free(&send_slab, buf);  // send rejected; return the buffer
        return false;
    }
    return true;
}

// Top up in-flight sends to kTargetInFlight. Self-healing: runs after any
// release (buf_release_cb) or re-enable (terminal_update_cb) and sends until
// the slab used-count — the in-flight count by construction — reaches target.
// The single audio_queue serializes SendPacket, so no re-entrancy guard.
void TopUpSends()
{
    while (g_uac2_dev && terminal_enabled.load(std::memory_order_relaxed) &&
           k_mem_slab_num_used_get(&send_slab) < kTargetInFlight) {
        if (!SendPacket(g_uac2_dev)) {
            break;
        }
    }
}

void PrimeHandler(struct k_work *work)
{
    ARG_UNUSED(work);
    TopUpSends();  // prime the stream on enable
}

void SendHandler(struct k_work *work)
{
    ARG_UNUSED(work);
    TopUpSends();  // top-up on completion
}

void Uac2SofCb(const struct device *dev, void *user_data)
{
    // FSP SOF never fires in normal operation (one-shot resume detector).
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
}

void Uac2TerminalCb(const struct device *dev, uint8_t terminal, bool enabled,
                    bool microframes, void *user_data)
{
    ARG_UNUSED(terminal);
    ARG_UNUSED(microframes);
    ARG_UNUSED(user_data);

    terminal_enabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        // Deferred: as_active is set only after this callback returns.
        k_work_submit_to_queue(&audio_queue, &prime_work);
    }
}

void Uac2BufReleaseCb(const struct device *dev, uint8_t terminal, void *buf,
                      void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(terminal);
    ARG_UNUSED(user_data);

    k_mem_slab_free(&send_slab, buf);  // the completed packet's buffer
    // Deferred: uac2_request frees the net_buf back to uac2_pool only AFTER
    // this callback returns, so re-enqueueing here would hit -ENOMEM (the pool
    // is 2 buffers). The work runs after the free, so the alloc succeeds.
    k_work_submit_to_queue(&audio_queue, &send_work);
}

const struct uac2_ops kUac2Ops = {
    .sof_cb = Uac2SofCb,
    .terminal_update_cb = Uac2TerminalCb,
    .buf_release_cb = Uac2BufReleaseCb,
};

// MIDI 2.0 rx: forward each packet's first UMP word to the control core over
// the shared MIDI ring. Single-word UMP (MIDI 1.0 channel voice) is the
// X-Touch's only traffic; the cm33 downconverts to MIDI 1.0 and feeds the
// interaction layer (surface map).
void MidiRxCb(const struct device *dev, const struct midi_ump ump)
{
    ARG_UNUSED(dev);
    MidiRing *ring = reinterpret_cast<MidiRing *>(kMidiRingAddr);
    if (!ring->Push(ump.data[0])) {
        reinterpret_cast<LossCounters *>(kLossCountersAddr)
            ->midi_ring_full.fetch_add(1, std::memory_order_relaxed);
    }
}

const struct usbd_midi_ops kMidiOps = {
    .rx_packet_cb = MidiRxCb,
};

// The composite device context. VID/PID are the Zephyr sample values (the
// prototype has no purchased vendor ID).
USBD_DEVICE_DEFINE(twang_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   0x2fe3, 0x000f);

USBD_DESC_LANG_DEFINE(twang_lang);
USBD_DESC_MANUFACTURER_DEFINE(twang_mfr, "twang");
USBD_DESC_PRODUCT_DEFINE(twang_product, "twang");

USBD_DESC_CONFIG_DEFINE(twang_fs_cfg, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(twang_hs_cfg, "HS Configuration");

USBD_CONFIGURATION_DEFINE(twang_fs_config, USB_SCD_SELF_POWERED, 250,
                          &twang_fs_cfg);
USBD_CONFIGURATION_DEFINE(twang_hs_config, USB_SCD_SELF_POWERED, 250,
                          &twang_hs_cfg);

/// Register both classes for one speed, UAC2 first (load-bearing order).
int RegisterClasses(struct usbd_context *ctx, enum usbd_speed speed)
{
    int err;

    err = usbd_register_class(ctx, "uac2_0", speed, 1);
    if (err) {
        LOG_ERR("register uac2_0 (%d): %d", speed, err);
        return err;
    }
    err = usbd_register_class(ctx, "midi_0", speed, 1);
    if (err) {
        LOG_ERR("register midi_0 (%d): %d", speed, err);
        return err;
    }
    return 0;
}

}  // namespace

namespace usb {

void AudioPush(const int16_t *stereo, int frames)
{
    FifoPush(stereo, frames);
}

int Init()
{
    const struct device *uac2 = DEVICE_DT_GET(DT_NODELABEL(uac2_synth));
    const struct device *midi = DEVICE_DT_GET(DT_NODELABEL(usb_midi));
    const struct device *udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
    int err;

    if (!device_is_ready(udc)) {
        LOG_ERR("UDC not ready");
        return -ENODEV;
    }
    if (!device_is_ready(uac2)) {
        LOG_ERR("uac2 device not ready");
        return -ENODEV;
    }
    if (!device_is_ready(midi)) {
        LOG_ERR("midi device not ready");
        return -ENODEV;
    }

    // Mandatory: uac2_init() fails with -EINVAL if ops are unset.
    usbd_uac2_set_ops(uac2, &kUac2Ops, NULL);
    g_uac2_dev = uac2;
    k_work_init(&prime_work, PrimeHandler);
    k_work_init(&send_work, SendHandler);
    k_work_queue_start(&audio_queue, audio_queue_stack,
                       K_THREAD_STACK_SIZEOF(audio_queue_stack), 5, NULL);

    // MIDI 2.0 rx -> control core over the shared ring. Reset before any
    // traffic (the SDRAM backing is uninitialized).
    reinterpret_cast<MidiRing *>(kMidiRingAddr)->Reset();
    reinterpret_cast<LossCounters *>(kLossCountersAddr)->Reset();
    usbd_midi_set_ops(midi, &kMidiOps);

    err = usbd_add_descriptor(&twang_usbd, &twang_lang);
    if (err) {
        LOG_ERR("add lang descriptor: %d", err);
        return err;
    }
    err = usbd_add_descriptor(&twang_usbd, &twang_mfr);
    if (err) {
        LOG_ERR("add manufacturer descriptor: %d", err);
        return err;
    }
    err = usbd_add_descriptor(&twang_usbd, &twang_product);
    if (err) {
        LOG_ERR("add product descriptor: %d", err);
        return err;
    }

    err = usbd_add_configuration(&twang_usbd, USBD_SPEED_HS,
                                 &twang_hs_config);
    if (err) {
        LOG_ERR("add HS configuration: %d", err);
        return err;
    }
    err = RegisterClasses(&twang_usbd, USBD_SPEED_HS);
    if (err) {
        return err;
    }

    err = usbd_add_configuration(&twang_usbd, USBD_SPEED_FS,
                                 &twang_fs_config);
    if (err) {
        LOG_ERR("add FS configuration: %d", err);
        return err;
    }
    err = RegisterClasses(&twang_usbd, USBD_SPEED_FS);
    if (err) {
        return err;
    }

    // Multi-interface composite: the device code triple must advertise the
    // interface-association descriptor (misc class, common subclass, IAD).
    usbd_device_set_code_triple(&twang_usbd, USBD_SPEED_HS,
                                USB_BCC_MISCELLANEOUS, 0x02, 0x01);
    usbd_device_set_code_triple(&twang_usbd, USBD_SPEED_FS,
                                USB_BCC_MISCELLANEOUS, 0x02, 0x01);
    usbd_self_powered(&twang_usbd, true);

    err = usbd_init(&twang_usbd);
    if (err) {
        LOG_ERR("usbd_init: %d", err);
        return err;
    }

    err = usbd_enable(&twang_usbd);
    if (err) {
        LOG_ERR("usbd_enable: %d", err);
        return err;
    }

    LOG_INF("composite USB device enabled (UAC2 capture + MIDI 2.0)");
    return 0;
}

}  // namespace usb
