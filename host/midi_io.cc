#include "midi_io.h"

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine.h"
#include "engine_control.h"
#include "interaction.h"
#include "midi.h"
#include "pages.h"
#include "panel.h"
#include "surface.h"

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

// The X-Touch Compact speaks on MIDI channel 1 (status low nibble 0).
constexpr std::uint8_t kChannel = 0;

// CC number → mapping entry, or nullptr if the surface does not map it.
const nostromo::ControlMap *FindControl(const nostromo::SurfaceProfile &surface,
                                        std::uint8_t cc) {
    for (std::uint8_t i = 0; i < surface.n_map; ++i)
        if (surface.map[i].physical == cc) return &surface.map[i];
    return nullptr;
}

// Monotonic milliseconds for InputEvent::t_ms (the gesture recognizer's clock).
std::uint32_t NowMs() {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
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

void MidiIo::Poll(nostromo::Panel *panel, nostromo::Interaction *interaction) {
    if (!in) return;
    const nostromo::SurfaceProfile &surface = nostromo::Surface();
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
        if ((status & 0x0F) != kChannel) continue;  // wrong channel
        switch (status & 0xF0) {
        case 0xB0: {  // Control Change → logical control via the surface map
            const nostromo::ControlMap *m = FindControl(surface, msg[1]);
            if (!m) break;  // unmapped CC (faders and other surplus controls)
            nostromo::InputEvent ev{};
            ev.control = m->logical;
            ev.t_ms = NowMs();
            if (m->turn) {
                ev.detents = static_cast<std::int8_t>(
                    nostromo::DecodeEnc(msg[2], m->enc));
                ev.edge = nostromo::Edge::kNone;
            } else {
                ev.detents = 0;
                ev.edge = (msg[2] > 0) ? nostromo::Edge::kDown
                                       : nostromo::Edge::kUp;
            }
            interaction->OnInput(ev);
            break;
        }
        case 0x90:  // Note On (velocity 0 = note off)
            if (msg[2] == 0) nostromo::PanelNoteOff(panel, engine::MidiNoteToFreq(msg[1]));
            else nostromo::PanelNoteOn(panel, engine::MidiNoteToFreq(msg[1]), msg[2]);
            break;
        case 0x80:  // Note Off
            nostromo::PanelNoteOff(panel, engine::MidiNoteToFreq(msg[1]));
            break;
        default:
            break;
        }
    }
}

void MidiIo::Feedback() {
    if (!out || !control || !interaction) return;
    const nostromo::SurfaceProfile &surface = nostromo::Surface();
    const nostromo::NavState &nav = interaction->Nav();
    for (int i = 0; i < surface.n_map; ++i) {
        const nostromo::ControlMap &m = surface.map[i];
        if (!m.turn) continue;  // buttons have no ring
        const nostromo::Binding b = nostromo::ResolveBinding(nav, m.logical);
        // Only a resolved parameter drives a ring; a pending/route-field/
        // view-control/off-the-end column is dark.
        float v = 0.0f;
        if (b.kind == nostromo::BindKind::kParam)
            v = control->GetParam(nav.part, b.param);
        const int out_cc = static_cast<int>(m.physical);
        const int out_val = static_cast<int>(v * 127.0f + 0.5f);
        if (last_sent[out_cc] == out_val) continue;
        last_sent[out_cc] = out_val;
        std::vector<unsigned char> msg = {
            static_cast<unsigned char>(0xB0 | kChannel),
            static_cast<unsigned char>(out_cc),
            static_cast<unsigned char>(out_val),
        };
        if (trace)
            std::fprintf(stderr, "midi -> B%d %02X %02X\n", kChannel, out_cc,
                         out_val);
        out->sendMessage(&msg);
    }
}
