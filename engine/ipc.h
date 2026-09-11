/// @file ipc.h
/// @brief Lock-free inter-thread communication: events and parameters.
///
/// The control thread produces note events and parameter changes; the audio
/// thread consumes them at block boundaries. Events travel over a
/// single-producer / single-consumer ring; parameters travel through a
/// double-buffered block. Keeping the two mechanisms separate avoids locking.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "params.h"

namespace engine {

/// A control event sent from the control thread to the audio thread.
struct Event {
    /// Event kind.
    enum class Type : std::uint8_t {
        kNoteOn,   ///< start a note
        kNoteOff,  ///< release a note
        kSteal,    ///< fast-ramp a voice down, then start a new note
    };

    Type type;
    std::uint8_t part;     ///< Part index; meaningful for kNoteOn and kSteal.
    std::uint8_t voice;    ///< Voice slot this event targets.
    std::uint8_t velocity; ///< MIDI velocity 1..127; meaningful for kNoteOn/kSteal.
    float freq;            ///< Note frequency in Hz; meaningful for kNoteOn/kSteal.
};

/// Lock-free single-producer / single-consumer ring of events.
///
/// `Push` is the control thread; `Pop` is the audio thread. The capacity must
/// be a power of two.
class EventRing {
  public:
    static constexpr std::size_t kCapacity = 32;  ///< power of two

    /// @brief Append an event.
    /// @param e Event to append.
    /// @return false if the ring is full (event dropped).
    bool Push(const Event &e);

    /// @brief Remove the oldest event.
    /// @param e Destination for the popped event.
    /// @return false if the ring is empty.
    bool Pop(Event *e);

    /// @brief Discard all pending events (single-threaded init only).
    void Reset();

  private:
    std::atomic<std::size_t> head_{0};  ///< consumer index
    std::atomic<std::size_t> tail_{0};  ///< producer index
    Event buf_[kCapacity];
};

/// Single-writer / single-reader parameter block, double-buffered.
///
/// The control thread keeps the authoritative set in `pending_` (whole `Part`
/// structs — params, key-follow depth, and routes). Publishing copies it into
/// the back buffer and advances the monotonic `front_` counter (the published
/// buffer index is `front_ & 1`). The audio thread snapshots the front buffer
/// into the parts at each block boundary. A `reading_` flag lets the writer
/// wait (spin, control thread only) until the audio thread finishes reading a
/// buffer before it reuses that buffer — the audio thread never takes a lock
/// (it may retry its claim while the control thread publishes).
/// `front_` is a monotonic counter (not a single bit) so a claim-recheck can
/// distinguish "never moved" from "published twice and came back" (the ABA
/// hazard); `last_front_` lets `Commit` skip the copy when nothing changed.
class ParamBlock {
  public:
    /// @brief Update one normalized parameter for a part and publish the set.
    /// Control thread only (single writer).
    /// @param part Part index in [0, kNumParts).
    /// @param id Parameter identifier.
    /// @param norm Normalized value in [0, 1].
    void Set(int part, ParamId id, float norm);

    /// @brief Read a parameter's current normalized value (control thread).
    /// @param part Part index in [0, kNumParts).
    /// @param id Parameter identifier.
    /// @return Value in [0, 1].
    float Get(int part, ParamId id) const {
        return ParamGet(&pending_[part], id);
    }

    /// @brief Set one modulation route for a part and publish the set.
    /// Control thread only (single writer).
    /// @param part Part index in [0, kNumParts).
    /// @param slot Route slot in [0, kModSlots).
    /// @param src Modulation source; kNone clears the slot.
    /// @param dst Destination parameter (a params[] member).
    /// @param amount Signed normalized amount in [-1, 1].
    void SetRoute(int part, int slot, ModSourceId src, ParamId dst,
                  float amount);

    /// @brief Snapshot the front buffer into all parts (audio thread).
    /// @param parts Destination part array (holds at least `kNumParts`).
    void Commit(Part *parts);

    /// @brief Reset both buffers to defaults (single-threaded init only).
    /// @param table Parameter descriptor table (g_params).
    void Reset(const ParamDesc *table);

  private:
    /// @brief Copy `pending_` into the back buffer and flip the front index.
    /// Control thread only (single writer).
    void Publish();

    Part pending_[kNumParts];              ///< control-thread authoritative state
    Part buf_[2][kNumParts];               ///< shared: audio reads buf_[front_ & 1]
    std::atomic<std::uint32_t> front_{0};  ///< monotonic publish counter; buffer = front_ & 1
    std::atomic<int> reading_{-1};         ///< buffer the audio thread is reading (-1 = none)
    std::uint32_t last_front_{0};          ///< generation last committed (audio thread only)
};

}  // namespace engine
