// nostromo/panel.cc — the Nostromo controller panel (double-buffered).
//
// Draws the signal-flow layout (titlebar, four modules, keyboard, nav) with
// the framebuffer primitives, and drives the four dynamic plot regions from
// the cached parameter state and the audio-tap scope ring.
//
// The panel is buffer-agnostic: the backend owns two framebuffers and swaps
// them each frame, so the panel draws static chrome once per buffer and keeps
// per-buffer column traces (TraceState[2] per plot) for the column-update
// redraw. Plots redraw only when their invalidation flag is set — never on a
// global timer — and update only changed columns, erasing the old vertical
// span graticule-aware.

#include "panel.h"

#include "palette.h"
#include "screens.h"

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>  // k_uptime_get_32 (monotonic ms; no gettimeofday)
#else
#include <chrono>
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "damage.h"
#include "engine.h"
#include "fft.h"
#include "font.h"
#include "interaction.h"
#include "pages.h"
#include "params.h"
#include "scope_ring.h"

namespace nostromo {

using namespace spike;

// ---- frame / layout constants (1024x600, per the Nostromo mockup) ----

constexpr int kFrameW = 1024;
constexpr int kFrameH = 600;

constexpr int kCycleBufSize = 4096;
constexpr int kFftSize = 8192;

// Column-trace sentinel: a column with y0 == y1 == kEmpty has no curve. The
// trace element is uint16_t (kPlotH = 404 > 255), so the sentinel is 0xFFFF
// rather than 0xFF.
constexpr std::uint16_t kEmpty = 0xFFFF;

// ---- Panel struct ----

struct Panel {
  // Cached engine state + transients (control thread). The parameter defaults
  // mirror engine/params.cc's g_params so the panel renders the engine's
  // initial state before any input (the per-frame poll then keeps them in
  // sync).
  float freq = 440.0f;
  float cutoff = 1.0f;
  float resonance = 0.0f;
  float attack = 0.25f;
  float decay = 0.6f;
  float sustain = 0.7f;
  float release = 0.6f;
  bool note_on = false;
  std::uint32_t note_at = 0;
  std::uint32_t release_at = 0;
  float release_from = 0.0f;
  float phase = 0.0f;
  float scope_peak = 0.0f;

  // Audio tap + draw scratch (owned here, never heap-allocated in the draw).
  ScopeRing scope_ring;
  float cycle_buf[kCycleBufSize];
  float fft_re[kFftSize];
  float fft_im[kFftSize];
  float fft_mag[kFftSize / 2];

  // Damage + redraw state.
  Damage damage{kFrameW, kFrameH};

  // Double buffering: the buffer we draw into this frame, set by the backend
  // via PanelDraw's buffer_index argument (the backend owns the swap — no
  // independent toggle to desync). chrome_drawn[k] records whether static
  // chrome (titlebar, frames, graticule, keyboard, tabs) is in buffer k.
  int fb_index = 0;
  bool chrome_drawn[2] = {false, false};

  // Per-plot, per-buffer column trace (two copies — one per buffer).
  TraceState traces[4][2];

  // The dynamic regions: four plots (slots 0-3) plus the output module's
  // mode buttons (slot 4). Rects are filled by the screen descriptor on the
  // first chrome draw; the hooks/state are set in PanelCreate.
  DynRegion dyn[kNumSlots];

  // Pending-buffer redraw count per plot: a plot invalidated in frame n must
  // be repainted into BOTH buffers (n's back buffer and n+1's), so it stays
  // pending for two frames.
  int pending[4] = {0, 0, 0, 0};

  // Drag state: -1 none, 0/1/2 env attack/decay/release.
  int drag_handle = -1;
  bool filter_drag = false;

  // Scope invalidation: set by the audio thread (PanelAudioTap), drained by
  // the control thread (PanelDraw) to redraw the output plot — the one plot
  // that animates in steady state. Relaxed ordering: the ring's own
  // synchronization orders the samples; this flag is only a redraw hint.
  std::atomic<bool> scope_dirty{false};

  // Column-update scratch: per-column lower/upper span (one entry per plot
  // column, <= kPlotW). lo == -1 marks an empty column.
  int col_lo[geom::kPlotW];
  int col_hi[geom::kPlotW];

  // Previous overlay-element rects (plot-local), per buffer, so moving cursor/
  // handles/playhead erase their old position before redrawing. Zero rects
  // mean "nothing drawn last frame".
  Rect filter_cursor[2] = {};
  Rect env_handles[3][2] = {};
  Rect env_playhead[2] = {};
  bool env_playhead_on[2] = {false, false};

