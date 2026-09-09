#include "midi_io.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "RtMidi.h"

#include "engine.h"
#include "midi.h"
#include "ui.h"

namespace {

rt::midi::RtMidiIn *g_midi_in = nullptr;
rt::midi::RtMidiOut *g_midi_out = nullptr;
int g_last_sent[128];  // per feedback CC; -1 = never sent

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

void midi_init() {
    for (int i = 0; i < 128; ++i) g_last_sent[i] = -1;
    try {
        g_midi_in = new rt::midi::RtMidiIn();
        const unsigned int in_n = g_midi_in->getPortCount();
        const int in = FindXtouch(g_midi_in, in_n);
        if (in < 0) {
            std::fprintf(stderr, "sim: X-Touch Compact not found; available MIDI inputs:\n");
            for (unsigned int i = 0; i < in_n; ++i)
                std::fprintf(stderr, "    %u: %s\n", i, g_midi_in->getPortName(i).c_str());
            delete g_midi_in;
            g_midi_in = nullptr;
            return;
        }
        g_midi_in->openPort(static_cast<unsigned int>(in));
        std::fprintf(stderr, "sim: MIDI input: %s\n", g_midi_in->getPortName(in).c_str());

        g_midi_out = new rt::midi::RtMidiOut();
        const unsigned int out_n = g_midi_out->getPortCount();
        const int out = FindXtouch(g_midi_out, out_n);
        if (out >= 0) {
            g_midi_out->openPort(static_cast<unsigned int>(out));
            std::fprintf(stderr, "sim: MIDI output: %s\n", g_midi_out->getPortName(out).c_str());
        } else {
            std::fprintf(stderr, "sim: no X-Touch MIDI output (no LED feedback)\n");
            delete g_midi_out;
            g_midi_out = nullptr;
        }
    } catch (rt::midi::RtMidiError &e) {
        std::fprintf(stderr, "sim: MIDI init failed: %s\n", e.what());
        delete g_midi_in;
        delete g_midi_out;
        g_midi_in = nullptr;
        g_midi_out = nullptr;
    }
}

void midi_poll() {
    if (!g_midi_in) return;
    const engine::MidiLayout &layout = engine::kXtouchCompact;
    std::vector<unsigned char> msg;
    for (;;) {
        msg.clear();
        g_midi_in->getMessage(&msg);
        if (msg.empty()) break;
        if (msg.size() < 3) continue;
        const std::uint8_t status = msg[0];
        if ((status & 0x0F) != layout.channel) continue;  // wrong channel
        switch (status & 0xF0) {
        case 0xB0:  // Control Change
            engine::MidiCc(layout, 0, msg[1], msg[2]);
            break;
        case 0x90:  // Note On (velocity 0 = note off)
            if (msg[2] == 0) ui_note_off(engine::MidiNoteToFreq(msg[1]));
            else ui_note_on(engine::MidiNoteToFreq(msg[1]));
            break;
        case 0x80:  // Note Off
            ui_note_off(engine::MidiNoteToFreq(msg[1]));
            break;
        default:
            break;
        }
    }
}

void midi_feedback() {
    if (!g_midi_out) return;
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
            out_val = static_cast<int>(v * 13.0f + 0.5f);   // LED ring (14 levels)
        }
        if (g_last_sent[out_cc] == out_val) continue;
        g_last_sent[out_cc] = out_val;
        std::vector<unsigned char> msg = {
            static_cast<unsigned char>(0xB0 | layout.channel),
            static_cast<unsigned char>(out_cc),
            static_cast<unsigned char>(out_val),
        };
        g_midi_out->sendMessage(&msg);
    }
}
