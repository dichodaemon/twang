// test_fb.cc — pixel-exact tests for the spike draw primitives.

#include <cstdio>

#include "fb.h"

using namespace spike;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

// A 16x16 buffer with stride 16, clip defaulting to the full frame.
static constexpr int kW = 16;
static std::uint16_t g_buf[kW * kW];
static FrameBuffer g_fb = {g_buf, kW, kW, kW, Rect{0, 0, kW, kW}};

static void Clear() {
    for (int i = 0; i < kW * kW; ++i) g_buf[i] = 0;
}

static std::uint16_t At(int x, int y) { return g_buf[y * kW + x]; }

int main() {
    const Color c = 0x1234;

    // FillRect: exact pixels inside, untouched outside.
    Clear();
    FillRect(g_fb, 2, 3, 5, 4, c);
    for (int y = 0; y < kW; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool in = x >= 2 && x < 7 && y >= 3 && y < 7;
            Check(At(x, y) == (in ? c : 0), "FillRect exact pixels");
        }
    }

    // FillRect 32-bit stores: odd offset + even offset both land correctly.
    Clear();
    FillRect(g_fb, 1, 0, 3, 1, c);  // misaligned start
    Check(At(0, 0) == 0 && At(1, 0) == c && At(2, 0) == c && At(3, 0) == c &&
              At(4, 0) == 0,
          "FillRect odd-offset row");
    Clear();
    FillRect(g_fb, 0, 0, 4, 1, c);  // aligned start
    Check(At(0, 0) == c && At(1, 0) == c && At(2, 0) == c && At(3, 0) == c &&
              At(4, 0) == 0,
          "FillRect aligned row");
    Clear();
    FillRect(g_fb, 1, 0, 4, 1, c);  // odd start, even width
    Check(At(0, 0) == 0 && At(1, 0) == c && At(4, 0) == c && At(5, 0) == 0,
          "FillRect odd-offset even-width");

    // DrawHLine / DrawVLine.
    Clear();
    DrawHLine(g_fb, 3, 5, 6, c);
    Check(At(2, 5) == 0 && At(3, 5) == c && At(8, 5) == c && At(9, 5) == 0,
          "DrawHLine run");
    Clear();
    DrawVLine(g_fb, 4, 2, 5, c);
    Check(At(4, 1) == 0 && At(4, 2) == c && At(4, 6) == c && At(4, 7) == 0,
          "DrawVLine run");

    // DrawPolyline: diagonal (0,0)->(4,4) matches the Bresenham walk.
    Clear();
    {
        const Point pts[2] = {{0, 0}, {4, 4}};
        DrawPolyline(g_fb, pts, 2, c);
    }
    for (int i = 0; i <= 4; ++i)
        Check(At(i, i) == c, "DrawPolyline diagonal");
    Check(At(1, 0) == 0 && At(0, 1) == 0, "DrawPolyline no off-diagonal");
    // Horizontal segment via polyline (dy = 0).
    Clear();
    {
        const Point pts[2] = {{2, 2}, {6, 2}};
        DrawPolyline(g_fb, pts, 2, c);
    }
    for (int x = 2; x <= 6; ++x) Check(At(x, 2) == c, "DrawPolyline horizontal");

    // SetClip: a fill beyond the clip is confined to the clip region.
    Clear();
    SetClip(g_fb, 4, 4, 4, 4);
    FillRect(g_fb, 0, 0, kW, kW, c);
    for (int y = 0; y < kW; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool in = x >= 4 && x < 8 && y >= 4 && y < 8;
            Check(At(x, y) == (in ? c : 0), "SetClip confines fill");
        }
    }

    SetClip(g_fb, 0, 0, kW, kW);  // restore the full-frame clip

    // Edge clipping: zero/negative size is a no-op.
    Clear();
    FillRect(g_fb, 1, 1, 0, 5, c);
    FillRect(g_fb, 1, 1, 5, -3, c);
    Check(At(1, 1) == 0, "zero/negative size is a no-op");

    // Edge clipping: partially off-frame fill writes only the on-frame part.
    Clear();
    FillRect(g_fb, kW - 3, kW - 3, 10, 10, c);  // 3x3 corner lands on-frame
    Check(At(kW - 1, kW - 1) == c && At(kW - 4, kW - 4) == 0,
          "FillRect clips at frame edge");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: fb primitives\n");
    return 0;
}
