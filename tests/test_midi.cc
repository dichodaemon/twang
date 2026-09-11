#include <cmath>
#include <cstdio>

#include "engine.h"
#include "midi.h"
#include "params.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    EngineInit();

    // Absolute: CC 1 -> attack, value 64 -> 64/127.
    MidiCc(kXtouchCompact, 0, 1, 64);
    Check(Near(EngineGetParam(0, ParamId::kAttack), 64.0f / 127.0f),
          "absolute CC 1 -> attack 64/127");

    // Absolute bounds.
    MidiCc(kXtouchCompact, 0, 1, 0);
    Check(EngineGetParam(0, ParamId::kAttack) == 0.0f, "absolute CC 1 -> 0");
    MidiCc(kXtouchCompact, 0, 1, 127);
    Check(EngineGetParam(0, ParamId::kAttack) == 1.0f, "absolute CC 1 -> 1");

    // Relative: CC 11 -> cutoff, two's-complement delta, step 0.004.
    EngineSetParam(0, ParamId::kCutoff, 0.5f);
    MidiCc(kXtouchCompact, 0, 11, 1);  // +1 detent
    Check(Near(EngineGetParam(0, ParamId::kCutoff), 0.504f), "relative +1 -> +0.004");
    MidiCc(kXtouchCompact, 0, 11, 127);  // -1 detent
    Check(Near(EngineGetParam(0, ParamId::kCutoff), 0.5f), "relative -1 -> -0.004");
    MidiCc(kXtouchCompact, 0, 11, 64);  // 64 -> -64 detents
    Check(Near(EngineGetParam(0, ParamId::kCutoff), 0.244f), "relative 64 -> -0.256");

    // Relative clamps at bounds.
    EngineSetParam(0, ParamId::kCutoff, 0.999f);
    MidiCc(kXtouchCompact, 0, 11, 10);  // +10 detents
    Check(EngineGetParam(0, ParamId::kCutoff) == 1.0f, "relative clamps high");
    EngineSetParam(0, ParamId::kCutoff, 0.001f);
    MidiCc(kXtouchCompact, 0, 11, 118);  // -10 detents
    Check(EngineGetParam(0, ParamId::kCutoff) == 0.0f, "relative clamps low");

    // Unmapped CC is ignored.
    EngineSetParam(0, ParamId::kAttack, 0.25f);
    MidiCc(kXtouchCompact, 0, 200, 64);
    Check(EngineGetParam(0, ParamId::kAttack) == 0.25f, "unmapped CC ignored");

    // Swappable layout: a different controller maps differently.
    const MidiBinding custom[] = {
        {20, static_cast<std::uint8_t>(MidiMode::kAbsolute), ParamId::kCutoff, 0},
    };
    const MidiLayout custom_layout = {"test", custom, 1, 0.01f, 0};
    MidiCc(custom_layout, 0, 20, 127);
    Check(EngineGetParam(0, ParamId::kCutoff) == 1.0f, "custom layout CC 20 -> cutoff");
    MidiCc(kXtouchCompact, 0, 20, 127);  // CC 20 not in X-Touch
    Check(EngineGetParam(0, ParamId::kCutoff) == 1.0f, "X-Touch layout ignores CC 20");

    // Channel filter: only layout.channel (0 = MIDI channel 1) is accepted.
    EngineSetParam(0, ParamId::kSustain, 0.5f);
    MidiMessage(kXtouchCompact, 0, 0xB1, 3, 100);  // CC 3 on channel 2
    Check(EngineGetParam(0, ParamId::kSustain) == 0.5f, "CC on channel 2 ignored");

    // Note -> frequency.
    Check(Near(MidiNoteToFreq(69), 440.0f), "note 69 -> 440 Hz");
    Check(std::fabs(MidiNoteToFreq(60) - 261.6256f) < 1e-3f, "note 60 -> 261.63 Hz (C4)");

    // Dispatch: CC routes to the parameter, Note On produces audio.
    EngineInit();
    EngineSetParamDisp(0, ParamId::kAttack, 0.01f);  // fast attack for the note
    MidiMessage(kXtouchCompact, 0, 0xB0, 3, 100);    // CC 3 -> sustain
    Check(Near(EngineGetParam(0, ParamId::kSustain), 100.0f / 127.0f),
          "dispatch CC 3 -> sustain");
    MidiMessage(kXtouchCompact, 0, 0x90, 69, 100);  // Note On A4
    float buf[kBlockSize];
    Render(buf, kBlockSize);
    float peak = 0.0f;
    for (int i = 0; i < kBlockSize; ++i)
        if (std::fabs(buf[i]) > peak) peak = std::fabs(buf[i]);
    Check(peak > 0.001f, "dispatch Note On produces audio");

    // Pitchbend (0xE0) -> kPitchBend: 14-bit bend (d1 | d2<<7), center 0x2000.
    MidiMessage(kXtouchCompact, 0, 0xE0, 0x00, 0x00);  // bend = 0 (full down)
    Check(EngineGetParam(0, ParamId::kPitchBend) == 0.0f,
          "pitchbend full down -> 0");
    MidiMessage(kXtouchCompact, 0, 0xE0, 0x00, 0x40);  // bend = 0x2000 (center)
    Check(Near(EngineGetParam(0, ParamId::kPitchBend), 0.5f),
          "pitchbend center -> 0.5");
    MidiMessage(kXtouchCompact, 0, 0xE0, 0x7F, 0x7F);  // bend = 16383 (full up)
    Check(EngineGetParam(0, ParamId::kPitchBend) == 1.0f,
          "pitchbend full up -> 1");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: midi mapping\n");
    return 0;
}
