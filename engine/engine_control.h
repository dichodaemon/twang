/// @file engine_control.h
/// @brief Control-side engine: the control core's note/param/route API over
/// the cross-core IPC.
///
/// The control core (M33 on the target, the host's control thread on the
/// desktop) writes notes, parameters and routes into the shared transport and
/// owns the voice allocator and batch bookkeeping. All mutable state is
/// private; callers reach the transport only through these methods, never a
/// global accessor.

#pragma once

#include <cstdint>

#include "allocator.h"
#include "ipc_shared.h"

namespace engine {

/// Notifier invoked after a note event is queued into the shared ring.
///
/// Defaults to nullptr (no-op on the desktop and in tests). The target passes
/// a function that signals the audio core (a mailbox send on the M33→M85
/// channel); the audio core drains the shared ring at its block boundary
/// regardless.
using EventNotify = void (*)();

/// The control core's runtime state. Owned by the caller (host/target main),
/// constructed once, `Init`-ed before the audio thread starts. Not copyable
/// (owns the allocator); reach it by pointer from the panel/interaction/midi.
class EngineControl {
  public:
    /// @brief Initialize the control side: reset the allocator, the shared
    /// event ring, the parameter block and the meter, and seed the 5 default
    /// routes. Call once on the control thread before the audio thread starts.
    /// @param ipc The shared transport block (fixed SDRAM address on the
    /// target; a plain object owned by main on the desktop). Referenced, not
    /// copied.
    /// @param notify Called after a note event is queued; nullptr (default) is
    /// a no-op. The target passes a signal-the-audio-core function.
    void Init(SharedIpc &ipc, EventNotify notify = nullptr);

    /// @brief Queue a note-on (control thread).
    void NoteOn(int part, float freq_hz, std::uint8_t velocity);

    /// @brief Queue a note-off (control thread).
    void NoteOff(int part, float freq_hz);

    /// @brief Release every active voice in `part` and queue one note-off per
    /// released voice (control thread; CC 123 All Notes Off).
    void AllNotesOff(int part);

    /// @brief Set a parameter's normalized value (control thread).
    void SetParam(int part, ParamRef ref, float norm);

    /// @brief Set a parameter from display units (control thread).
    void SetParamDisp(int part, ParamRef ref, float disp);

    /// @brief Read a parameter's current normalized value (control thread).
    /// Reads the transport only — no engine/control state.
    float GetParam(int part, ParamRef ref) const;

    /// @brief Set one modulation route (control thread).
    bool SetRoute(int part, int slot, ModSourceId src, ParamRef dst,
                  float amount);

    /// @brief Read one modulation route (control thread).
    /// Reads the transport only — no engine/control state.
    bool GetRoute(int part, int slot, ModRoute *out) const;

    /// @brief Begin a batched update (control thread); see Flush.
    void BeginBatch();

    /// @brief End a batched update and publish once (control thread).
    void Flush();

    /// @brief Read-and-clear the shared peak meter (control thread).
    float GetMeter();

  private:
    Allocator alloc_;
    bool batching_ = false;
    SharedIpc *ipc_ = nullptr;  ///< set once in Init; never null afterward
    EventNotify notify_ = nullptr;  ///< post-queue notifier (target-only)
};

}  // namespace engine
