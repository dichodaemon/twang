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

/// Assigns notes to voices with a per-part reservation floor and a shared
/// surplus pool (digest §9):
///
/// - a free voice is used first;
/// - otherwise the allocator steals — within the requesting part first (if it
///   is over its reservation), then from the part most over its reservation,
///   never from a part at or below its reservation.
///
/// "Reservation" is a stealing floor, not a pre-allocation: a part can always
/// hold its reservation count without being stolen, but its idle slots stay in
/// the shared free pool for other parts to borrow.
class Allocator {
  public:
    /// What NoteOn decided: which voice to use, and whether it is a steal.
    struct Decision {
        int voice;   ///< voice index, or -1 if the note is dropped
        bool steal;  ///< true → steal (fast release then retrigger)
    };

    Allocator();
    void Reset();

    /// Assign a new note for `part` at `freq_hz`.
    Decision NoteOn(int part, float freq_hz);

    /// Release the note `freq_hz` in `part`. Returns the voice, or -1.
    int NoteOff(int part, float freq_hz);

    /// Number of held notes in `part` (control-side view).
    int ActiveCount(int part) const;

    // Introspection (tests, voice-usage meters).
    bool VoiceActive(int voice) const;
    int VoicePart(int voice) const;
    float VoiceFreq(int voice) const;

  private:
    struct Owner {
        bool active;
        float freq;
        std::uint8_t part;
        std::uint32_t serial;  ///< monotonic note-on order, for "oldest" steals
    };

    int FirstFree() const;
    void Claim(int voice, int part, float freq_hz);
    int PickVictim(int part) const;
    int OldestOf(int part) const;

    Owner owner_[kNumVoices];
    std::uint32_t serial_;
};

}  // namespace engine
