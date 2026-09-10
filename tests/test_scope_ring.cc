// test_scope_ring.cc — concurrent single-writer/single-reader exercise of the
// scope ring. The atomic<float> slots make each slot access well-defined; this
// test exists so ThreadSanitizer can prove there is no data race, and so a
// deterministic tail read proves the ring round-trips intact.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

#include "scope_ring.h"

int main() {
    ScopeRing ring;

    // The writer streams a ramp: sample at global index s has value s % 1000.
    constexpr int kBlocks = 8192;
    constexpr int kBlock = 64;
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        float buf[kBlock];
        for (int block = 0; block < kBlocks; ++block) {
            for (int i = 0; i < kBlock; ++i)
                buf[i] = static_cast<float>((block * kBlock + i) % 1000);
            ring.Write(buf, kBlock);
        }
        stop.store(true, std::memory_order_relaxed);
    });

    // Reader drains while the writer is live; every sample must be a value the
    // ramp could produce (a torn float would be garbage/out-of-range).
    int bad = 0;
    float out[512];
    while (!stop.load(std::memory_order_relaxed)) {
        ring.ReadLast(out, 512, 1);
        for (int i = 0; i < 512; ++i)
            if (!std::isfinite(out[i]) || out[i] < 0.0f || out[i] >= 1000.0f)
                ++bad;
    }
    writer.join();

    // Deterministic tail: the last 512 ramp samples, in order.
    constexpr int kTotal = kBlocks * kBlock;
    ring.ReadLast(out, 512, 1);
    for (int i = 0; i < 512; ++i) {
        const float expect = static_cast<float>((kTotal - 512 + i) % 1000);
        if (out[i] != expect) ++bad;
    }

    if (bad) {
        std::printf("FAIL: %d mismatches\n", bad);
        return 1;
    }
    std::printf("PASS: scope ring\n");
    return 0;
}
