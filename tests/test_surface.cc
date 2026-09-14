// test_surface.cc — surface-profile injectivity and encoder decode round-trips.
//
// A mis-decode is expensive to find by feel (a binary-offset encoder read as
// signed-bit turns clockwise into -1 and anticlockwise into +63), so the
// round-trip is a test, not a manual check (arch-design §7.9). The ControlMap
// must be injective in both directions: a prototype whose table forgets a
// control should fail a test, not a session.

#include <cstdio>

#include "interaction.h"
#include "surface.h"

using namespace nostromo;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

int main() {
    // 1. The active profile's ControlMap is injective in both directions.
    {
        const SurfaceProfile &sp = Surface();
        for (int i = 0; i < sp.n_map; ++i) {
            for (int j = i + 1; j < sp.n_map; ++j) {
                Check(sp.map[i].physical != sp.map[j].physical,
                      "physical CCs are unique");
                Check(sp.map[i].logical != sp.map[j].logical,
                      "logical controls are unique");
            }
        }
    }

    // 2. DecodeEnc round-trips each scheme, and clamps wraparound/aggregate
    //    bytes to ±1 so a single event cannot jump a parameter end-to-end.
    {
        struct Case { EncEncoding enc; std::uint8_t raw; int want; };
        const Case kCases[] = {
            // signed-bit: bit 6 = sign, bits 0-5 = magnitude
            {EncEncoding::kSignedBit, 0x01, +1},
            {EncEncoding::kSignedBit, 0x41, -1},
            {EncEncoding::kSignedBit, 0x7F, -1},  // magnitude 63 clamped
            {EncEncoding::kSignedBit, 0x3F, +1},  // magnitude 63 clamped
            {EncEncoding::kSignedBit, 0x00, 0},
            {EncEncoding::kSignedBit, 0x40, 0},   // sign set, magnitude 0
            // two's complement: 7-bit signed
            {EncEncoding::kTwosComplement, 0x01, +1},
            {EncEncoding::kTwosComplement, 0x7F, -1},
            {EncEncoding::kTwosComplement, 0x41, -1},  // -63 clamped
            {EncEncoding::kTwosComplement, 0x00, 0},
            {EncEncoding::kTwosComplement, 0x40, -1},  // -64 clamped
            // binary offset: center 0x40, offset wraps
            {EncEncoding::kBinaryOffset, 0x41, +1},
            {EncEncoding::kBinaryOffset, 0x3F, -1},
            {EncEncoding::kBinaryOffset, 0x00, -1},  // -64 clamped
            {EncEncoding::kBinaryOffset, 0x7F, +1},  // +63 clamped
            {EncEncoding::kBinaryOffset, 0x40, 0},
        };
        for (const Case &c : kCases) {
            const int got = DecodeEnc(c.raw, c.enc);
            Check(got == c.want, "DecodeEnc round-trips");
        }
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: surface\n");
    return 0;
}
