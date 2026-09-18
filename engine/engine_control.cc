/// @file engine_control.cc
/// @brief EngineControl implementation: the control core's note/param/route
/// writes over the shared transport.

#include "engine_control.h"

#include <cstddef>

#include "params.h"

#ifdef TWANG_SHARED_IPC
#include "loss_counters.h"
#endif

namespace engine {

__attribute__((weak)) void EngineEventsPending() {}

#ifdef TWANG_SHARED_IPC
namespace {
// Increment the transport's IPC event-drop counter (diagnostic; read over
// J-Link). Target-only — on the desktop there is no fixed-address block.
void CountEventDrop() {
    reinterpret_cast<LossCounters *>(kLossCountersAddr)
        ->ipc_event_drops.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace
#else
namespace {
void CountEventDrop() {}
}  // namespace
#endif

void EngineControl::Init(SharedIpc &ipc) {
    ipc_ = &ipc;
    batching_ = false;
    ipc_->meter.store(0.0f, std::memory_order_relaxed);
    alloc_.Reset();
    ipc_->events.Reset();
    ipc_->params.Reset(k_params);

    // Pre-populate the 5 default routes (arch-design §5.4). Slots 0-2 absorb
    // today's hardcoded modulation (velocity->amp, env0->amp, env1->cutoff);
    // velocity->amp uses full depth (1.0). kAmp is a pure level (default 1.0)
    // — the polyphony headroom now lives on the bus (kBusGain 0.125), not in
    // kAmp. Slot 3 (key follow) is enabled at half depth (0.5) and slot 4
    // (pitchbend) is off (amount 0) so it contributes nothing at rest. Slot 3's
    // amount is a seed only — key-follow depth is read from the named
    // Part::key_follow_depth field, not this route's amount.
    for (int p = 0; p < kNumParts; ++p) {
        SetRoute(p, 0, ModSourceId::kVelocity, ParamRef{0, ParamId::kAmp}, 1.0f);
        SetRoute(p, 1, ModSourceId::kEnv0, ParamRef{0, ParamId::kAmp}, 1.0f);
        SetRoute(p, 2, ModSourceId::kEnv1, ParamRef{0, ParamId::kCutoff}, 0.0f);
        SetRoute(p, 3, ModSourceId::kNote, ParamRef{0, ParamId::kCutoff}, 0.5f);
        SetRoute(p, 4, ModSourceId::kPitchBend, ParamRef{0, ParamId::kPitchCoarse}, 0.0f);
    }
}

void EngineControl::NoteOn(int part, float freq_hz, std::uint8_t velocity) {
    const Allocator::Decision d = alloc_.NoteOn(part, freq_hz);
    if (d.voice < 0) return;  // dropped: full and nothing to steal
    const Event::Type type =
        d.steal ? Event::Type::kSteal : Event::Type::kNoteOn;
    if (!ipc_->events.Push({type, static_cast<std::uint8_t>(part),
                            static_cast<std::uint8_t>(d.voice), velocity,
                            freq_hz})) {
        CountEventDrop();
    }
    EngineEventsPending();
}

void EngineControl::NoteOff(int part, float freq_hz) {
    const int voice = alloc_.NoteOff(part, freq_hz);
    if (voice < 0) return;  // no matching note
    if (!ipc_->events.Push({Event::Type::kNoteOff,
                            static_cast<std::uint8_t>(part),
                            static_cast<std::uint8_t>(voice), 0, 0.0f})) {
        CountEventDrop();
    }
    EngineEventsPending();
}

void EngineControl::AllNotesOff(int part) {
    const std::uint32_t released = alloc_.AllNotesOff(part);
    for (int v = 0; v < kNumVoices; ++v) {
        if (released & (1u << v)) {
            if (!ipc_->events.Push({Event::Type::kNoteOff,
                                    static_cast<std::uint8_t>(part),
                                    static_cast<std::uint8_t>(v), 0, 0.0f})) {
                CountEventDrop();
            }
        }
    }
    if (released) {
        EngineEventsPending();
    }
}

void EngineControl::SetParam(int part, ParamRef ref, float norm) {
    if (part < 0 || part >= kNumParts) return;
    ipc_->params.Set(part, ref, norm);
    if (!batching_) ipc_->params.Flush();
}

void EngineControl::SetParamDisp(int part, ParamRef ref, float disp) {
    if (part < 0 || part >= kNumParts) return;
    ipc_->params.Set(
        part, ref,
        ParamDispToNorm(&k_params[static_cast<std::size_t>(ref.id)], disp));
    if (!batching_) ipc_->params.Flush();
}

float EngineControl::GetParam(int part, ParamRef ref) const {
    if (part < 0 || part >= kNumParts) return 0.0f;
    return ipc_->params.Get(part, ref);
}

bool EngineControl::SetRoute(int part, int slot, ModSourceId src, ParamRef dst,
                             float amount) {
    if (part < 0 || part >= kNumParts) return false;
    if (slot < 0 || slot >= kModSlots) return false;
    // dst must name a modulatable parameter. Named fields (kKeyFollowDepth),
    // performance inputs (kPitchBend), and kCount are not destinations; reject
    // them so a stored route can never silently do nothing.
    if (!k_params[static_cast<std::size_t>(dst.id)].modulatable) return false;
    ipc_->params.SetRoute(part, slot, src, dst, amount);
    if (!batching_) ipc_->params.Flush();
    return true;
}

bool EngineControl::GetRoute(int part, int slot, ModRoute *out) const {
    return ipc_->params.GetRoute(part, slot, out);
}

void EngineControl::BeginBatch() {
    batching_ = true;
}

void EngineControl::Flush() {
    batching_ = false;
    ipc_->params.Flush();
}

float EngineControl::GetMeter() {
    return ipc_->meter.exchange(0.0f, std::memory_order_relaxed);
}

}  // namespace engine
