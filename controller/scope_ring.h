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

    ScopeRing() {
#if !defined(__ZEPHYR__)
        // Host only: zero the buffer so reads before the first Write stay
        // in-range. On the target this ring lives at a fixed SDRAM address
        // shared with the audio core (see scope_tap.h); the panel's
        // construction must NOT zero the 64 KB buffer while the audio core is
        // concurrently writing it — every slot is written before it is read,
        // so zero-fill buys nothing there and only races.
        for (auto &v : buf_) v.store(0.0f, std::memory_order_relaxed);
#endif
    }

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

    /// @brief Reset the write index (producer, once at boot).
    ///
    /// The ring lives at a fixed SDRAM address on the target and is not
    /// constructed by a linker-placed object, so its SDRAM backing is
    /// uninitialized until this runs. The buffer itself needs no reset: every
    /// slot is written before it is read.
    void Reset();

  private:
    std::atomic<std::uint32_t> write_{0};  ///< Monotonic sample count.
    // atomic<float> makes each slot access well-defined under concurrent
    // single-writer/single-reader use (no torn reads); memory_order_relaxed
    // lowers to plain loads/stores on ARM. The write_ release/acquire pair
    // orders publication of the samples.
    //
    // Deliberately NOT zero-initialized: on the target this ring lives at a
    // fixed SDRAM address shared with the audio core (see scope_tap.h). The
    // panel's construction must not zero the 64 KB buffer while the audio
    // core is concurrently writing it — every slot is written before it is
    // read, so zero-fill buys nothing and only races.
    std::atomic<float> buf_[kCapacity];
};
