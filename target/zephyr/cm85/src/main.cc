// twang cm85 — engine bring-up with I2S audio output.
//
// Renders the engine to the EK-RA8D2's on-board codec through SSIE1 (i2s1).
// A GPT PWM (pwm2) synthesizes the codec MCLK (3.072 MHz = 48 kHz x 64),
// matching engine::kSampleRate. Mono engine output is duplicated to both I2S
// channels and converted float -> int16.

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "engine.h"

namespace {

/// I2S frame: 16-bit stereo. One render block (64 mono frames) -> 64 frames.
constexpr int kFramesPerBlock = engine::kBlockSize;
constexpr int kBlockBytes = kFramesPerBlock * 2 * sizeof(int16_t);  // 256 B

/// In-flight DMA blocks; each is only 256 B, so a few is plenty.
constexpr int kNumBlocks = 4;

K_MEM_SLAB_DEFINE(tx_slab, WB_UP(kBlockBytes), kNumBlocks, 4);

/// Render one engine block into an interleaved 16-bit stereo I2S block.
void RenderBlock(int16_t *out) {
    float buf[engine::kBlockSize];
    engine::Render(buf, engine::kBlockSize);
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

    engine::EngineInit();
    engine::EngineNoteOn(0, 440.0f);  // A4 on part 0: audible test tone

    // Prime the first block, then start the stream.
    void *blk;
    if (k_mem_slab_alloc(&tx_slab, &blk, K_FOREVER) < 0) {
        return -ENOMEM;
    }
    RenderBlock(static_cast<int16_t *>(blk));
    if (i2s_write(i2s, blk, kBlockBytes) < 0) {
        printk("i2s: first write failed\n");
        return -EIO;
    }
    if (i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START) < 0) {
        printk("i2s: trigger failed\n");
        return -EIO;
    }
    printk("twang cm85: engine -> I2S (48 kHz stereo)\n");

    // Continuous render loop. The slab gives backpressure: k_mem_slab_alloc
    // blocks until the driver frees a completed DMA block.
    for (;;) {
        if (k_mem_slab_alloc(&tx_slab, &blk, K_FOREVER) < 0) {
            break;
        }
        RenderBlock(static_cast<int16_t *>(blk));
        if (i2s_write(i2s, blk, kBlockBytes) < 0) {
            printk("i2s: write failed\n");
            break;
        }
    }
    return 0;
}