  // Test/debug: draw-call counts per plot.
  int draw_counts[4] = {0, 0, 0, 0};
};

// ---- time ----

std::uint32_t NowMs() {
#if defined(__ZEPHYR__)
  // Monotonic milliseconds since boot. std::chrono's libstdc++ clock pulls
  // in gettimeofday, which picolibc does not provide, so use Zephyr's uptime
  // on the target instead.
  return k_uptime_get_32();
#else
  using namespace std::chrono;
  return static_cast<std::uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
#endif
}

// ---- parameter math (ported unchanged from controller/ui.cc) ----

float Clamp01(float v) {
  if (!(v >= 0.0f)) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float NormToHz(float n) {
  return engine::ParamNormToDisp(
      &engine::g_params[static_cast<int>(engine::ParamId::kCutoff)], n);
}

float HzToNorm(float h) {
  return engine::ParamDispToNorm(
      &engine::g_params[static_cast<int>(engine::ParamId::kCutoff)], h);
}

float QOf(float res) { return 0.5f + res * res * 20.0f; }

constexpr float kDbTop = 30.0f;
constexpr float kDbBot = -48.0f;

float ResToDb(float res) { return 20.0f * std::log10(0.5f + res * res * 20.0f); }

float DbToRes(float db) {
  const float q = std::pow(10.0f, db / 20.0f);
  if (q <= 0.5f) return 0.0f;
  return std::sqrt((q - 0.5f) / 20.0f);
}

// ---- text helpers ----

int TextW(const Font &f, const char *s) { return static_cast<int>(std::strlen(s)) * f.w; }

void TextLeft(FrameBuffer &fb, const char *s, int x, int y, const Font &f, Color c) {
  DrawGlyphRun(fb, x, y, s, static_cast<int>(std::strlen(s)), f, c, 0);
}

void TextRight(FrameBuffer &fb, const char *s, int xr, int y, const Font &f, Color c) {
  DrawGlyphRun(fb, xr - TextW(f, s), y, s, static_cast<int>(std::strlen(s)), f, c, 0);
}

// ---- cursor (used by the filter and envelope DYN hooks) ----

void Cursor(FrameBuffer &fb, int x, int y, int w, int h, Color c) {
  FillRect(fb, x, y, 7, 2, c);
  FillRect(fb, x + w - 7, y, 7, 2, c);
  FillRect(fb, x, y + h - 2, 7, 2, c);
  FillRect(fb, x + w - 7, y + h - 2, 7, 2, c);
  FillRect(fb, x, y, 2, 7, c);
  FillRect(fb, x + w - 2, y, 2, 7, c);
  FillRect(fb, x, y + h - 7, 2, 7, c);
  FillRect(fb, x + w - 2, y + h - 7, 2, 7, c);
}

// ---- column-update traces ----

// A horizontal graticule line (plot-local y + color). The column-update erase
// restores these lines instead of the background when a curve span passes over
// them, so the graticule survives per-column updates without a 1-bpp mask.
struct GratLine {
  int y;
  Color c;
};

// Updates a single-valued plot column-by-column against its per-buffer trace.
//
// For each column x the curve occupies plot-local rows [lo[x], hi[x]]
// (inclusive); lo[x] == -1 means "no curve in this column". Columns whose span
// matches the trace are skipped; changed columns erase the old span
// (graticule-aware) and draw the new span. `tr` must be the trace for the
// buffer currently being drawn (two-frame rule).
void ColumnUpdate(FrameBuffer &fb, int ox, int oy, int w, const int *lo,
                  const int *hi, TraceState &tr, const GratLine *grat,
                  int n_grat, Color line) {
  // Clip the column/row ranges to the framebuffer's clip rect (plot-local
  // coords -> frame coords) so a span can never spill outside it.
  const int x0 = std::max(0, fb.clip.x - ox);
  const int x1 = std::min(w, fb.clip.x + fb.clip.w - ox);
  const int y_top = fb.clip.y - oy;
  const int y_bot = fb.clip.y + fb.clip.h - oy - 1;
  for (int x = x0; x < x1; ++x) {
    const bool old_on = tr.y0[x] != kEmpty;
    const bool new_on = lo[x] >= 0;
    if (!old_on && !new_on) continue;
    // Skip columns whose span is unchanged (the documented column-update
    // optimisation): nothing to erase or redraw.
    if (old_on && new_on && tr.y0[x] == lo[x] && tr.y1[x] == hi[x]) continue;
    if (old_on) {
      for (int y = std::max<int>(tr.y0[x], y_top);
           y <= std::min<int>(tr.y1[x], y_bot); ++y) {
        Color c = kBg;
        for (int g = 0; g < n_grat; ++g)
          if (y == grat[g].y) { c = grat[g].c; break; }
        fb.px[static_cast<std::size_t>(oy + y) * fb.stride + (ox + x)] = c;
      }
    }
    if (new_on) {
      // Store the clamped (drawn) span, not the raw lo/hi: the draw loop
      // clamps to [y_top, y_bot], so an out-of-range value would wrap in the
      // uint8_t cast (e.g. (uint8_t)(-5) == 251) and the next erase would miss
      // the drawn pixels, leaving a permanent trail. Clamping here keeps the
      // trace == what was drawn regardless of what the callers produce.
      const int y0 = std::max<int>(lo[x], y_top);
      const int y1 = std::min<int>(hi[x], y_bot);
      for (int y = y0; y <= y1; ++y)
        fb.px[static_cast<std::size_t>(oy + y) * fb.stride + (ox + x)] = line;
      tr.y0[x] = static_cast<std::uint16_t>(y0);
      tr.y1[x] = static_cast<std::uint16_t>(y1);
    } else {
      tr.y0[x] = tr.y1[x] = kEmpty;
    }
  }
}

// Erases a plot-local overlay rect, restoring the background (bg + graticule)
// and the curve (from the trace) underneath it. Used to clear a moving cursor/
// handle/playhead's previous position before drawing the new one.
void EraseOverlayRect(FrameBuffer &fb, int ox, int oy, int w, int h,
                      const Rect &r, const TraceState &tr,
                      const GratLine *grat, int n_grat, Color line) {
  if (r.w <= 0 || r.h <= 0) return;
  // Intersect the overlay rect (plot-local) with the plot bounds AND the
  // framebuffer's clip rect so it can never spill outside either.
  const int x0 = std::max(std::max(0, r.x), fb.clip.x - ox);
  const int x1 = std::min(std::min(w, r.x + r.w), fb.clip.x + fb.clip.w - ox);
  const int y0 = std::max(std::max(0, r.y), fb.clip.y - oy);
  const int y1 = std::min(std::min(h, r.y + r.h), fb.clip.y + fb.clip.h - oy);
  for (int x = x0; x < x1; ++x) {
    for (int y = y0; y < y1; ++y) {
      Color c = kBg;
      for (int g = 0; g < n_grat; ++g)
        if (y == grat[g].y) { c = grat[g].c; break; }
      fb.px[static_cast<std::size_t>(oy + y) * fb.stride + (ox + x)] = c;
    }
    if (tr.y0[x] != kEmpty) {
      const int cy0 = std::max<int>(tr.y0[x], y0);
      const int cy1 = std::min<int>(tr.y1[x], y1 - 1);
      for (int y = cy0; y <= cy1; ++y)
        fb.px[static_cast<std::size_t>(oy + y) * fb.stride + (ox + x)] = line;
    }
  }
}

// ---- plot drawing (column-update) ----

void DrawOscPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int mid = h / 2;
  const int amp = static_cast<int>(h * 0.34f);
  const int x0 = 10, x1 = w - 10;
  const float cyc = 3.0f;

  const float ph = p.phase - std::floor(p.phase);
  for (int x = 0; x < w; ++x) p.col_lo[x] = -1;
  int prev = 0;
  for (int x = x0; x <= x1; ++x) {
    const float t =
        static_cast<float>(x - x0) / static_cast<float>(x1 - x0) * cyc;
    float pp = t + ph;
    pp -= std::floor(pp);
    const int y = mid - static_cast<int>((2.0f * pp - 1.0f) * amp);
    if (x == x0) {
      p.col_lo[x] = y;
      p.col_hi[x] = y;
    } else {
      p.col_lo[x] = std::min(prev, y);
      p.col_hi[x] = std::max(prev, y);
    }
    prev = y;
  }

  const GratLine grat[] = {
      {(h * 17) / 100, kFaint}, {h / 2, kDim}, {(h * 83) / 100, kFaint}};
  ColumnUpdate(fb, ox, oy, w, p.col_lo, p.col_hi, p.traces[0][p.fb_index],
               grat, 3, kBright);
}

void DrawFilterPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int L = 14, R = w - 14, T = 12, B = h - 18;

  const float fc = NormToHz(p.cutoff);
  const float q = QOf(p.resonance);
  const auto YFor = [&](float db) {
    return T + (kDbTop - db) / (kDbTop - kDbBot) * (B - T);
  };
  const auto XFor = [&](float f) { return L + HzToNorm(f) * (R - L); };

  const GratLine grat[] = {
      {(h * 17) / 100, kFaint}, {h / 2, kDim}, {(h * 83) / 100, kFaint},
      {B, kDim}};
  const int b = p.fb_index;

  // Erase the previous cutoff cursor (restoring the curve underneath) before
  // updating the curve and drawing the cursor at its new position.
  EraseOverlayRect(fb, ox, oy, w, h, p.filter_cursor[b], p.traces[1][b],
                   grat, 4, kBright);

