// twang cm85 — engine bring-up with I2S audio output.
//
// Renders the engine to the EK-RA8D2's on-board codec through SSIE1 (i2s1).
// A GPT PWM (pwm2) synthesizes the codec MCLK (3.072 MHz = 48 kHz x 64),
// matching engine::kSampleRate. Mono engine output is duplicated to both I2S
// channels and converted float -> int16. This core owns the audio engine only;
// the control core (cm33) queues notes/params into the shared SDRAM ring.

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "engine_audio.h"
#include "scope_tap.h"
#include "usb_composite.h"

namespace {

/// I2S frame: 16-bit stereo. One render block (64 mono frames) -> 64 frames.
constexpr int kFramesPerBlock = engine::kBlockSize;
constexpr int kBlockBytes = kFramesPerBlock * 2 * sizeof(int16_t);  // 256 B

/// In-flight DMA blocks; each is only 256 B, so a few is plenty.
constexpr int kNumBlocks = 4;

// Justified exception, not a pattern -- tx_slab is a Zephyr kernel memory slab:
// K_MEM_SLAB_DEFINE expands to a kernel object that must have static storage
// duration (it is registered with the kernel at build time, and the I2S driver
// holds a pointer to it for the process lifetime).
K_MEM_SLAB_DEFINE(tx_slab, WB_UP(kBlockBytes), kNumBlocks, 4);

/// The audio engine's complete DSP state. Static storage in DTCM (fast,
/// per-core tightly-coupled memory): the .dtcm_bss section attribute keeps the
/// ~3.5 KB struct off the small main-thread stack and off the shared SDRAM.
/// Zero-initialized by the C runtime (NOLOAD section).
engine::EngineAudio audio __attribute__((section(".dtcm_bss")));

/// Render one engine block into `buf` and tap the samples into the shared
/// scope ring so the UI core (cm33) can draw the scope.
void RenderTap(float *buf, ScopeTap *tap) {
    engine::Render(audio, buf, engine::kBlockSize);
    tap->ring.Write(buf, engine::kBlockSize);
    tap->dirty.store(true, std::memory_order_relaxed);
}

/// Convert a mono float block into an interleaved 16-bit stereo I2S block.
void ConvertToI16(const float *buf, int16_t *out) {
    for (int i = 0; i < engine::kBlockSize; ++i) {
        float s = buf[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        const int16_t v = static_cast<int16_t>(s * 32767.0f);
        out[2 * i] = v;      // left
        out[2 * i + 1] = v;  // right (mono duplicated)
    }
}

}  // namespace

int main(void) {
    // Bring up the composite USB device (UAC2 audio + MIDI 2.0). Best-effort:
    // the I2S audio path and scope continue even if USB fails to enumerate.
    if (usb::Init() != 0) {
        printk("usb: composite init failed\n");
    }

    const struct device *i2s = DEVICE_DT_GET(DT_ALIAS(i2s_tx));
    if (!device_is_ready(i2s)) {
        printk("i2s: device not ready\n");
        return -ENODEV;
    }

    struct i2s_config cfg = {};
    cfg.word_size = 16U;
    cfg.channels = 2U;
    cfg.format = I2S_FMT_DATA_FORMAT_I2S;
    cfg.frame_clk_freq = engine::kSampleRate;  // 48000 Hz
    cfg.block_size = kBlockBytes;
    cfg.timeout = 1000;  // ms, not a k_timeout_t
    cfg.options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER;
    cfg.mem_slab = &tx_slab;

    if (i2s_configure(i2s, I2S_DIR_TX, &cfg) < 0) {
        printk("i2s: configure failed\n");
        return -EIO;
    }

    // Point the audio engine at the shared IPC block (fixed SDRAM address).
    // The control core (cm33) owns EngineControl: it has seeded the default
    // routes and queued the A4 test note; this core only renders.
    audio.ipc = reinterpret_cast<engine::SharedIpc *>(engine::kSharedIpcAddr);

    // Reset the shared events + params before the render loop. This core's
    // render loop starts before the cm33 has run EngineControl::Init, so
    // without this the audio core would consume uninitialized-SDRAM garbage
    // note events and garbage params — a non-deterministic boot race that
    // makes the scope flat/frozen on some boots. The cm33's Init re-resets
    // and seeds the real note afterward; both Reset calls are idempotent.
    audio.ipc->events.Reset();
    audio.ipc->params.Reset(engine::k_params);

    // Reset the shared scope tap (fixed SDRAM address) before the render loop:
    // the SDRAM backing is uninitialized until a producer runs this.
    ScopeTap *tap = reinterpret_cast<ScopeTap *>(kScopeTapAddr);
    tap->Reset();

    // Start the I2S stream (best-effort). The codec MCLK (GPT PWM) is not yet
    // verified on hardware, so the DMA may stall; audio output is best-effort
    // and the scope is clocked independently below.
    (void)i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START);

    // Render + tap at the engine block rate, decoupled from the I2S
    // backpressure so the scope animates even when the I2S DMA stalls. The
    // I2S output is best-effort: convert + queue only if a DMA block is free.
    constexpr uint32_t kBlockUs =
        1000000u * engine::kBlockSize / engine::kSampleRate;  // ~1333 us
    for (;;) {
        float buf[engine::kBlockSize];
        RenderTap(buf, tap);

        void *blk;
        if (k_mem_slab_alloc(&tx_slab, &blk, K_NO_WAIT) == 0) {
            ConvertToI16(buf, static_cast<int16_t *>(blk));
            (void)i2s_write(i2s, blk, kBlockBytes);
        }
        k_busy_wait(kBlockUs);
    }
    return 0;
}
