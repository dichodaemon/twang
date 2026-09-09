#include "allocator.h"

namespace engine {

// Per-part reservation: the guaranteed minimum voices a part can always hold.
// The allocator reads this data; it does not assume a value. Runtime
// configurability (validation + UI + persistence) is deferred.
constexpr int kReservation[kNumParts] = {3, 3, 3, 3};

Allocator::Allocator() { Reset(); }

void Allocator::Reset() {
    for (int i = 0; i < kNumVoices; ++i)
        owner_[i] = Owner{false, 0.0f, 0, 0};
    serial_ = 0;
}

Allocator::Decision Allocator::NoteOn(int part, float freq_hz) {
    if (part < 0 || part >= kNumParts) return {-1, false};
    int voice = FirstFree();
    if (voice >= 0) {
        Claim(voice, part, freq_hz);
        return {voice, false};
    }
    voice = PickVictim(part);
    if (voice < 0) return {-1, false};  // full and nothing to steal
    Claim(voice, part, freq_hz);
    return {voice, true};
}

int Allocator::NoteOff(int part, float freq_hz) {
    for (int i = 0; i < kNumVoices; ++i) {
        if (owner_[i].active && owner_[i].part == part &&
            owner_[i].freq == freq_hz) {
            owner_[i].active = false;
            return i;
        }
    }
    return -1;
}

int Allocator::ActiveCount(int part) const {
    int n = 0;
    for (int i = 0; i < kNumVoices; ++i)
        if (owner_[i].active && owner_[i].part == part) ++n;
    return n;
}

bool Allocator::VoiceActive(int voice) const { return owner_[voice].active; }
int Allocator::VoicePart(int voice) const { return owner_[voice].part; }
float Allocator::VoiceFreq(int voice) const { return owner_[voice].freq; }

int Allocator::FirstFree() const {
    for (int i = 0; i < kNumVoices; ++i)
        if (!owner_[i].active) return i;
    return -1;
}

void Allocator::Claim(int voice, int part, float freq_hz) {
    owner_[voice].active = true;
    owner_[voice].freq = freq_hz;
    owner_[voice].part = static_cast<std::uint8_t>(part);
    owner_[voice].serial = serial_++;
}

// Pick a victim voice to steal for a new note on `part`, per the reservation
// order: within the part first (if over its reservation), then from the part
// most over its reservation, never at/below reservation.
int Allocator::PickVictim(int part) const {
    if (ActiveCount(part) > kReservation[part])
        return OldestOf(part);  // give back one of our own surplus voices

    int best = -1;
    int best_over = 0;
    for (int p = 0; p < kNumParts; ++p) {
        if (p == part) continue;
        int over = ActiveCount(p) - kReservation[p];
        if (over > best_over) {
            best_over = over;
            best = p;
        }
    }
    if (best >= 0) return OldestOf(best);

    // Unreachable when sum(kReservation) < kNumVoices: all 24 busy forces
    // some part over its reservation. Defensive fallback only.
    return OldestOf(part);
}

int Allocator::OldestOf(int part) const {
    int best = -1;
    std::uint32_t best_serial = 0xFFFFFFFFu;
    for (int i = 0; i < kNumVoices; ++i) {
        if (owner_[i].active && owner_[i].part == part &&
            owner_[i].serial < best_serial) {
            best_serial = owner_[i].serial;
            best = i;
        }
    }
    return best;
}

}  // namespace engine
