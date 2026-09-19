// test_event_drops.cc — the transport's event-drop counter.
//
// Fills the shared event ring to its max occupancy, then drives one more
// note-on through EngineControl so the real NoteOn -> Push-fails -> bump path
// is exercised (not a hand-bump of the counter). EventRing reserves one slot,
// so max occupancy is kCapacity - 1 (see test_ring.cc).

#include <cstdio>

#include "engine_control.h"
#include "ipc.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

int main() {
    SharedIpc ipc;
    EngineControl control;
    control.Init(ipc);  // default null notifier; resets the rings

    Check(ipc.event_drops.load(std::memory_order_relaxed) == 0,
          "starts at zero");

    // Fill the event ring to its max occupancy directly, so the NoteOn below
    // is guaranteed to attempt a push into a full ring.
    const int n = static_cast<int>(EventRing::kCapacity) - 1;  // 31
    for (int i = 0; i < n; ++i) {
        ipc.events.Push({Event::Type::kNoteOn, 0, 0, 127, 100.0f + i});
    }
    Check(ipc.event_drops.load(std::memory_order_relaxed) == 0,
          "no drop under capacity");

    // One more note-on: the allocator grants a voice (all free), the engine
    // pushes into the full ring, Push fails, and the transport counter bumps.
    control.NoteOn(0, 69, 440.0f, 127);
    Check(ipc.event_drops.load(std::memory_order_relaxed) == 1,
          "one drop counted");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: event drops\n");
    return 0;
}
