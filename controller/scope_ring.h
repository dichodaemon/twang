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
    /// @param src Samples to append.
    /// @param n Number of samples.
    void Write(const float *src, int n);

    /// @brief Read `count` samples, `stride` apart, ending at the newest
    /// sample (oldest first). UI thread.
    /// @param dst Destination buffer (holds at least `count` floats).
    /// @param count Number of samples to read.
    /// @param stride Sample distance between consecutive reads.
    /// Requires count * stride <= kCapacity.
    void ReadLast(float *dst, int count, int stride) const;

  private:
    std::atomic<std::uint32_t> write_{0};  ///< Monotonic sample count.
    // atomic<float> makes each slot access well-defined under concurrent
    // single-writer/single-reader use (no torn reads); memory_order_relaxed
    // lowers to plain loads/stores on ARM. The write_ release/acquire pair
    // orders publication of the samples.
    std::atomic<float> buf_[kCapacity] = {};
};
