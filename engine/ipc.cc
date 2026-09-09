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
    pending_[slot(part, static_cast<int>(id))] = norm;
    int back = 1 - front_.load(std::memory_order_relaxed);
    for (int i = 0; i < kSlots; ++i)
        buf_[back].v[i].store(pending_[i], std::memory_order_relaxed);
    front_.store(back, std::memory_order_release);
}

void ParamBlock::Commit(Part *parts) {
    int front = front_.load(std::memory_order_acquire);
    for (int p = 0; p < kNumParts; ++p)
        for (int i = 0; i < kParamCount; ++i)
            ParamSet(&parts[p], static_cast<ParamId>(i),
                     buf_[front].v[slot(p, i)].load(std::memory_order_relaxed));
}

void ParamBlock::Reset(const ParamDesc *table) {
    for (int p = 0; p < kNumParts; ++p)
        for (int i = 0; i < kParamCount; ++i) {
            pending_[slot(p, i)] = table[i].def;
            buf_[0].v[slot(p, i)].store(table[i].def,
                                        std::memory_order_relaxed);
            buf_[1].v[slot(p, i)].store(table[i].def,
                                        std::memory_order_relaxed);
        }
    front_.store(0, std::memory_order_relaxed);
}

int ParamBlock::slot(int part, int i) { return part * kParamCount + i; }

}  // namespace engine
