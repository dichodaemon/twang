#include "scope_ring.h"

void ScopeRing::Write(const float *src, int n) {
    std::uint32_t pos = write_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        buf_[pos & (kCapacity - 1)].store(src[i], std::memory_order_relaxed);
        ++pos;
    }
    write_.store(pos, std::memory_order_release);
}

void ScopeRing::ReadLast(float *dst, int count, int stride) const {
    const std::uint32_t pos = write_.load(std::memory_order_acquire);
    const std::uint32_t span =
        static_cast<std::uint32_t>(count) * static_cast<std::uint32_t>(stride);
    if (pos < span) {
        // Ring not yet full: there is no complete window. Return a flat line
        // rather than reading slots that were never written (on the target the
        // buffer is uninitialized SDRAM; reading it before the audio core has
        // filled it would feed garbage/NaN into the scope plot).
        for (int i = 0; i < count; ++i) dst[i] = 0.0f;
        return;
    }
    for (int i = 0; i < count; ++i) {
        dst[i] = buf_[(pos - (count - i) * stride) & (kCapacity - 1)].load(
            std::memory_order_relaxed);
    }
}

void ScopeRing::Reset() {
    write_.store(0, std::memory_order_relaxed);
}

void ScopeRing::Clear() {
    for (auto &v : buf_) v.store(0.0f, std::memory_order_relaxed);
}
