// test_midi_ring.cc — the two MIDI rings and the UMP word classifier.
//
// IsNoteWord splits the raw UMP word stream into the note ring (notes + CC 123)
// and the CC ring (everything else). Tests the classifier's boundaries and the
// rings' SPSC FIFO + full-drop behaviour.

#include <cstdint>
#include <cstdio>

#include "midi_ring.h"

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

// Build a MIDI 1.0 channel-voice UMP word (MT=2, group 0): status in bits
// 23-16, data1 in 15-8, data2 in 7-0.
static std::uint32_t VoiceWord(std::uint32_t status, std::uint32_t d1,
                               std::uint32_t d2) {
    return (0x2u << 28) | ((status & 0xFF) << 16) | ((d1 & 0x7F) << 8) |
           (d2 & 0x7F);
}

int main() {
    // Classifier: notes and CC 123 are note words; other CCs / SysEx / MIDI 2.0
    // are not.
    Check(IsNoteWord(VoiceWord(0x90, 60, 100)), "note-on is a note word");
    Check(IsNoteWord(VoiceWord(0x80, 60, 0)), "note-off is a note word");
    Check(IsNoteWord(VoiceWord(0xB0, 123, 0)),
          "CC 123 (All Notes Off) is a note word");
    Check(!IsNoteWord(VoiceWord(0xB0, 7, 127)), "CC 7 is not a note word");
    Check(!IsNoteWord(VoiceWord(0xE0, 0, 64)), "pitch bend is not a note word");
    Check(!IsNoteWord(0x3u << 28), "SysEx (MT=3) is not a note word");
    Check(!IsNoteWord(0x4u << 28), "MIDI 2.0 (MT=4) is not a note word");
    // Group filter: a note-on on a non-zero group is not ours (HandleMidiWord
    // rejects it as group_reject), so it must not consume a note-ring slot.
    Check(!IsNoteWord((0x2u << 28) | (0x3u << 24) | (0x90u << 16) |
                      (60u << 8) | 100u),
          "note-on on group 3 is not a note word");

    // Note ring: FIFO order + full-drop.
    {
        NoteRing n;
        n.Reset();
        std::uint32_t w;
        Check(!n.Pop(&w), "empty note ring pops nothing");
        const int cap = static_cast<int>(NoteRing::kCapacity);
        for (int i = 0; i < cap; ++i) {
            Check(n.Push(static_cast<std::uint32_t>(i)), "note ring push fits");
        }
        Check(!n.Push(0xFFFFFFFF), "full note ring rejects");
        for (int i = 0; i < cap; ++i) {
            Check(n.Pop(&w) && w == static_cast<std::uint32_t>(i),
                  "note ring FIFO order preserved");
        }
        Check(!n.Pop(&w), "note ring drained");
        n.Reset();
        Check(!n.Pop(&w), "note ring reset clears");
    }

    // CC ring: larger capacity, still a FIFO with full-drop.
    {
        CcRing c;
        c.Reset();
        std::uint32_t w;
        Check(CcRing::kCapacity == 1024, "CC ring is 1024 words");
        const int cap = static_cast<int>(CcRing::kCapacity);
        for (int i = 0; i < cap; ++i) {
            Check(c.Push(static_cast<std::uint32_t>(i)), "cc ring push fits");
        }
        Check(!c.Push(0xFFFFFFFF), "full cc ring rejects");
        for (int i = 0; i < cap; ++i) {
            Check(c.Pop(&w) && w == static_cast<std::uint32_t>(i),
                  "cc ring FIFO order preserved");
        }
        Check(!c.Pop(&w), "cc ring drained");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: midi ring\n");
    return 0;
}
