// twang cm85 — engine bring-up with USB audio output.
//
// Renders the engine (48 kHz, block-oriented) and streams it to the host over
// the UAC2 capture endpoint. The render clock is the SSIE I2S DMA: each
// completed transfer releases a tx_slab block, and the render loop refills it
// — so the SSIE's BCLK/WCLK (derived from the GPT PWM MCLK) paces the synth
// at exactly the 48 kHz the UAC2 descriptor claims. The on-board codec's J38
// analog output is unfitted; the I2S is used as the clock, not the sound
// output. The scope tap stays as the engine-vs-USB discriminator.

#include <errno.h>
#include <cstring>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "engine_audio.h"
#include "loss_counters.h"
#include "midi_ring.h"
#include "scope_tap.h"
#include "sdram_map.h"
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
void RenderTap(float *buf, ScopeTap *tap)
{
    engine::Render(audio, buf, engine::kBlockSize);
    tap->ring.Write(buf, engine::kBlockSize);
    tap->dirty.store(true, std::memory_order_relaxed);
}

/// Convert a mono float block into an interleaved 16-bit stereo block.
void ConvertToI16(const float *buf, int16_t *out)
{
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

int main(void)
{
    // Reset the shared MIDI rings + loss counters before USB init. This core
    // (the producer) is the single owner of the rings; the cm33's control
    // thread waits on the boot magic before its first drain. Doing this here —
    // not inside usb::Init() — keeps it unconditional: usb::Init() can return
    // early on a failed device_is_ready and would otherwise leave the rings
    // uninitialized for the cm33's drain.
    reinterpret_cast<NoteRing *>(kNoteRingAddr)->Reset();
    reinterpret_cast<CcRing *>(kCcRingAddr)->Reset();
    LossCounters *loss = reinterpret_cast<LossCounters *>(kLossCountersAddr);
    loss->Reset();
    loss->boot_magic.store(kBootMagic, std::memory_order_release);

    // Bring up the composite USB device (UAC2 audio + MIDI 2.0). Best-effort:
    // the render + scope continue even if USB fails to enumerate.
    if (usb::Init() != 0) {
        printk("usb: composite init failed\n");
    }

    const struct device *i2s = DEVICE_DT_GET(DT_ALIAS(i2s_tx));
    if (device_is_ready(i2s)) {
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
        }

        // Prime the TX queue: START pulls one block with K_NO_WAIT and latches
        // I2S_STATE_ERROR (-ENOMEM) if the queue is empty, after which every
        // START returns -EIO until a PREPARE. Two silent blocks give the
        // driver one to send and one queued for the ISR to pick up.
        for (int i = 0; i < 2; ++i) {
            void *blk;
            if (k_mem_slab_alloc(&tx_slab, &blk, K_NO_WAIT) == 0) {
                std::memset(blk, 0, kBlockBytes);
                i2s_write(i2s, blk, kBlockBytes);
            }
        }

        const int rc = i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START);
        if (rc < 0) {
            printk("i2s: start failed %d\n", rc);
        }
    } else {
        printk("i2s: device not ready\n");
    }

    // Point the audio engine at the shared IPC block (fixed SDRAM address).
    // The control core (cm33) owns EngineControl: it has seeded the default
    // routes and queued the A4 test note; this core only renders.
    audio.ipc = reinterpret_cast<engine::SharedIpc *>(kSharedIpcAddr);

    // Reset the shared events + params before the render loop: this core's
    // render loop starts before the cm33 has run EngineControl::Init, so this
    // is the cold-boot safeguard against consuming uninitialized-SDRAM note
    // events and params. It is not "idempotent" — the two resets are safe
    // because each core resets before it begins its own side: the cm33
    // (producer) resets in EngineControl::Init before producing, and this core
    // (consumer) resets here before rendering. Neither resets while the other
    // is mid-flight.
    audio.ipc->events.Reset();
    audio.ipc->params.Reset(engine::k_params);

    // Reset the shared scope tap (fixed SDRAM address) before the render loop:
    // the SDRAM backing is uninitialized until a producer runs this.
    ScopeTap *tap = reinterpret_cast<ScopeTap *>(kScopeTapAddr);
    tap->Reset();

    // Render at the engine block rate, clocked by the SSIE I2S DMA: each
    // completed transfer returns a tx_slab block, and this loop refills it.
    // The scope tap is written in the same loop, so it animates at the render
    // rate. If the DMA never completes (clock fault), this loop blocks on
    // k_mem_slab_alloc after 4 blocks and the scope tap freezes — the
    // on-board diagnostic for the MCLK.
    for (;;) {
        void *blk;
        k_mem_slab_alloc(&tx_slab, &blk, K_FOREVER);

        int16_t *stereo = static_cast<int16_t *>(blk);
        float buf[engine::kBlockSize];
        RenderTap(buf, tap);
        ConvertToI16(buf, stereo);

        // Feed the UAC2 capture stream (the SOF callback drains it to the
        // host), then hand the block back to the SSIE DMA.
        usb::AudioPush(stereo, engine::kBlockSize);
        i2s_write(i2s, blk, kBlockBytes);
    }
    return 0;
}
