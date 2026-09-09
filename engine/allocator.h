/// @file allocator.h
/// @brief Control-side voice allocator: per-part reservation + shared surplus.
///
/// Runs on the control thread (the M33 on the target). NoteOn/NoteOff return
/// a decision — which voice to use, and whether to start or steal it — and the
/// engine pushes the corresponding event onto the ring. The audio thread never
/// touches this state.

#pragma once

#include <cstdint>

#include "engine.h"

namespace engine {

/// Per-part reservation: the guaranteed minimum voices a part can always
/// hold. The allocator reads this data; it does not assume a value. Runtime
/// configurability (validation + UI + persistence) is deferred.
inline constexpr int kReservation[kNumParts] = {3, 3, 3, 3};

/// Assigns notes to voices with a per-part reservation floor and a shared
/// surplus pool (digest §9):
///
/// - a free voice is used first;
/// - otherwise the allocator steals — within the requesting part first (if it
///   is over its reservation), then from the part most over its reservation,
///   never from a part at or below its reservation.
///
/// "Reservation" is a stealing floor, not a pre-allocation: a part can always
/// hold `kReservation[part]` notes without being stolen, but its idle slots
/// stay in the shared free pool for other parts to borrow.
class Allocator {
  public:
    /// What NoteOn decided: which voice to use, and whether it is a steal.
    struct Decision {
        int voice;   ///< voice index, or -1 if the note is dropped
        bool steal;  ///< true → steal (fast release then retrigger)
    };

    Allocator() { Reset(); }

    void Reset() {
        for (int i = 0; i < kNumVoices; ++i)
            owner_[i] = Owner{false, 0.0f, 0, 0};
        serial_ = 0;
    }

    /// Assign a new note for `part` at `freq_hz`.
    Decision NoteOn(int part, float freq_hz) {
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

    /// Release the note `freq_hz` in `part`. Returns the voice, or -1.
    int NoteOff(int part, float freq_hz) {
        for (int i = 0; i < kNumVoices; ++i) {
            if (owner_[i].active && owner_[i].part == part &&
                owner_[i].freq == freq_hz) {
                owner_[i].active = false;
                return i;
            }
        }
        return -1;
    }

    /// Number of held notes in `part` (control-side view).
    int ActiveCount(int part) const {
        int n = 0;
        for (int i = 0; i < kNumVoices; ++i)
            if (owner_[i].active && owner_[i].part == part) ++n;
        return n;
    }

    // Introspection (tests, voice-usage meters).
    bool VoiceActive(int voice) const { return owner_[voice].active; }
    int VoicePart(int voice) const { return owner_[voice].part; }
    float VoiceFreq(int voice) const { return owner_[voice].freq; }

  private:
    struct Owner {
        bool active;
        float freq;
        std::uint8_t part;
        std::uint32_t serial;  ///< monotonic note-on order, for "oldest" steals
    };

    int FirstFree() const {
        for (int i = 0; i < kNumVoices; ++i)
            if (!owner_[i].active) return i;
        return -1;
    }

    void Claim(int voice, int part, float freq_hz) {
        owner_[voice].active = true;
        owner_[voice].freq = freq_hz;
        owner_[voice].part = static_cast<std::uint8_t>(part);
        owner_[voice].serial = serial_++;
    }

    // Pick a victim voice to steal for a new note on `part`, per the
    // reservation order: within the part first (if over its reservation), then
    // from the part most over its reservation, never at/below reservation.
    int PickVictim(int part) const {
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

    int OldestOf(int part) const {
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

    Owner owner_[kNumVoices];
    std::uint32_t serial_;
};

}  // namespace engine
