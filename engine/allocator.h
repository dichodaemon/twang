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

    /// @brief Construct an allocator with all voices free.
    Allocator();

    /// @brief Reset all voices to free and restart the note serial.
    void Reset();

    /// @brief Assign a new note for `part`, stealing a voice if none is free.
    /// @param part Part index in [0, kNumParts).
    /// @param freq_hz Note frequency in Hz.
    /// @return The chosen voice and whether it is a steal; `voice` is -1 if
    /// the note is dropped.
    Decision NoteOn(int part, float freq_hz);

    /// @brief Release the note `freq_hz` in `part`.
    /// @param part Part index in [0, kNumParts).
    /// @param freq_hz Note frequency in Hz.
    /// @return The released voice, or -1 if no matching note is held.
    int NoteOff(int part, float freq_hz);

    /// @brief Number of held notes in `part` (control-side view).
    /// @param part Part index in [0, kNumParts).
    /// @return Held-note count.
    int ActiveCount(int part) const;

    /// @brief Whether a voice holds a note.
    /// @param voice Voice index in [0, kNumVoices).
    /// @return true if the voice is active.
    bool VoiceActive(int voice) const;

    /// @brief The part a voice is bound to.
    /// @param voice Voice index in [0, kNumVoices).
    /// @return Part index.
    int VoicePart(int voice) const;

    /// @brief The frequency of the note a voice holds.
    /// @param voice Voice index in [0, kNumVoices).
    /// @return Note frequency in Hz.
    float VoiceFreq(int voice) const;

  private:
    struct Owner {
        bool active;           ///< a note occupies this voice
        float freq;            ///< note frequency, for note-off matching
        std::uint8_t part;     ///< owning part
        std::uint32_t serial;  ///< note-on order, for "oldest" stealing
    };

    /// @return The first free voice index, or -1 if all are active.
    int FirstFree() const;

    /// @brief Bind a voice to a new note and stamp its serial.
    /// @param voice Voice index in [0, kNumVoices).
    /// @param part Part index in [0, kNumParts).
    /// @param freq_hz Note frequency in Hz.
    void Claim(int voice, int part, float freq_hz);

    /// @brief Choose a victim voice to steal for a new note on `part`.
    /// @param part Part index in [0, kNumParts).
    /// @return Victim voice index, or -1 if nothing is stealable.
    int PickVictim(int part) const;

    /// @brief The oldest (lowest serial) active voice of `part`.
    /// @param part Part index in [0, kNumParts).
    /// @return Voice index, or -1 if `part` holds no notes.
    int OldestOf(int part) const;

    Owner owner_[kNumVoices];  ///< per-voice ownership
    std::uint32_t serial_;     ///< monotonic note-on counter
};

}  // namespace engine
