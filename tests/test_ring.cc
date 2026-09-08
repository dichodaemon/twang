#include <cstdio>

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
    EventRing ring;
    Event e;

    Check(!ring.Pop(&e), "empty ring pops nothing");

    const int n = static_cast<int>(EventRing::kCapacity) - 1;  // max occupancy
    for (int i = 0; i < n; ++i)
        Check(ring.Push({Event::Type::kNoteOn, 100.0f + i}), "push fits");
    Check(!ring.Push({Event::Type::kNoteOn, 0.0f}), "full ring rejects");

    for (int i = 0; i < n; ++i) {
        Check(ring.Pop(&e), "pop succeeds");
        Check(e.type == Event::Type::kNoteOn && e.freq == 100.0f + i,
              "FIFO order preserved");
    }
    Check(!ring.Pop(&e), "ring drained");

    ring.Push({Event::Type::kNoteOff, 0.0f});
    Check(ring.Pop(&e) && e.type == Event::Type::kNoteOff,
          "note-off round-trips");

    ring.Reset();
    Check(!ring.Pop(&e), "reset clears the ring");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: event ring\n");
    return 0;
}
