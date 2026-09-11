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
    const std::uint32_t f = front_.load(std::memory_order_seq_cst);
    const int back = static_cast<int>((f + 1u) & 1u);  // buffer to publish into
    // Wait until the audio thread finishes reading `back` (it only ever reads
    // the published buffer, so `back` is free once `reading_` moves off it).
    // The audio thread never takes a lock; only the control thread spins here,
    // and only when it laps a slow reader (rare, ~100 ns in practice).
    while (reading_.load(std::memory_order_seq_cst) == back) {
    }
    for (int p = 0; p < kNumParts; ++p) buf_[back][p] = pending_[p];
    front_.store(f + 1u, std::memory_order_seq_cst);
}

void ParamBlock::Commit(Part *parts) {
    std::uint32_t f = front_.load(std::memory_order_seq_cst);
    if (f == last_front_) return;  // nothing published since the last commit

    // Claim the published buffer, then verify the counter has not advanced.
    // The monotonic counter distinguishes "published twice and came back" from
    // "never moved" (the ABA case a single-bit index cannot see). seq_cst
    // orders the reading_ store before the front_ re-load, so the writer
    // observes our claim before it can reuse the buffer (StoreLoad).
    do {
        f = front_.load(std::memory_order_seq_cst);
        reading_.store(static_cast<int>(f & 1u), std::memory_order_seq_cst);
    } while (front_.load(std::memory_order_seq_cst) != f);
    for (int p = 0; p < kNumParts; ++p) parts[p] = buf_[f & 1u][p];
    reading_.store(-1, std::memory_order_seq_cst);
    last_front_ = f;
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
    last_front_ = ~0u;  // "nothing committed yet" — force the first Commit to copy
}

}  // namespace engine