  for (int x = 0; x < w; ++x) p.col_lo[x] = -1;
  int prev = 0;
  for (int x = L; x <= R; ++x) {
    const float f = NormToHz(static_cast<float>(x - L) / static_cast<float>(R - L));
    const float r = f / fc;
    const float den =
        std::sqrt((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q));
    float db = 20.0f * std::log10(den > 1e-6f ? 1.0f / den : 1e6f);
    if (db > kDbTop) db = kDbTop;
    if (db < kDbBot) db = kDbBot;
    const int y = static_cast<int>(std::lround(YFor(db)));
    if (x == L) {
      p.col_lo[x] = y;
      p.col_hi[x] = y;
    } else {
      p.col_lo[x] = std::min(prev, y);
      p.col_hi[x] = std::max(prev, y);
    }
    prev = y;
  }
  ColumnUpdate(fb, ox, oy, w, p.col_lo, p.col_hi, p.traces[1][b], grat, 4,
               kBright);

  // Draw the cutoff cursor at its new (plot-local) position and remember it.
  const int cx = static_cast<int>(std::lround(XFor(fc)));
  const int cy = static_cast<int>(std::lround(YFor(ResToDb(p.resonance))));
  Cursor(fb, ox + cx - 13, oy + cy - 20, 26, 40, kBright);
  p.filter_cursor[b] = Rect{cx - 13, cy - 20, 26, 40};
}

// ---- envelope ----

struct EnvLayout {
  int L, R, T, B, W, H, xA, xD, xH, xR, yS;
};

EnvLayout EnvLayoutOf(int w, int h, const Panel &p) {
  EnvLayout e;
  e.L = 14;
  e.R = w - 14;
  e.T = 12;
  e.B = h - 20;
  e.W = e.R - e.L;
  e.H = e.B - e.T;
  e.xA = e.L + static_cast<int>(p.attack * 0.25f * e.W);
  e.xD = e.xA + static_cast<int>(p.decay * 0.25f * e.W);
  e.xH = e.xD + static_cast<int>(0.20f * e.W);
  e.xR = e.xH + static_cast<int>(p.release * 0.30f * e.W);
  e.yS = e.T + static_cast<int>((1.0f - p.sustain) * e.H);
  return e;
}

// Real envelope level, mirroring the engine (linear attack/decay, exp release).
float EnvLevel(std::uint32_t now, const Panel &p) {
  const float attack_s = engine::ParamNormToDisp(
      &engine::g_params[static_cast<int>(engine::ParamId::kAttack)], p.attack);
  const float decay_s = engine::ParamNormToDisp(
      &engine::g_params[static_cast<int>(engine::ParamId::kDecay)], p.decay);
  const float release_s = engine::ParamNormToDisp(
      &engine::g_params[static_cast<int>(engine::ParamId::kRelease)], p.release);

  if (!p.note_on) {
    if (release_s <= 0.0f) return 0.0f;
    const float t = static_cast<float>(now - p.release_at) / 1000.0f;
    return p.release_from * std::exp(-t / release_s);
  }

  float t = static_cast<float>(now - p.note_at) / 1000.0f;
  if (attack_s > 0.0f && t < attack_s) return t / attack_s;
  t -= attack_s;
  if (decay_s > 0.0f && t < decay_s)
    return 1.0f - (1.0f - p.sustain) * (t / decay_s);
  return p.sustain;
}

constexpr float kReleaseTau = 5.0f;

// The ADSR curve's y at plot-local column x (single-valued piecewise curve).
int EnvYAt(int x, const EnvLayout &e) {
  if (x <= e.xA) {
    // Attack: (L,B) -> (xA,T).
    if (e.xA == e.L) return e.B;
    const float u = static_cast<float>(x - e.L) / static_cast<float>(e.xA - e.L);
    return e.B - static_cast<int>(u * (e.B - e.T));
  }
  if (x <= e.xD) {
    // Decay: (xA,T) -> (xD,yS).
    if (e.xD == e.xA) return e.T;
    const float u = static_cast<float>(x - e.xA) / static_cast<float>(e.xD - e.xA);
    return e.T + static_cast<int>(u * (e.yS - e.T));
  }
  if (x <= e.xH) return e.yS;  // Sustain plateau.
  if (x <= e.xR) {
    // Exponential release: (xH,yS) -> (xR,B).
    const float u = static_cast<float>(x - e.xH) / static_cast<float>(e.xR - e.xH);
    const float lvl = std::exp(-kReleaseTau * u);
    return e.yS + static_cast<int>((1.0f - lvl) * (e.B - e.yS));
  }
  return e.B;
}

void DrawEnvPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const EnvLayout e = EnvLayoutOf(w, h, p);

  const GratLine grat[] = {
      {(h * 17) / 100, kFaint}, {h / 2, kDim}, {(h * 83) / 100, kFaint},
      {e.B, kDim}};
  const int b = p.fb_index;
  TraceState &tr = p.traces[2][b];

  // Erase the previous handles + playhead (restoring the curve underneath)
  // before updating the curve and drawing them at their new positions.
  for (int i = 0; i < 3; ++i)
    EraseOverlayRect(fb, ox, oy, w, h, p.env_handles[i][b], tr, grat, 4, kBright);
  if (p.env_playhead_on[b])
    EraseOverlayRect(fb, ox, oy, w, h, p.env_playhead[b], tr, grat, 4, kBright);

  for (int x = 0; x < w; ++x) p.col_lo[x] = -1;
  int prev = 0;
  for (int x = e.L; x <= e.R; ++x) {
    const int y = EnvYAt(x, e);
    if (x == e.L) {
      p.col_lo[x] = y;
      p.col_hi[x] = y;
    } else {
      p.col_lo[x] = std::min(prev, y);
      p.col_hi[x] = std::max(prev, y);
    }
    prev = y;
  }
  ColumnUpdate(fb, ox, oy, w, p.col_lo, p.col_hi, tr, grat, 4, kBright);

  // Handles (plot-local rects remembered for the next frame's erase). The
  // plot draw runs under fb.clip == the plot rect, so a handle at the plot
  // top (T == 12, or sustain -> 1) is clipped to the plot interior rather
  // than drawing one row above it.
  const Rect ha{e.xA - 13, e.T - 13, 26, 26};
  const Rect hd{e.xD - 13, e.yS - 13, 26, 26};
  const Rect hr{e.xR - 13, e.B - 13, 26, 26};
  Cursor(fb, ox + ha.x, oy + ha.y, ha.w, ha.h, kBright);
  Cursor(fb, ox + hd.x, oy + hd.y, hd.w, hd.h, kBright);
  Cursor(fb, ox + hr.x, oy + hr.y, hr.w, hr.h, kBright);
  p.env_handles[0][b] = ha;
  p.env_handles[1][b] = hd;
  p.env_handles[2][b] = hr;

  // Playhead.
  const std::uint32_t now = NowMs();
  const float attack_s = engine::ParamNormToDisp(
      &engine::g_params[static_cast<int>(engine::ParamId::kAttack)], p.attack);
  const float decay_s = engine::ParamNormToDisp(
      &engine::g_params[static_cast<int>(engine::ParamId::kDecay)], p.decay);
  const float release_s = engine::ParamNormToDisp(
      &engine::g_params[static_cast<int>(engine::ParamId::kRelease)], p.release);

  int px = -1, py = -1;
  if (p.note_on) {
    float t = static_cast<float>(now - p.note_at) / 1000.0f;
    if (attack_s > 0.0f && t < attack_s) {
      const float u = t / attack_s;
      px = e.L + static_cast<int>(u * (e.xA - e.L));
      py = e.B - static_cast<int>(u * (e.B - e.T));
    } else {
      t -= attack_s;
      if (decay_s > 0.0f && t < decay_s) {
        const float u = t / decay_s;
        const float lvl = 1.0f - (1.0f - p.sustain) * u;
        px = e.xA + static_cast<int>(u * (e.xD - e.xA));
        py = e.B - static_cast<int>(lvl * (e.B - e.T));
      } else {
        const float u = std::fmin(t / 0.25f, 1.0f);
        px = e.xD + static_cast<int>(u * (e.xH - e.xD));
        py = e.yS;
      }
    }
  } else if (release_s > 0.0f) {
    const float t = static_cast<float>(now - p.release_at) / 1000.0f;
    const float u = t / (kReleaseTau * release_s);
    if (u < 1.0f) {
      const float lvl = p.release_from * std::exp(-t / release_s);
      px = e.xH + static_cast<int>(u * (e.xR - e.xH));
      py = e.B - static_cast<int>(lvl * (e.B - e.T));
    }
  }
  if (px >= 0) {
    FillRect(fb, ox + px - 2, oy + py - 2, 4, 4, kBright);
    p.env_playhead[b] = Rect{px - 2, py - 2, 4, 4};
    p.env_playhead_on[b] = true;
  } else {
    p.env_playhead_on[b] = false;
  }
}

