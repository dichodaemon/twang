// test_damage.cc — merge, overflow fallback, and the two-frame repaint rule.

#include <cstdio>

#include "damage.h"

using namespace spike;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static bool HasRect(const Damage &d, int x, int y, int w, int h) {
    for (int i = 0; i < d.Count(); ++i) {
        const Rect &r = d.Rects()[i];
        if (r.x == x && r.y == y && r.w == w && r.h == h) return true;
    }
    return false;
}

int main() {
    // Merge-on-overlap.
    {
        Damage d(100, 100);
        d.Add(0, 0, 10, 10);
        d.Add(5, 5, 10, 10);    // overlaps the first
        d.Add(50, 50, 10, 10);  // disjoint
        const int n = d.Repaint();
        Check(n == 2, "merge: two rects survive");
        Check(HasRect(d, 0, 0, 15, 15), "merge: overlapping pair unioned");
        Check(HasRect(d, 50, 50, 10, 10), "merge: disjoint rect kept");
    }

    // Bridging cascade: a new rect overlapping two disjoint rects merges all
    // three (exercises MergeInPlace's worst case, not just the pairwise union).
    {
        Damage d(100, 100);
        d.Add(0, 0, 10, 10);    // A
        d.Add(30, 0, 10, 10);   // B (disjoint from A)
        d.Add(5, 0, 30, 10);    // C overlaps both A and B, bridging them
        const int n = d.Repaint();
        Check(n == 1, "bridging: three rects collapse to one");
        Check(HasRect(d, 0, 0, 40, 10), "bridging: union spans all three");
    }

    // Whole-frame fallback on overflow.
    {
        Damage d(100, 100);
        for (int i = 0; i < Damage::kMaxRects + 1; ++i) d.Add(i * 5, 0, 2, 2);
        const int n = d.Repaint();
        Check(n == 1, "overflow: collapses to one rect");
        Check(HasRect(d, 0, 0, 100, 100), "overflow: rect is the whole frame");
    }

    // Two-frame union: a region damaged in frame n repaints in n and n+1.
    {
        Damage d(100, 100);
        d.Add(0, 0, 10, 10);
        Check(d.Repaint() == 1 && HasRect(d, 0, 0, 10, 10),
              "two-frame: frame 1 repaints");
        Check(d.Repaint() == 1 && HasRect(d, 0, 0, 10, 10),
              "two-frame: frame 2 keeps the region alive");
        Check(d.Repaint() == 0, "two-frame: frame 3 expires the region");
    }

    // Add clips to the frame bounds.
    {
        Damage d(100, 100);
        d.Add(-5, -5, 10, 10);
        d.Repaint();
        Check(d.Count() == 1 && HasRect(d, 0, 0, 5, 5), "clip: rect clipped to frame");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: damage\n");
    return 0;
}
