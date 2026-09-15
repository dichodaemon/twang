/// @file engine_audio.h
/// @brief Audio-side engine: the audio core's DSP state and render entry.
///
/// The audio core (M85 on the target, the host's audio thread on the desktop)
/// owns a plain public POD struct of per-voice/per-part/per-bus state plus the
/// shared transport pointer. It renders by draining the transport's pending
/// events and parameters at each block boundary. Data-oriented on purpose:
/// there is nothing to hide (single consumer), so the state is public and the
/// helpers are free functions.

#pragma once

#include "engine.h"
#include "ipc_shared.h"

namespace engine {

/// Per-block stereo accumulation. Phase 1 is mono: voices accumulate into
/// buses[0].L and Render downmixes to the mono `out`; the R side and the
/// per-voice pan/level/send taps are phase 4.
struct Bus {
    float L[kBlockSize];
    float R[kBlockSize];
};

/// The audio core's complete DSP state. Plain old data (memcpy-able), owned by
/// the caller — static storage in DTCM on the target, never the stack — and
/// zero-initialized at construction. The control core never touches this
/// state; it reaches the shared transport through `ipc`.
struct EngineAudio {
    Part  parts[kNumParts];             ///< committed from the transport each block
    Voice voices[kNumVoices];           ///< per-voice DSP state
    Bus   buses[kNumBuses];             ///< per-block accumulation
    bool  drive_in_use[kNumParts];      ///< per-part shaper gate
    bool  prev_drive_in_use[kNumParts]; ///< previous control step (enable edges)
    SharedIpc *ipc = nullptr;           ///< the shared transport (set once)
};

/// @brief Render `frames` mono samples into `out` (audio thread).
///
/// Drains the pending events and parameters from `e.ipc` at each block
/// boundary. Output is the sum of all active voices, soft-saturated then
/// clamped to [-1, 1].
/// @param e Audio state (its `ipc` must be set to the shared block).
/// @param out Destination buffer (holds at least `frames` floats).
/// @param frames Number of samples to render.
void Render(EngineAudio &e, float *out, int frames);

}  // namespace engine
