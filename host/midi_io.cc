#include "midi_io.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine.h"
#include "midi.h"
#include "panel.h"

namespace {

// Case-insensitive "x-touch" / "xtouch" match (the mockup's /x-?touch/i).
bool IsXtouch(const std::string &name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("x-touch") != std::string::npos ||
           lower.find("xtouch") != std::string::npos;
}

// First port whose name matches the X-Touch, or -1.
int FindXtouch(rt::midi::RtMidi *midi, unsigned int count) {
    for (unsigned int i = 0; i < count; ++i)
        if (IsXtouch(midi->getPortName(i))) return static_cast<int>(i);
    return -1;
}

}  // namespace

void MidiIo::Init() {
    for (int i = 0; i < 128; ++i) last_sent[i] = -1;
    const char *t = std::getenv("TWANG_MIDI_TRACE");
    trace = (t != nullptr && t[0] == '1');
    try {
        in = new rt::midi::RtMidiIn();
        const unsigned int in_n = in->getPortCount();
        const int idx = FindXtouch(in, in_n);
        if (idx < 0) {
            std::fprintf(stderr, "host: X-Touch Compact not found; available MIDI inputs:\n");
            for (unsigned int i = 0; i < in_n; ++i)
                std::fprintf(stderr, "    %u: %s\n", i, in->getPortName(i).c_str());
            delete in;
            in = nullptr;
            return;
        }
        in->openPort(static_cast<unsigned int>(idx));
        std::fprintf(stderr, "host: MIDI input: %s\n", in->getPortName(idx).c_str());

        out = new rt::midi::RtMidiOut();
        const unsigned int out_n = out->getPortCount();
        const int oidx = FindXtouch(out, out_n);
        if (oidx >= 0) {
            out->openPort(static_cast<unsigned int>(oidx));
            std::fprintf(stderr, "host: MIDI output: %s\n", out->getPortName(oidx).c_str());
        } else {
            std::fprintf(stderr, "host: no X-Touch MIDI output (no LED feedback)\n");
            delete out;
            out = nullptr;
        }
    } catch (rt::midi::RtMidiError &e) {
        std::fprintf(stderr, "host: MIDI init failed: %s\n", e.what());
        delete in;
        delete out;
        in = nullptr;
        out = nullptr;
    }
}

void MidiIo::Poll(spike::Panel *panel) {
    if (!in) return;
    const engine::MidiLayout &layout = engine::kXtouchCompact;
    std::vector<unsigned char> msg;
    for (;;) {
        msg.clear();
        in->getMessage(&msg);
        if (msg.empty()) break;
        if (msg.size() < 3) continue;
        const std::uint8_t status = msg[0];
        if (trace) {
            std::fprintf(stderr, "midi <-");
            for (unsigned char b : msg) std::fprintf(stderr, " %02X", b);
            std::fprintf(stderr, "\n");
        }
        if ((status & 0x0F) != layout.channel) continue;  // wrong channel
        switch (status & 0xF0) {
        case 0xB0:  // Control Change
            engine::MidiCc(layout, 0, msg[1], msg[2]);
            break;
        case 0x90:  // Note On (velocity 0 = note off)
            if (msg[2] == 0) spike::PanelNoteOff(panel, engine::MidiNoteToFreq(msg[1]));
            else spike::PanelNoteOn(panel, engine::MidiNoteToFreq(msg[1]));
            break;
        case 0x80:  // Note Off
            spike::PanelNoteOff(panel, engine::MidiNoteToFreq(msg[1]));
            break;
        default:
            break;
        }
    }
}

void MidiIo::Feedback() {
    if (!out) return;
    const engine::MidiLayout &layout = engine::kXtouchCompact;
    for (int i = 0; i < layout.count; ++i) {
        const engine::MidiBinding &b = layout.bindings[i];
        const float v = engine::EngineGetParam(0, b.param);
        int out_cc, out_val;
        if (b.mode == static_cast<std::uint8_t>(engine::MidiMode::kAbsolute)) {
            out_cc = b.cc;
            out_val = static_cast<int>(v * 127.0f + 0.5f);  // fader position
        } else {
            out_cc = b.ring_cc;
            // The surface maps 0..127 across the ring's 13 segments itself;
            // sending 0..13 lit only the first tenth of it. norm 0 still sends
            // 0 (dark ring), so a true zero is preserved.
            out_val = static_cast<int>(v * 127.0f + 0.5f);  // LED ring
        }
        if (last_sent[out_cc] == out_val) continue;
        last_sent[out_cc] = out_val;
        std::vector<unsigned char> msg = {
            static_cast<unsigned char>(0xB0 | layout.channel),
            static_cast<unsigned char>(out_cc),
            static_cast<unsigned char>(out_val),
        };
        if (trace)
            std::fprintf(stderr, "midi -> B%d %02X %02X\n", layout.channel,
                         out_cc, out_val);
        out->sendMessage(&msg);
    }
}
