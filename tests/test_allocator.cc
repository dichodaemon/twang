#include <cstdio>

#include "allocator.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

// Fill `count` notes on `part` with distinct frequencies.
static void Fill(Allocator *a, int part, int count, float base) {
    for (int i = 0; i < count; ++i)
        a->NoteOn(part, base + static_cast<float>(i));
}

int main() {
    // Free voices are used in index order, no steal.
    {
        Allocator a;
        const Allocator::Decision d0 = a.NoteOn(0, 100.0f);
        const Allocator::Decision d1 = a.NoteOn(0, 101.0f);
        const Allocator::Decision d2 = a.NoteOn(0, 102.0f);
        Check(d0.voice == 0 && !d0.steal, "first note → voice 0, no steal");
        Check(d1.voice == 1 && !d1.steal, "second note → voice 1");
        Check(d2.voice == 2 && !d2.steal, "third note → voice 2");
        Check(a.ActiveCount(0) == 3, "three held notes in part 0");

        Check(a.NoteOff(0, 101.0f) == 1, "note-off releases the matching voice");
        Check(a.ActiveCount(0) == 2, "count decrements after note-off");
        Check(a.NoteOff(0, 999.0f) == -1, "note-off of a missing note → -1");
    }

    // A part at its reservation steals from an over-reservation part, never
    // from its own reserved voices.
    {
        Allocator a;
        Fill(&a, 0, 3, 100.0f);  // part 0 at reservation (3)
        Fill(&a, 1, 7, 200.0f);  // part 1 over by 4
        Fill(&a, 2, 7, 300.0f);  // part 2 over by 4
        Fill(&a, 3, 7, 400.0f);  // part 3 over by 4  → 24 total, all busy
        Check(a.ActiveCount(0) == 3 && a.ActiveCount(1) == 7 &&
                  a.ActiveCount(2) == 7 && a.ActiveCount(3) == 7,
              "24 voices all busy");

        const Allocator::Decision d = a.NoteOn(0, 999.0f);
        Check(d.voice == 3 && d.steal, "steal the oldest note of part 1");
        Check(a.ActiveCount(0) == 4, "part 0 gained the voice");
        Check(a.ActiveCount(1) == 6, "part 1 lost the voice");
        Check(a.VoiceFreq(0) == 100.0f && a.VoiceFreq(1) == 101.0f &&
                  a.VoiceFreq(2) == 102.0f,
              "part 0's reserved notes untouched");
    }

    // A part over its reservation sacrifices its own oldest note first.
    {
        Allocator a;
        Fill(&a, 0, 6, 100.0f);  // part 0 over by 3
        Fill(&a, 1, 6, 200.0f);  // parts 1..3 over by 3 each
        Fill(&a, 2, 6, 300.0f);
        Fill(&a, 3, 6, 400.0f);  // → 24 total, all busy
        Check(a.ActiveCount(0) == 6, "part 0 holds 6 (over reservation)");

        const Allocator::Decision d = a.NoteOn(0, 999.0f);
        Check(d.voice == 0 && d.steal, "steal own oldest when over reservation");
        Check(a.ActiveCount(0) == 6, "count unchanged after self-steal");
        Check(a.VoiceFreq(0) == 999.0f, "voice 0 now plays the new note");
    }

    // Never steal from a part at/below its reservation.
    {
        Allocator a;
        a.NoteOn(1, 200.0f);       // part 1: 1 note (below its 3)
        Fill(&a, 2, 7, 300.0f);    // part 2: over by 4
        Fill(&a, 0, 16, 100.0f);   // part 0: over by 13  → 24 total, all busy
        Check(a.ActiveCount(0) == 16 && a.ActiveCount(1) == 1 &&
                  a.ActiveCount(2) == 7,
              "24 voices busy, part 1 below reservation");

        const Allocator::Decision d = a.NoteOn(1, 999.0f);
        Check(d.voice == 8 && d.steal, "steal from part 0 (most over)");
        Check(a.VoiceFreq(0) == 200.0f, "part 1's only note untouched");
        Check(a.ActiveCount(1) == 2, "part 1 gained a note (still below 3)");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: allocator (reservation + stealing)\n");
    return 0;
}