// ---- output module (scope / cycle / spectrum) ----

void DrawScopePlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int mid = h / 2;
  const int amp = static_cast<int>(h * 0.42f);

  // One ring window across the plot: ReadLast requires count * stride <=
  // kCapacity, so stride = kCapacity / kPlotW (16384 / 900 = 18). The old
  // stride 48 was sized for the 230 px module plot and, at 900 px, wrapped
  // the ring ~2.6 times — three copies of the waveform.
  constexpr int kStride = ScopeRing::kCapacity / geom::kPlotW;
  float buf[geom::kPlotW];
  p.scope_ring.ReadLast(buf, w, kStride);

  float peak = 0.0f;
  int prev = 0;
  for (int x = 0; x < w; ++x) {
    const float av = std::fabs(buf[x]);
    if (av > peak) peak = av;
    const int y = std::clamp(mid - static_cast<int>(std::lround(buf[x] * amp)), 0, h - 1);
    if (x == 0) {
      p.col_lo[x] = y;
      p.col_hi[x] = y;
    } else {
      p.col_lo[x] = std::min(prev, y);
      p.col_hi[x] = std::max(prev, y);
    }
    prev = y;
  }
  p.scope_peak = peak;

  const GratLine grat[] = {
      {(h * 17) / 100, kFaint}, {h / 2, kDim}, {(h * 83) / 100, kFaint}};
  ColumnUpdate(fb, ox, oy, w, p.col_lo, p.col_hi, p.traces[3][p.fb_index],
               grat, 3, kBright);
}

void DrawCyclePlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int mid = h / 2;
  const int amp = static_cast<int>(h * 0.42f);

  const int period = static_cast<int>(engine::kSampleRate / p.freq);
  const int count = 3 * period;
  if (period >= 8 && count <= kCycleBufSize) {
    p.scope_ring.ReadLast(p.cycle_buf, count, 1);

    int trigger = -1;
    for (int i = 1; i < count; ++i) {
      if (p.cycle_buf[i - 1] < 0.0f && p.cycle_buf[i] >= 0.0f) {
        trigger = i;
        break;
      }
    }
    if (trigger >= 0 && trigger + period <= count) {
      int prev = 0;
      for (int x = 0; x < w; ++x) {
        const int idx = trigger + x * period / w;
        const int y = std::clamp(mid - static_cast<int>(std::lround(p.cycle_buf[idx] * amp)), 0, h - 1);
        if (x == 0) {
          p.col_lo[x] = y;
          p.col_hi[x] = y;
        } else {
          p.col_lo[x] = std::min(prev, y);
          p.col_hi[x] = std::max(prev, y);
        }
        prev = y;
      }
    } else {
      // No trigger: empty curve.
      for (int x = 0; x < w; ++x) p.col_lo[x] = -1;
    }
  } else {
    // Period out of range: empty curve.
    for (int x = 0; x < w; ++x) p.col_lo[x] = -1;
  }

  const GratLine grat[] = {
      {(h * 17) / 100, kFaint}, {h / 2, kDim}, {(h * 83) / 100, kFaint}};
  ColumnUpdate(fb, ox, oy, w, p.col_lo, p.col_hi, p.traces[3][p.fb_index],
               grat, 3, kBright);
}

void DrawSpectrumPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  constexpr int kN = kFftSize;
  constexpr int kBinCount = kN / 2;
  float *re = p.fft_re;
  float *im = p.fft_im;
  float *mag = p.fft_mag;

  p.scope_ring.ReadLast(re, kN, 1);
  for (int i = 0; i < kN; ++i) {
    re[i] *= 0.5f - 0.5f * std::cos(2.0f * controller::kPi * static_cast<float>(i) /
                                    static_cast<float>(kN - 1));
    im[i] = 0.0f;
  }
  controller::Fft(re, im, kN);

  float peak = 0.0f;
  for (int b = 1; b < kBinCount; ++b) {
    mag[b] = re[b] * re[b] + im[b] * im[b];
    if (mag[b] > peak) peak = mag[b];
  }

  const int T = 12, B = h - 18;
  const float db_top = 0.0f, db_bot = -90.0f;

  if (peak < 1e-12f) {
    for (int x = 0; x < w; ++x) p.col_lo[x] = -1;  // silence: no bars
  } else {
    for (int x = 0; x < w; ++x) {
      const float f0 = NormToHz(static_cast<float>(x) / w);
      const float f1 = NormToHz(static_cast<float>(x + 1) / w);
      int b0 = static_cast<int>(f0 * kN / engine::kSampleRate);
      int b1 = static_cast<int>(f1 * kN / engine::kSampleRate);
      if (b0 < 1) b0 = 1;
      if (b1 >= kBinCount) b1 = kBinCount - 1;
      if (b1 < b0) b1 = b0;
      float m = 0.0f;
      for (int b = b0; b <= b1; ++b)
        if (mag[b] > m) m = mag[b];
      float db = 10.0f * std::log10(m / peak);
      if (db < db_bot) db = db_bot;
      if (db > db_top) db = db_top;
      const int bar = static_cast<int>((db - db_bot) / (db_top - db_bot) * (B - T));
      p.col_lo[x] = B - bar;
      p.col_hi[x] = B;
    }
  }

  const GratLine grat[] = {
      {(h * 17) / 100, kFaint}, {h / 2, kDim}, {(h * 83) / 100, kFaint},
      {B, kDim}};
  ColumnUpdate(fb, ox, oy, w, p.col_lo, p.col_hi, p.traces[3][p.fb_index],
               grat, 4, kBright);
}

