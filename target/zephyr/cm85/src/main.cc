// twang cm85 — engine bring-up.
//
// Proves the portable engine cross-compiles and runs on the target. Triggers
// a 440 Hz note and renders one block, reporting the peak sample magnitude.

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "engine.h"

int main(void)
{
    printk("twang cm85: engine bring-up\n");

    engine::EngineInit();
    engine::EngineNoteOn(0, 440.0f);

    float buf[engine::kBlockSize];
    engine::Render(buf, engine::kBlockSize);

    float peak = 0.0f;
    for (int i = 0; i < engine::kBlockSize; ++i) {
        const float m = (buf[i] < 0.0f) ? -buf[i] : buf[i];
        peak = (m > peak) ? m : peak;
    }

    printk("rendered %d samples, peak = %d (x1000)\n",
           engine::kBlockSize, static_cast<int>(peak * 1000.0f));
    return 0;
}
