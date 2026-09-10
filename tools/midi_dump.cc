// midi_dump.cc — minimal MIDI input dump via RtMidi's CALLBACK API.
//
// Deliberately does nothing else: no output port, no layout, no polling, no
// terminal formatting between messages. The callback path bypasses RtMidi's
// 100-message input ring (MidiInApi::MidiQueue), which the polling API uses and
// which silently drops messages when the consumer runs slower than the surface
// sends. Compare this against `midi_probe monitor`: if this shows a smooth ramp
// and that shows only endpoints, the queue is overflowing.
//
// Usage: ./midi_dump   (Ctrl-C to stop)

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "RtMidi.h"

namespace {

void OnMessage(double, std::vector<unsigned char> *m, void *) {
  if (!m || m->empty()) return;
  for (unsigned char b : *m) std::printf("%02X ", b);
  if (m->size() >= 3 && (( *m )[0] & 0xF0) == 0xB0)
    std::printf("  CC %3d ch %d value %3d", (*m)[1], ((*m)[0] & 0x0F) + 1, (*m)[2]);
  std::printf("\n");
  std::fflush(stdout);
}

bool IsXtouch(const std::string &n) {
  std::string l;
  for (char c : n) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return l.find("x-touch") != std::string::npos || l.find("xtouch") != std::string::npos;
}

}  // namespace

int main() {
  try {
    auto *in = new rt::midi::RtMidiIn();
    const unsigned int n = in->getPortCount();
    int idx = -1;
    std::printf("MIDI inputs:\n");
    for (unsigned int i = 0; i < n; ++i) {
      const std::string name = in->getPortName(i);
      std::printf("  %u: %s\n", i, name.c_str());
      if (idx < 0 && IsXtouch(name)) idx = static_cast<int>(i);
    }
    if (idx < 0) { std::printf("no X-Touch found\n"); return 1; }
    in->openPort(static_cast<unsigned int>(idx));
    in->ignoreTypes(true, true, true);
    in->setCallback(&OnMessage);
    std::printf("listening on %s (callback; no queue)\n\n", in->getPortName(idx).c_str());
    for (;;) std::getchar();
  } catch (rt::midi::RtMidiError &e) {
    std::fprintf(stderr, "MIDI error: %s\n", e.what());
    return 1;
  }
}