// The OUT view the current subject wants. The three OUT subjects share one
// plot slot, so the hook reads the subject to pick the view (the old mode
// buttons are gone, so scope_mode is no longer driven by anything).
ScopeMode ScopeModeOf(SubjectId s) {
  switch (s) {
    case SubjectId::kOutCycle: return ScopeMode::kCycle;
    case SubjectId::kOutSpec: return ScopeMode::kSpectrum;
    default: return ScopeMode::kScope;
  }
}

void DrawOutPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  switch (ScopeModeOf(InteractionNavState().subject)) {
    case ScopeMode::kCycle:
      DrawCyclePlot(fb, ox, oy, w, h, p);
      break;
    case ScopeMode::kSpectrum:
      DrawSpectrumPlot(fb, ox, oy, w, h, p);
      break;
    case ScopeMode::kScope:
    default:
      DrawScopePlot(fb, ox, oy, w, h, p);
      break;
  }
}

// ---- readouts ----

void FmtTime(float norm, engine::ParamId id, char *buf, int n) {
  const float s = engine::ParamNormToDisp(&engine::g_params[static_cast<int>(id)], norm);
  if (s <= 0.0f) std::snprintf(buf, n, "0s");
  else if (s < 1.0f) std::snprintf(buf, n, "%.0fms", s * 1000.0f);
  else std::snprintf(buf, n, "%.2fs", s);
}

// ---- module draw hooks (DynRegion callbacks) ----

void PlotOsc(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[0];
  const Rect saved = fb.clip;
  fb.clip = r;
  FillRect(fb, r.x, r.y, r.w, r.h, kBg);
  std::memset(&p->traces[0][p->fb_index], 0xFF, sizeof(TraceState));
  DrawOscPlot(fb, r.x, r.y, r.w, r.h, *p);
  fb.clip = saved;
}

void PlotFilter(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[1];
  const Rect saved = fb.clip;
  fb.clip = r;
  FillRect(fb, r.x, r.y, r.w, r.h, kBg);
  std::memset(&p->traces[1][p->fb_index], 0xFF, sizeof(TraceState));
  DrawFilterPlot(fb, r.x, r.y, r.w, r.h, *p);
  fb.clip = saved;
}

void PlotEnv(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[2];
  const Rect saved = fb.clip;
  fb.clip = r;
  FillRect(fb, r.x, r.y, r.w, r.h, kBg);
  std::memset(&p->traces[2][p->fb_index], 0xFF, sizeof(TraceState));
  DrawEnvPlot(fb, r.x, r.y, r.w, r.h, *p);
  fb.clip = saved;
}

void PlotOut(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[3];
  const Rect saved = fb.clip;
  fb.clip = r;
  FillRect(fb, r.x, r.y, r.w, r.h, kBg);
  std::memset(&p->traces[3][p->fb_index], 0xFF, sizeof(TraceState));
  DrawOutPlot(fb, r.x, r.y, r.w, r.h, *p);
  fb.clip = saved;
}

// ---- edit-screen chrome (title + pane + columns) ----
// Drawn per-frame (not cached) because it reflects the interaction layer's
// NavState. Pane rows mirror SubjectId's pane order (§7.6): singletons and
// instanced classes, with the globals below a rule.
struct PaneRow {
  const char *label;
  int subject0;  // first subject index (SubjectId enum value)
  int count;     // 1 = singleton, >1 = a strip of cells
  bool global;
  const char *const *cells;
};

const char *const kOutCells[3] = {"SC", "CY", "SP"};
const PaneRow kPaneRows[] = {
    {"PART", 0, 1, false, nullptr},
    {"OSC", 1, 4, false, nullptr},
    {"FILT", 5, 1, false, nullptr},
    {"AMP", 6, 1, false, nullptr},
    {"ENV", 7, 3, false, nullptr},
    {"LFO", 10, 3, false, nullptr},
    {"MOD", 13, 1, false, nullptr},
    {"OUT", 14, 3, true, kOutCells},
    {"FX", 17, 1, true, nullptr},
    {"PATCH", 18, 1, true, nullptr},
    {"CONF", 19, 1, true, nullptr},
};

void DrawStrip(FrameBuffer &fb, int y, int n, int sel,
               const char *const *cells) {
  // Pitch the cells at the strip's widest label: OUT's "SC"/"CY"/"SP" are two
  // glyphs, so the single-digit width (StripCellW(1) = 18) would overdraw by
  // 6 px. The mockup passed the widest label into StripCellW.
  int chars = 1;
  if (cells)
    for (int k = 0; k < n; ++k) {
      const int len = static_cast<int>(std::strlen(cells[k]));
      if (len > chars) chars = len;
    }
  const int cw = geom::StripCellW(chars);
  for (int k = 0; k < n; ++k) {
    const int cx = geom::kPaneX + geom::kStripX0 + k * cw;
    char num[2] = {static_cast<char>('1' + k), 0};
    const char *d = cells ? cells[k] : num;
    const int dw = static_cast<int>(std::strlen(d)) * kPrimaryFont.w + 4;
    // Fill either way: the inverse-video cursor must not leave a stale bright
    // block behind when it moves off a cell.
    FillRect(fb, cx, y, dw, geom::kStripH, k == sel ? kBright : kBg);
    TextLeft(fb, d, cx + 2, y + 3, kPrimaryFont, k == sel ? kBg : kMid);
  }
}

void DrawPane(FrameBuffer &fb, const NavState &nav) {
  const int tx = geom::kPaneX + geom::kLabelX;
  int y = geom::kPaneY;
  bool rule = false;
  const int subj = static_cast<int>(nav.subject);
  for (const PaneRow &row : kPaneRows) {
    if (row.global && !rule) {
      DrawHLine(fb, geom::kPaneX, y + 4, geom::kPaneW - 8, kDim);
      y += geom::kPaneRule + 6;
      rule = true;
    }
    if (row.count == 1) {
      const bool sel = (subj == row.subject0);
      const int ty = y + (geom::kPanePitch - kPrimaryFont.h) / 2;
      if (sel) {
        FillRect(fb, geom::kPaneX, y + 1, geom::kPaneW - 8,
                 geom::kPanePitch - 2, kBright);
        TextLeft(fb, row.label, tx, ty, kPrimaryFont, kBg);
      } else {
        FillRect(fb, geom::kPaneX, y + 1, geom::kPaneW - 8,
                 geom::kPanePitch - 2, kBg);
        TextLeft(fb, row.label, tx, ty, kPrimaryFont, kMid);
      }
      y += geom::kPanePitch;
    } else {
      const int hdr_h = geom::kPanePitch - 6;
      TextLeft(fb, row.label, tx, y, kPrimaryFont, kMid);
      const int sel_cell =
          (subj >= row.subject0 && subj < row.subject0 + row.count)
              ? subj - row.subject0
              : -1;
      DrawStrip(fb, y + hdr_h, row.count, sel_cell, row.cells);
      DrawVLine(fb, geom::kPaneX + 1, y, hdr_h + geom::kStripH, kMid);
      y += hdr_h + geom::kStripH + 8;
    }
  }
}

