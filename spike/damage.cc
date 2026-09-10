// spike/damage.cc — damage accumulation and the two-frame repaint union.

#include "spike/damage.h"

#include <algorithm>

namespace spike {
namespace {

bool Overlaps(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
         b.y < a.y + a.h;
}

Rect Union(const Rect &a, const Rect &b) {
  const int x0 = std::min(a.x, b.x);
  const int y0 = std::min(a.y, b.y);
  const int x1 = std::max(a.x + a.w, b.x + b.w);
  const int y1 = std::max(a.y + a.h, b.y + b.h);
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

// Merges overlapping rects in place, returning the new count.
int MergeInPlace(Rect *r, int n) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        if (Overlaps(r[i], r[j])) {
          r[i] = Union(r[i], r[j]);
          r[j] = r[n - 1];
          --n;
          changed = true;
          break;
        }
      }
      if (changed) break;
    }
  }
  return n;
}

}  // namespace

Damage::Damage(int w, int h) : w_(w), h_(h) {}

void Damage::Add(const Rect &r) { Add(r.x, r.y, r.w, r.h); }

void Damage::Add(int x, int y, int w, int h) {
  // Clip to the frame; empty regions are ignored.
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, w_);
  const int y1 = std::min(y + h, h_);
  if (x1 <= x0 || y1 <= y0) return;

  if (collapsed_) return;  // whole frame already damaged

  const Rect c{x0, y0, x1 - x0, y1 - y0};

  // Merge into an overlapping region if one exists.
  for (int i = 0; i < count_; ++i) {
    if (Overlaps(c, rects_[i])) {
      rects_[i] = Union(c, rects_[i]);
      count_ = MergeInPlace(rects_, count_);  // the union may bridge others
      return;
    }
  }

  // No overlap: append, or collapse when out of capacity.
  if (count_ >= kMaxRects) {
    rects_[0] = Rect{0, 0, w_, h_};
    count_ = 1;
    collapsed_ = true;
    return;
  }
  rects_[count_++] = c;
}

int Damage::Repaint() {
  // damage[n] ∪ damage[n−1], merged.
  int n = 0;
  for (int i = 0; i < count_; ++i) repaint_[n++] = rects_[i];
  for (int i = 0; i < prev_count_; ++i) repaint_[n++] = prev_[i];
  n = MergeInPlace(repaint_, n);
  repaint_count_ = n;

  // Roll: current becomes previous, current clears.
  prev_count_ = count_;
  for (int i = 0; i < count_; ++i) prev_[i] = rects_[i];
  count_ = 0;
  collapsed_ = false;

  return n;
}

}  // namespace spike
