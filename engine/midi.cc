#include "midi.h"

#include <cmath>

#include "params.h"

namespace engine {

const MidiBinding kXtouchCompactBindings[] = {
    {1, static_cast<std::uint8_t>(MidiMode::kAbsolute), ParamId::kAttack, 0},
    {2, static_cast<std::uint8_t>(MidiMode::kAbsolute), ParamId::kDecay, 0},
    {3, static_cast<std::uint8_t>(MidiMode::kAbsolute), ParamId::kSustain, 0},
    {4, static_cast<std::uint8_t>(MidiMode::kAbsolute), ParamId::kRelease, 0},
    {10, static_cast<std::uint8_t>(MidiMode::kRelative), ParamId::kResonance, 26},
    {11, static_cast<std::uint8_t>(MidiMode::kRelative), ParamId::kCutoff, 27},
};

const MidiLayout kXtouchCompact = {
    "X-Touch Compact", kXtouchCompactBindings, 6, 0.004f, 0,  // channel 1
};

float MidiNoteToFreq(std::uint8_t note) {
    // A4 (note 69) = 440 Hz; equal temperament.
    return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

const MidiBinding *MidiFind(const MidiLayout &layout, std::uint8_t cc) {
    for (int i = 0; i < layout.count; ++i)
        if (layout.bindings[i].cc == cc) return &layout.bindings[i];
    return nullptr;
}

void MidiCc(const MidiLayout &layout, int part, std::uint8_t cc,
            std::uint8_t value) {
    if (part < 0 || part >= kNumParts) return;
    const MidiBinding *b = MidiFind(layout, cc);
    if (!b) return;

    if (b->mode == static_cast<std::uint8_t>(MidiMode::kAbsolute)) {
        EngineSetParam(part, b->param, static_cast<float>(value) / 127.0f);
    } else {
        // Two's-complement delta: 0..63 positive, 64..127 negative.
        const float delta = (value < 64)
                                ? static_cast<float>(value)
                                : static_cast<float>(value) - 128.0f;
        float cur = EngineGetParam(part, b->param) + delta * layout.rel_step;
        if (cur < 0.0f) cur = 0.0f;
        if (cur > 1.0f) cur = 1.0f;
        EngineSetParam(part, b->param, cur);
    }
}

void MidiNoteOn(int part, std::uint8_t note) {
    if (part < 0 || part >= kNumParts) return;
    EngineNoteOn(part, MidiNoteToFreq(note));
}

void MidiNoteOff(int part, std::uint8_t note) {
    if (part < 0 || part >= kNumParts) return;
    EngineNoteOff(part, MidiNoteToFreq(note));
}

void MidiMessage(const MidiLayout &layout, int part, std::uint8_t status,
                 std::uint8_t d1, std::uint8_t d2) {
    if ((status & 0x0F) != layout.channel) return;  // wrong channel
    switch (status & 0xF0) {
    case 0xB0:  // Control Change
        MidiCc(layout, part, d1, d2);
        break;
    case 0x90:  // Note On (velocity 0 = note off)
        if (d2 == 0) MidiNoteOff(part, d1);
        else MidiNoteOn(part, d1);
        break;
    case 0x80:  // Note Off
        MidiNoteOff(part, d1);
        break;
    default:
        break;  // ignore other message types
    }
}

}  // namespace engine