const char *ColumnLabel(const ColumnSpec &cs) {
  return cs.label;  // the column's uppercase display name (pages.cc)
}

// The split well: a bipolar value bar under the value text. Two segments meet
// at a centre gap (the parameter's mid); the value fills left (below mid) or
// right (above mid), and exactly mid is a full-height tick — the distinct zero
// state that `zero_notch` parameters get (arch-design §7.8).
void SplitWell(FrameBuffer &fb, int x, int y, int value) {
  constexpr int kHeadEnd = 8, kWellGap = 6;
  const int kBarW = geom::kColW - kHeadEnd;
  const int kWellSegW = (kBarW - kWellGap) / 2;
  FillRect(fb, x, y + 2, kWellSegW, 4, kDim);
  FillRect(fb, x + kWellSegW + kWellGap, y + 2, kWellSegW, 4, kDim);
  if (value == 0) {
    FillRect(fb, x + kWellSegW, y - 3, kWellGap, geom::kWellH + 6, kBright);
  } else {
    int len = (std::abs(value) * kWellSegW + 50) / 99;
    if (len < 2) len = 2;
    if (value > 0)
      FillRect(fb, x + kWellSegW + kWellGap, y + 2, len, 4, kBright);
    else
      FillRect(fb, x + kWellSegW - len, y + 2, len, 4, kBright);
  }
  DrawVLine(fb, x, y, geom::kWellH, kMid);
  DrawVLine(fb, x + kBarW - 1, y, geom::kWellH, kMid);
}

void DrawColumns(FrameBuffer &fb, const NavState &nav, const PageDesc &page) {
  for (int c = 0; c < geom::kColumns; ++c) {
    const ColumnSpec cs = Column<>(page, nav.group, c);
    const int x = geom::kColX(c);
    // Clear the header and value rows for every column first, so a label
    // change (TIMEBASE → CYCLES) or a kNone column past a partial final group
    // leaves no stale glyphs behind.
    FillRect(fb, x, geom::kHeaderY, geom::kColW, geom::kHeaderH - 4, kBg);
    FillRect(fb, x + 6, geom::kValueY, geom::kColW - 6, geom::kValueH, kBg);
    if (cs.kind == ColumnKind::kNone) continue;  // past a partial final group
    const char *label = ColumnLabel(cs);
    const int bw = static_cast<int>(std::strlen(label)) * kPrimaryFont.w + 12;
    FillRect(fb, x, geom::kHeaderY, bw, geom::kHeaderH - 4, kDim);
    TextLeft(fb, label, x + 6, geom::kHeaderY + 1, kPrimaryFont, kBright);
    DrawHLine(fb, x + bw + 2, geom::kHeaderY + geom::kHeaderH - 6,
              geom::kColW - bw - 2 - 8, kDim);
    if (cs.kind == ColumnKind::kParam) {
      char val[16];
      const float norm = engine::EngineGetParam(
          nav.part, engine::ParamRef{0, cs.param});
      engine::ParamFormatValue(
          &engine::g_params[static_cast<std::size_t>(cs.param)], norm, val,
          sizeof(val));
      TextLeft(fb, val, x + 6, geom::kValueY + 1, kPrimaryFont, kMid);
      const int well = static_cast<int>(std::lround((norm - 0.5f) * 198.0f));
      SplitWell(fb, x, geom::kWellY, well);
    }
    // kPending / kRouteField / kViewCtl: header only, empty value row.
  }
}

// Part-identity swatches: the active part is filled with its hue, the rest
// outlined. A global subject has no part, so all four outline. Returns the x
// just past the swatches.
int DrawPartSwatches(FrameBuffer &fb, int x, int y, int active) {
  constexpr int kSw = 8, kSh = 16, kGap = 4;
  for (int k = 0; k < 4; ++k) {
    const int sx = x + k * (kSw + kGap);
    if (k == active) {
      FillRect(fb, sx, y, kSw, kSh, kPartHue[k]);
    } else {
      DrawHLine(fb, sx, y, kSw, kFaint);
      DrawHLine(fb, sx, y + kSh - 1, kSw, kFaint);
      DrawVLine(fb, sx, y, kSh, kFaint);
      DrawVLine(fb, sx + kSw - 1, y, kSh, kFaint);
    }
  }
  return x + 4 * (kSw + kGap);
}

// Global subjects sit below the pane rule and are not owned by a part.
bool IsGlobalSubject(SubjectId s) {
  return static_cast<int>(s) >= static_cast<int>(SubjectId::kOutScope);
}

// The long-form subject name for the title bar (§7.6), spelled out so the
// pane's abbreviated cells have their expansion always on screen.
const char *LongName(SubjectId s) {
  switch (s) {
    case SubjectId::kPart: return "PART";
    case SubjectId::kOsc1: return "OSCILLATOR 1";
    case SubjectId::kOsc2: return "OSCILLATOR 2";
    case SubjectId::kOsc3: return "OSCILLATOR 3";
    case SubjectId::kOsc4: return "OSCILLATOR 4";
    case SubjectId::kFilt: return "FILTER";
    case SubjectId::kAmp: return "AMPLIFIER";
    case SubjectId::kEnv1: return "ENVELOPE 1";
    case SubjectId::kEnv2: return "ENVELOPE 2";
    case SubjectId::kEnv3: return "ENVELOPE 3";
    case SubjectId::kLfo1: return "LFO 1";
    case SubjectId::kLfo2: return "LFO 2";
    case SubjectId::kLfo3: return "LFO 3";
    case SubjectId::kMod: return "MODULATION";
    case SubjectId::kOutScope: return "SCOPE";
    case SubjectId::kOutCycle: return "CYCLE";
    case SubjectId::kOutSpec: return "SPECTRUM";
    case SubjectId::kFx: return "EFFECTS";
    case SubjectId::kPatch: return "PATCH";
    case SubjectId::kConf: return "CONFIGURATION";
    default: return "";
  }
}

// Mode prefix for the title, or "" in edit mode.
const char *ModePrefix(ViewMode m) {
  switch (m) {
    case ViewMode::kModArm: return "MOD ARM ";
    case ViewMode::kModView: return "MOD VIEW ";
    case ViewMode::kPerform: return "PERFORM ";
    default: return "";
  }
}

