// test_font.cc — pixel-exact tests for DrawGlyphRun.
//
// The key case is the 10-bit-in-16-bit left-alignment: an 8-wide atlas fills
// its byte exactly, so a wrong unpacking is silent there; the 10-wide atlas
// leaves the low 6 bits empty, so '#', with bars at columns 2 and 7, pins the
// alignment.

#include <cstdio>

#include "font.h"

using namespace spike;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static constexpr int kW = 40;
static constexpr int kH = 24;
static std::uint16_t g_buf[kW * kH];
static FrameBuffer g_fb = {g_buf, kW, kH, kW, Rect{0, 0, kW, kH}};

static void Clear() {
    for (int i = 0; i < kW * kH; ++i) g_buf[i] = 0;
}

static std::uint16_t At(int x, int y) { return g_buf[y * kW + x]; }

int main() {
    const Color c = 0x00FF;

    // '#' (numbersign): vertical bars at columns 2 and 7, horizontal bars at
    // rows 7/11 spanning columns 1-7. Pins the left-alignment.
    Clear();
    DrawGlyphRun(g_fb, 0, 0, "#", 1, kPrimaryFont, c, 0);
    Check(At(2, 3) == c && At(6, 3) == c, "# bars at columns 2 and 6");
    Check(At(0, 3) == 0 && At(1, 3) == 0 && At(8, 3) == 0 && At(9, 3) == 0,
          "# bars stay inside the cell");
    Check(At(1, 7) == c && At(7, 7) == c, "# crossbar spans columns 1..7");
    Check(At(0, 7) == 0 && At(8, 7) == 0, "# crossbar stays inside");

    // '!' (exclam): bar at column 4 rows 3-11, dot at rows 14-15.
    Clear();
    DrawGlyphRun(g_fb, 0, 0, "!", 1, kPrimaryFont, c, 0);
    Check(At(4, 3) == c && At(4, 11) == c, "! bar spans rows 3..11");
    Check(At(4, 12) == 0 && At(4, 13) == 0, "! gap at rows 12-13");
    Check(At(4, 14) == c, "! dot at row 14");

    // Middle dot (0xB7) maps to the extra glyph: dots at rows 9-10.
    Clear();
    {
        const char dot[1] = {static_cast<char>(0xB7)};
        DrawGlyphRun(g_fb, 0, 0, dot, 1, kPrimaryFont, c, 0);
    }
    Check(At(4, 9) == c && At(4, 10) == c, "middle-dot glyph at index 95");

    // Fallback: an out-of-range byte renders as space (nothing drawn).
    Clear();
    {
        const char bad[1] = {static_cast<char>(0x01)};
        DrawGlyphRun(g_fb, 0, 0, bad, 1, kPrimaryFont, c, 0);
    }
    {
        bool any = false;
        for (int y = 0; y < kH && !any; ++y)
            for (int x = 0; x < kW && !any; ++x) any = At(x, y) != 0;
        Check(!any, "out-of-range byte renders as space");
    }

    // Tracking: "!!" with tracking 2 advances 12 px, second bar at column 16.
    Clear();
    DrawGlyphRun(g_fb, 0, 0, "!!", 2, kPrimaryFont, c, 2);
    Check(At(4, 3) == c && At(16, 3) == c, "tracking: second ! at column 16");
    Check(At(10, 3) == 0 && At(11, 3) == 0, "tracking: gap between glyphs");

    // Clipping: a glyph overlapping a small clip only draws the in-clip part.
    Clear();
    SetClip(g_fb, 3, 3, 3, 3);  // x in [3,6), y in [3,6)
    DrawGlyphRun(g_fb, 0, 0, "!", 1, kPrimaryFont, c, 0);
    Check(At(4, 3) == c && At(4, 5) == c, "font clip keeps in-clip pixels");
    Check(At(4, 6) == 0, "font clip drops out-of-clip pixel");
    SetClip(g_fb, 0, 0, kW, kH);

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: font\n");
    return 0;
}
