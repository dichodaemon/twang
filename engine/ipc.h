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
    std::uint8_t part;   ///< Part index; meaningful for kNoteOn and kSteal.
    std::uint8_t voice;  ///< Voice slot this event targets.
    float freq;          ///< Note frequency in Hz; meaningful for kNoteOn/kSteal.
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

/// Double-buffered parameter block.
///
/// The control thread keeps the authoritative set in `pending_` and publishes
/// it wholesale into the back buffer, then flips the front index. The audio
/// thread snapshots the front buffer into the parts at each block boundary.
/// Publishing the full set keeps the front buffer a consistent snapshot even
/// though the control updates one parameter at a time.
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
        return pending_[slot(part, static_cast<int>(id))];
    }

    /// @brief Snapshot the front buffer into all parts (audio thread).
    /// @param parts Destination part array (holds at least `kNumParts`).
    void Commit(Part *parts);

    /// @brief Reset both buffers to defaults (single-threaded init only).
    /// @param table Parameter descriptor table (g_params).
    void Reset(const ParamDesc *table);

  private:
    static constexpr int kParamCount = static_cast<int>(ParamId::kCount);
    static constexpr int kSlots = kNumParts * kParamCount;

    /// @brief Linearize (part, param) into the flat slot index.
    static int slot(int part, int i);

    float pending_[kSlots];  ///< control-thread-only authoritative set

    struct ParamValues {
        std::atomic<float> v[kSlots];
    };
    ParamValues buf_[2];         ///< shared: audio reads buf_[front_]
    std::atomic<int> front_{0};  ///< which buffer the audio reads
};

}  // namespace engine