void DrawEditChrome(FrameBuffer &fb) {
  const NavState &nav = InteractionNavState();
  const PageDesc &page = g_pages[static_cast<int>(nav.subject)];

  // Title bar: part swatches, the screen name in full (with mode prefix), a
  // cut for the patch name, and the patch/group indicators (§7.1).
  FillRect(fb, geom::kTitleX, geom::kTitleY, geom::kTitleW, geom::kTitleH - 4,
           kDim);
  const bool global = IsGlobalSubject(nav.subject);
  const int nx = DrawPartSwatches(fb, geom::kTitleX + 8, geom::kTitleY + 3,
                                  global ? -1 : static_cast<int>(nav.part));
  char pn[3] = {'P', static_cast<char>('1' + nav.part), 0};
  if (global) { pn[0] = 'G'; pn[1] = 'L'; }
  TextLeft(fb, pn, nx + 4, geom::kTitleY + 2, kPrimaryFont,
           global ? kMid : kBright);

  // The navigator boundary, cut out of the filled bar rather than drawn on it.
  const int gx = geom::kPaneX + geom::kPaneW - 5;
  DrawVLine(fb, gx, geom::kTitleY, geom::kTitleH - 4, kBg);
  DrawVLine(fb, gx + 1, geom::kTitleY, geom::kTitleH - 4, kBg);

  char name[24];
  std::snprintf(name, sizeof(name), "%s%s", ModePrefix(nav.mode),
                LongName(nav.subject));
  TextLeft(fb, name, geom::kTitleNameX, geom::kTitleY + 2, kPrimaryFont,
           kBright);

  // A cut separating the screen name from the patch name.
  DrawVLine(fb, geom::kTitleSepX, geom::kTitleY, geom::kTitleH - 4, kBg);
  DrawVLine(fb, geom::kTitleSepX + 1, geom::kTitleY, geom::kTitleH - 4, kBg);
  TextLeft(fb, "GLASS BELLS MK2", geom::kTitlePatchX, geom::kTitleY + 2,
           kPrimaryFont, kMid);

  int rx = geom::kTitleX + geom::kTitleW - 8;
  TextRight(fb, "A007", rx, geom::kTitleY + 2, kPrimaryFont, kMid);
  const int gc = GroupCount<>(page);
  if (gc > 1) {
    char gs[24];
    std::snprintf(gs, sizeof(gs), "GROUP %d/%d", nav.group + 1, gc);
    rx -= 4 * kPrimaryFont.w + 16;
    TextRight(fb, gs, rx, geom::kTitleY + 2, kPrimaryFont, kBright);
  }

  DrawPane(fb, nav);
  DrawColumns(fb, nav, page);
}

void DrawChrome(FrameBuffer &fb, Panel &p) {
  // The screen descriptor draws the static chrome (background, pane boundary,
  // column header frames) and fills the DYN slot rects. The dynamic edit
  // chrome (title, pane cursor, column values) is drawn per-frame by
  // DrawEditChrome, not cached here.
  const auto &bytes = SignalScreen();
  spike::Interpret(bytes.data(), fb, MakeCtx(p.dyn, kNumSlots));
}

// ---- Panel API ----

#ifdef TWANG_UI_SDRAM
/// Fixed SDRAM address for the Panel on the target. SDRAM spans
/// 0x68000000..0x6c000000 (64 MiB); the GLCDC frame buffers occupy the first
/// ~4.8 MB (two 2.4 MB buffers) and the IPC block sits at 0x68400000
/// (engine/ipc_shared.h), so the Panel lands at +5 MB — clear of both.
constexpr std::uintptr_t kPanelSdrAddr = 0x68500000UL;
#endif

Panel *PanelCreate() {
#ifdef TWANG_UI_SDRAM
  // The Panel is ~160 KB of draw scratch; the M33's 640 KB SRAM is tight, so
  // place it in SDRAM (placement new — never freed in practice).
  auto *p = new (reinterpret_cast<void *>(kPanelSdrAddr)) Panel;
#else
  auto *p = new Panel;
#endif
  // Column traces start empty (no curve) in every column of every buffer.
  std::memset(p->traces, 0xFF, sizeof(p->traces));

  // The four plot slots: hooks/state live here; the rects are filled by the
  // screen descriptor's DYN ops on the first chrome draw. The mode-button slot
  // (4) is drawn directly by DrawChrome, not through the damage walk.
  void (*hooks[4])(FrameBuffer &, const Rect &, void *) = {
      PlotOsc, PlotFilter, PlotEnv, PlotOut};
  for (int i = 0; i < 4; ++i)
    p->dyn[i] = DynRegion{{0, 0, 0, 0}, hooks[i], p, true};
  p->dyn[kSlotMode] = DynRegion{{0, 0, 0, 0}, nullptr, nullptr, false};
  return p;
}

void SyncFromEngine(Panel *p);  // defined below (after PanelDraw)

void MarkDirty(Panel *p, SlotIdx idx) {
  p->dyn[idx].dirty = true;
  p->pending[idx] = 2;  // repaint into BOTH buffers (double buffering)
  p->damage.Add(p->dyn[idx].rect);
}

// The plot slot the current page shows, or -1 for pages without a plot. The
// four slots share one band; only the active page's plot may paint into it.
int ActivePlotSlot() {
  const NavState &nav = InteractionNavState();
  return g_pages[static_cast<int>(nav.subject)].dyn_slot;
}

void PanelDraw(Panel *p, FrameBuffer &fb, int buffer_index) {
  // Record which buffer we draw into; the backend owns the swap and passes it
  // in, so there is no independent toggle to desync.
  p->fb_index = buffer_index;

  // Poll the engine for parameter changes (MIDI CC, encoders) and invalidate
  // the affected plots before drawing.
  SyncFromEngine(p);

  // Drain the audio thread's scope-dirty flag into the output plot's
  // invalidation — the scope animates in steady state.
  if (p->scope_dirty.exchange(false, std::memory_order_relaxed))
    MarkDirty(p, kSlotOut);

  // The envelope playhead traces the ADSR while a note is held or releasing;
  // invalidate the env plot each frame so it animates (the scope does the same
  // via the audio thread's flag). The curve itself is unchanged, so the
  // column-update skip makes this cheap — only the moving playhead redraws.
  if (p->note_on || EnvLevel(NowMs(), *p) > 0.001f)
    MarkDirty(p, kSlotEnv);

  const int b = p->fb_index;

  if (!p->chrome_drawn[b]) {
    // First draw of this buffer: static chrome + every plot (full render).
    DrawChrome(fb, *p);
    // The background wipe invalidates the column traces; reset them so the
    // column-update skip does not elide a curve over the wiped background.
    for (int i = 0; i < 4; ++i)
      std::memset(&p->traces[i][b], 0xFF, sizeof(TraceState));
    // Only the active page's plot paints into the shared band; the other three
    // slots are different pages and must not paint over it.
    const int active = ActivePlotSlot();
    for (int i = 0; i < 4; ++i) {
      if (i != active) continue;
      p->dyn[i].draw(fb, p->dyn[i].rect, p->dyn[i].state);
      p->pending[i] = 0;
      p->dyn[i].dirty = false;
    }
    DrawEditChrome(fb);
    p->chrome_drawn[b] = true;
    p->damage.Repaint();  // the full render subsumes the pending damage
    return;
  }

  // `pending` and `damage` look duplicative and are complementary: `pending`
  // decides whether a hook *re-runs* into the second buffer (double buffering
  // means one MarkDirty must repaint twice), `damage` decides which *rects*
  // are repainted at all. Both are required; removing either leaves a stale
  // half. The gate is `dirty && pending` — `dirty` says *redraw*, `pending`
  // says *a buffer is still owed* — and they coincide because MarkDirty is the
  // sole writer of both.
  const int n = p->damage.Repaint();
  const int active = ActivePlotSlot();
  for (int k = 0; k < 4; ++k) {
    if (k != active) continue;  // only the active page's plot paints
    if (!p->dyn[k].dirty || p->pending[k] <= 0) continue;
    const Rect &pr = p->dyn[k].rect;
    bool hit = false;
    for (int i = 0; i < n && !hit; ++i) {
      const Rect &r = p->damage.Rects()[i];
      hit = r.x < pr.x + pr.w && pr.x < r.x + r.w && r.y < pr.y + pr.h &&
            pr.y < r.y + r.h;
    }
    if (hit) {
      p->dyn[k].draw(fb, pr, p->dyn[k].state);
      --p->pending[k];
      p->dyn[k].dirty = p->pending[k] > 0;
    }
  }
  if (active < 0) {
    // A page with no plot (dyn_slot = -1) must not leave the previous page's
    // plot in the shared band: MarkPage marks nothing for such pages, so the
    // band would otherwise keep whatever the last plotted page drew.
    FillRect(fb, geom::kPlotX, geom::kPlotY, geom::kPlotW, geom::kPlotH, kBg);
  }
  DrawEditChrome(fb);
}

