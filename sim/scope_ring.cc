#include "scope_ring.h"

void ScopeRing::Write(const float *src, int n) {
    std::uint32_t pos = write_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        buf_[pos & (kCapacity - 1)] = src[i];
        ++pos;
    }
    write_.store(pos, std::memory_order_release);
}

void ScopeRing::ReadLast(float *dst, int count, int stride) const {
    const std::uint32_t pos = write_.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        dst[i] = buf_[(pos - (count - i) * stride) & (kCapacity - 1)];
    }
}
