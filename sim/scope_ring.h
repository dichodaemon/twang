/// @file scope_ring.h
/// @brief Lock-free single-producer/single-consumer ring feeding the scope.
///
/// The audio thread appends rendered samples (`Write`); the UI thread reads a
/// trailing, decimated window (`ReadLast`) to draw the scope. A single atomic
/// write index is the only shared state: the producer never blocks and never
/// waits on the UI, so this adds zero latency to the audio path — it is a
/// display-only tap.

#pragma once

#include <atomic>
#include <cstdint>

class ScopeRing {
  public:
    /// Capacity in samples; a power of two. Sized well above the widest scope
    /// window so the producer can never wrap into a window being read.
    static constexpr int kCapacity = 1 << 14;  // 16384 (~341 ms @ 48 kHz)

    /// @brief Append samples (audio thread).
    void Write(const float *src, int n) {
        std::uint32_t pos = write_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            buf_[pos & (kCapacity - 1)] = src[i];
            ++pos;
        }
        write_.store(pos, std::memory_order_release);
    }

    /// @brief Read `count` samples, `stride` apart, ending at the newest
    /// sample (oldest first). UI thread. Requires count * stride <= kCapacity.
    void ReadLast(float *dst, int count, int stride) const {
        const std::uint32_t pos = write_.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i) {
            dst[i] = buf_[(pos - (count - i) * stride) & (kCapacity - 1)];
        }
    }

  private:
    std::atomic<std::uint32_t> write_{0};  ///< Monotonic sample count.
    float buf_[kCapacity] = {};            ///< Circular sample storage.
};