// ---- pointer (touch/drag) ----

void ApplyFilterDrag(int x, int y, int w, int h) {
  const int L = 14, R = w - 14, T = 12, B = h - 18;
  const float cutoff = Clamp01(static_cast<float>(x - L) / static_cast<float>(R - L));
  const float db = kDbTop - static_cast<float>(y - T) / static_cast<float>(B - T) * (kDbTop - kDbBot);
  const float resonance = Clamp01(DbToRes(db));
  engine::EngineSetParam(0, engine::ParamRef{0, engine::ParamId::kCutoff}, cutoff);
  engine::EngineSetParam(0, engine::ParamRef{0, engine::ParamId::kResonance}, resonance);
}

int HitTestEnv(int x, int y, int w, int h, const Panel &p) {
  const EnvLayout e = EnvLayoutOf(w, h, p);
  const int hs[3][2] = {{e.xA, e.T}, {e.xD, e.yS}, {e.xR, e.B}};
  int best = -1;
  int bd = 18 * 18;
  for (int i = 0; i < 3; ++i) {
    const int dx = x - hs[i][0];
    const int dy = y - hs[i][1];
    const int d2 = dx * dx + dy * dy;
    if (d2 < bd) {
      bd = d2;
      best = i;
    }
  }
  return best;
}

void ApplyEnvDrag(Panel *p, int handle, int x, int y, int w, int h) {
  const int L = 14, R = w - 14, T = 12, B = h - 20;
  const int W = R - L, H = B - T;
  if (handle == 0) {
    engine::EngineSetParam(0, engine::ParamRef{0, engine::ParamId::kAttack},
        Clamp01(static_cast<float>(x - L) / (0.25f * W)));
  } else if (handle == 1) {
    const int xA = L + static_cast<int>(p->attack * 0.25f * W);
    engine::EngineSetParam(0, engine::ParamRef{0, engine::ParamId::kDecay},
        Clamp01(static_cast<float>(x - xA) / (0.25f * W)));
    engine::EngineSetParam(0, engine::ParamRef{0, engine::ParamId::kSustain},
        Clamp01(1.0f - static_cast<float>(y - T) / H));
  } else {
    const int xH = L + static_cast<int>(p->attack * 0.25f * W) +
                   static_cast<int>(p->decay * 0.25f * W) +
                   static_cast<int>(0.20f * W);
    engine::EngineSetParam(0, engine::ParamRef{0, engine::ParamId::kRelease},
        Clamp01(static_cast<float>(x - xH) / (0.30f * W)));
  }
}

// Poll the engine's parameter state and invalidate any plot whose backing
// parameters changed. This is the single sync point: MIDI CC, encoders, and
// any future input source write the engine directly, so the panel watches for
// changes rather than being pushed (the drag paths are covered too).
void SyncFromEngine(Panel *p) {
  const float cutoff = engine::EngineGetParam(0, engine::ParamRef{0, engine::ParamId::kCutoff});
  const float resonance = engine::EngineGetParam(0, engine::ParamRef{0, engine::ParamId::kResonance});
  const float attack = engine::EngineGetParam(0, engine::ParamRef{0, engine::ParamId::kAttack});
  const float decay = engine::EngineGetParam(0, engine::ParamRef{0, engine::ParamId::kDecay});
  const float sustain = engine::EngineGetParam(0, engine::ParamRef{0, engine::ParamId::kSustain});
  const float release = engine::EngineGetParam(0, engine::ParamRef{0, engine::ParamId::kRelease});

  if (cutoff != p->cutoff || resonance != p->resonance) {
    p->cutoff = cutoff;
    p->resonance = resonance;
    MarkDirty(p, kSlotFilter);
  }
  if (attack != p->attack || decay != p->decay || sustain != p->sustain ||
      release != p->release) {
    p->attack = attack;
    p->decay = decay;
    p->sustain = sustain;
    p->release = release;
    MarkDirty(p, kSlotEnv);
  }
}

void PanelPointer(Panel *p, PointerEvent e) {
  // Locate the plot under the pointer.
  for (int m = 0; m < 4; ++m) {
    const Rect &pr = p->dyn[m].rect;
    if (e.x < pr.x || e.x >= pr.x + pr.w || e.y < pr.y || e.y >= pr.y + pr.h)
      continue;
    const int x = e.x - pr.x;
    const int y = e.y - pr.y;
    if (m == 1) {  // filter XY pad
      if (e.kind == PointerKind::kPress) p->filter_drag = true;
      if (p->filter_drag && e.kind != PointerKind::kRelease)
        ApplyFilterDrag(x, y, pr.w, pr.h);
      if (e.kind == PointerKind::kRelease) p->filter_drag = false;
      return;
    }
    if (m == 2) {  // envelope handles
      if (e.kind == PointerKind::kPress) p->drag_handle = HitTestEnv(x, y, pr.w, pr.h, *p);
      if (p->drag_handle >= 0 && e.kind != PointerKind::kRelease)
        ApplyEnvDrag(p, p->drag_handle, x, y, pr.w, pr.h);
      if (e.kind == PointerKind::kRelease) p->drag_handle = -1;
      return;
    }
    return;
  }
}

void PanelNoteOn(Panel *p, float freq_hz, std::uint8_t velocity) {
  engine::EngineNoteOn(0, freq_hz, velocity);
  p->note_on = true;
  p->note_at = NowMs();
  p->freq = freq_hz;
  MarkDirty(p, kSlotOsc);
  MarkDirty(p, kSlotEnv);
  MarkDirty(p, kSlotOut);
}

void PanelNoteOff(Panel *p, float freq_hz) {
  p->release_from = EnvLevel(NowMs(), *p);
  p->note_on = false;
  p->release_at = NowMs();
  engine::EngineNoteOff(0, freq_hz);
  MarkDirty(p, kSlotEnv);
  MarkDirty(p, kSlotOut);
}

void PanelAudioTap(Panel *p, const float *samples, int n) {
  p->scope_ring.Write(samples, n);
  p->scope_dirty.store(true, std::memory_order_relaxed);
}

int PanelPlotDraws(const Panel *p, int idx) {
  return p->draw_counts[idx];
}

}  // namespace nostromo
