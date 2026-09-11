#include "ipc.h"

namespace engine {

bool EventRing::Push(const Event &e) {
    std::size_t tail = tail_.load(std::memory_order_relaxed);
    std::size_t next = (tail + 1) & (kCapacity - 1);
    if (next == head_.load(std::memory_order_acquire))
        return false;  // full
    buf_[tail] = e;
    tail_.store(next, std::memory_order_release);
    return true;
}

bool EventRing::Pop(Event *e) {
    std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire))
        return false;  // empty
    *e = buf_[head];
    head_.store((head + 1) & (kCapacity - 1), std::memory_order_release);
    return true;
}

void EventRing::Reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

void ParamBlock::Set(int part, ParamId id, float norm) {
    ParamSet(&pending_[part], id, norm);
    Publish();
}

void ParamBlock::SetRoute(int part, int slot, ModSourceId src, ParamId dst,
                          float amount) {
    pending_[part].routes[slot] = {src, dst, amount};
    Publish();
}

void ParamBlock::Publish() {
    int back = 1 - front_.load(std::memory_order_acquire);
    // Wait until the audio thread finishes reading `back` (it only ever reads
    // the published buffer, so `back` is free once `reading_` moves off it).
    // The audio thread never blocks; only the control thread spins here, and
    // only when it laps a slow reader (rare, ~100 ns in practice).
    while (reading_.load(std::memory_order_acquire) == back) {
    }
    for (int p = 0; p < kNumParts; ++p) buf_[back][p] = pending_[p];
    front_.store(back, std::memory_order_release);
}

void ParamBlock::Commit(Part *parts) {
    int f;
    do {
        f = front_.load(std::memory_order_acquire);
        reading_.store(f, std::memory_order_release);
        // If the writer flipped `front_` while we were claiming, retry: the
        // buffer we just marked could be mid-rewrite by the writer.
    } while (front_.load(std::memory_order_acquire) != f);
    for (int p = 0; p < kNumParts; ++p) parts[p] = buf_[f][p];
    reading_.store(-1, std::memory_order_release);
}

void ParamBlock::Reset(const ParamDesc *table) {
    for (int p = 0; p < kNumParts; ++p) {
        pending_[p] = Part{};
        for (int i = 0; i < static_cast<int>(ParamId::kCount); ++i)
            ParamSet(&pending_[p], static_cast<ParamId>(i), table[i].def);
    }
    for (int p = 0; p < kNumParts; ++p) {
        buf_[0][p] = pending_[p];
        buf_[1][p] = pending_[p];
    }
    front_.store(0, std::memory_order_relaxed);
    reading_.store(-1, std::memory_order_relaxed);
}

}  // namespace engine
