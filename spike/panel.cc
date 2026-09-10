// spike/panel.cc — the Nostromo controller panel.
//
// Draws the signal-flow layout (titlebar, four modules, keyboard, nav) with
// the framebuffer primitives, and drives the four dynamic plot regions from
// the cached parameter state and the audio-tap scope ring. Chrome is drawn
// once per buffer; plots redraw only when their invalidation flag is set.

#include "panel.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "damage.h"
#include "engine.h"
#include "fft.h"
#include "font.h"
#include "params.h"
#include "scope_ring.h"

namespace spike {

// ---- frame / layout constants (1024x600, per the Nostromo mockup) ----

constexpr int kFrameW = 1024;
constexpr int kFrameH = 600;

constexpr int kTitleX = 16, kTitleY = 16, kTitleW = 992, kTitleH = 26;
constexpr int kModY = 84, kModH = 340, kModW = 242;
constexpr int kPlotDX = 6, kPlotDY = 58, kPlotW = 230, kPlotH = 232;
constexpr int kReadoutX = kModW - 6;  // right-aligned margin
constexpr int kReadoutY = kModY + 306;
constexpr int kKeyY = 440;
constexpr int kKeyW = 992 / 13;
constexpr int kNavY = 512;

constexpr int kPx0[4] = {16, 266, 516, 766};
constexpr const char *kLabels[4] = {"OSCILLATOR", "FILTER", "ENVELOPE",
                                    "OUTPUT"};
constexpr const char *kModes[4] = {"POLYBLEP SAW", "TPT SVF LOWPASS", "ADSR",
                                   nullptr};

constexpr int kCycleBufSize = 4096;
constexpr int kFftSize = 8192;

// ---- Nostromo palette (RGB565) ----

constexpr Color Rgb565(int r, int g, int b) {
  return static_cast<Color>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
constexpr Color kBg = Rgb565(5, 10, 6);
constexpr Color kFaint = Rgb565(13, 53, 32);
constexpr Color kDim = Rgb565(27, 98, 56);
constexpr Color kMid = Rgb565(63, 191, 120);
constexpr Color kBright = Rgb565(124, 255, 176);

// ---- Panel struct ----

struct Panel {
  // Cached engine state + transients (control thread).
  float freq = 440.0f;
  float cutoff = 0.5f;
  float resonance = 0.0f;
  float attack = 0.0f;
  float decay = 0.0f;
  float sustain = 0.7f;
  float release = 0.0f;
  bool note_on = false;
  std::uint32_t note_at = 0;
  std::uint32_t release_at = 0;
  float release_from = 0.0f;
  float phase = 0.0f;
  float scope_peak = 0.0f;
  ScopeMode scope_mode = ScopeMode::kScope;

  // Audio tap + draw scratch (owned here, never heap-allocated in the draw).
  ScopeRing scope_ring;
  float cycle_buf[kCycleBufSize];
  float fft_re[kFftSize];
  float fft_im[kFftSize];
  float fft_mag[kFftSize / 2];

  // Damage + redraw state.
  Damage damage{kFrameW, kFrameH};
  bool full_redraw = true;

  // The four dynamic plot regions.
  DynRegion plots[4];

  // Drag state: -1 none, 0/1/2 env attack/decay/release.
  int drag_handle = -1;
  bool filter_drag = false;

  // Reusable polyline scratch (one point per plot column, <= 230).
  Point curve_pts[256];

  // Test/debug: draw-call counts per plot.
  int draw_counts[4] = {0, 0, 0, 0};
};

// ---- time ----

std::uint32_t NowMs() {
  using namespace std::chrono;
  return static_cast<std::uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
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

// ---- chrome helpers (ported from the mockup's primitives) ----

void PanelHeader(FrameBuffer &fb, int x, int y, int w, const char *label) {
  const int bw = TextW(kPrimaryFont, label) + 12;
  FillRect(fb, x, y, bw, 22, kDim);
  TextLeft(fb, label, x + 6, y + 1, kPrimaryFont, kBright);
  DrawHLine(fb, x + bw + 6, y + 20, w - bw - 6, kDim);
  DrawHLine(fb, x + bw + 6, y + 21, w - bw - 6, kDim);
}

void PlotFrame(FrameBuffer &fb, int x, int y, int w, int h) {
  DrawHLine(fb, x, y + (h * 17) / 100, w, kFaint);
  DrawHLine(fb, x, y + h / 2, w, kDim);
  DrawHLine(fb, x, y + (h * 83) / 100, w, kFaint);
}

void Brackets(FrameBuffer &fb, int x, int y, int w, int h, Color c) {
  const int leg = 10, th = 2, off = 6;
  FillRect(fb, x - off, y - off, leg, th, c);
  FillRect(fb, x + w + off - leg, y - off, leg, th, c);
  FillRect(fb, x - off, y + h + off - th, leg, th, c);
  FillRect(fb, x + w + off - leg, y + h + off - th, leg, th, c);
  FillRect(fb, x - off, y - off, th, leg, c);
  FillRect(fb, x + w + off - th, y - off, th, leg, c);
  FillRect(fb, x - off, y + h + off - leg, th, leg, c);
  FillRect(fb, x + w + off - th, y + h + off - leg, th, leg, c);
}

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

// ---- plot drawing ----

void DrawOscPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int mid = h / 2;
  const int amp = static_cast<int>(h * 0.34f);
  const int x0 = 10, x1 = w - 10;
  const float cyc = 3.0f;

  DrawHLine(fb, ox + x0, oy + mid, x1 - x0, kDim);

  const float ph = p.phase - std::floor(p.phase);
  int n = 0;
  for (int x = x0; x <= x1; ++x) {
    const float t = static_cast<float>(x - x0) / static_cast<float>(x1 - x0) * cyc;
    float pp = t + ph;
    pp -= std::floor(pp);
    const int y = mid - static_cast<int>((2.0f * pp - 1.0f) * amp);
    p.curve_pts[n++] = Point{ox + x, oy + y};
  }
  DrawPolyline(fb, p.curve_pts, n, kBright);
}

void DrawFilterPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int L = 14, R = w - 14, T = 12, B = h - 18;

  DrawHLine(fb, ox + L, oy + B, R - L, kDim);
  DrawVLine(fb, ox + L, oy + T, B - T, kDim);

  const float fc = NormToHz(p.cutoff);
  const float q = QOf(p.resonance);
  const auto XFor = [&](float f) { return L + HzToNorm(f) * (R - L); };
  const auto YFor = [&](float db) {
    return T + (kDbTop - db) / (kDbTop - kDbBot) * (B - T);
  };

  TextLeft(fb, "0dB", ox + L + 3, oy + T + 10, kSecondaryFont, kMid);
  TextLeft(fb, "20Hz", ox + L + 3, oy + B - 12, kSecondaryFont, kMid);
  TextRight(fb, "20k", ox + R - 3, oy + B - 12, kSecondaryFont, kMid);

  int n = 0;
  for (int x = L; x <= R; ++x) {
    const float f = NormToHz(static_cast<float>(x - L) / static_cast<float>(R - L));
    const float r = f / fc;
    const float den =
        std::sqrt((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q));
    float db = 20.0f * std::log10(den > 1e-6f ? 1.0f / den : 1e6f);
    if (db > kDbTop) db = kDbTop;
    if (db < kDbBot) db = kDbBot;
    const int y = static_cast<int>(std::lround(YFor(db)));
    p.curve_pts[n++] = Point{ox + x, oy + y};
  }
  DrawPolyline(fb, p.curve_pts, n, kBright);

  // Cutoff cursor.
  const int cutoff_x = ox + static_cast<int>(std::lround(XFor(fc)));
  const int peak_y = oy + static_cast<int>(std::lround(YFor(ResToDb(p.resonance))));
  Cursor(fb, cutoff_x - 13, peak_y - 20, 26, 40, kBright);
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

void DrawEnvPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const EnvLayout e = EnvLayoutOf(w, h, p);

  DrawHLine(fb, ox + e.L, oy + e.B, e.R - e.L, kDim);
  DrawVLine(fb, ox + e.L, oy + e.T, e.B - e.T, kDim);

  // ADSR curve: attack, decay, sustain, then the exponential release.
  int n = 0;
  p.curve_pts[n++] = Point{ox + e.L, oy + e.B};
  p.curve_pts[n++] = Point{ox + e.xA, oy + e.T};
  p.curve_pts[n++] = Point{ox + e.xD, oy + e.yS};
  p.curve_pts[n++] = Point{ox + e.xH, oy + e.yS};
  constexpr int kSteps = 32;
  for (int i = 1; i <= kSteps; ++i) {
    const float u = static_cast<float>(i) / kSteps;
    const float lvl = std::exp(-kReleaseTau * u);
    const int x = e.xH + static_cast<int>(u * (e.xR - e.xH));
    const int y = e.yS + static_cast<int>((1.0f - lvl) * (e.B - e.yS));
    p.curve_pts[n++] = Point{ox + x, oy + y};
  }
  DrawPolyline(fb, p.curve_pts, n, kBright);

  // Handles.
  Cursor(fb, ox + e.xA - 13, oy + e.T - 13, 26, 26, kBright);
  Cursor(fb, ox + e.xD - 13, oy + e.yS - 13, 26, 26, kBright);
  Cursor(fb, ox + e.xR - 13, oy + e.B - 13, 26, 26, kBright);

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
  if (px >= 0) FillRect(fb, ox + px - 2, oy + py - 2, 4, 4, kBright);
}

// ---- output module (scope / cycle / spectrum) ----

void DrawScopePlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int mid = h / 2;
  const int amp = static_cast<int>(h * 0.42f);
  DrawHLine(fb, ox, oy + mid, w, kDim);

  constexpr int kStride = 48;
  float buf[256];
  p.scope_ring.ReadLast(buf, w, kStride);

  float peak = 0.0f;
  int n = 0;
  for (int x = 0; x < w; ++x) {
    const float av = std::fabs(buf[x]);
    if (av > peak) peak = av;
    p.curve_pts[n++] = Point{ox + x, oy + mid - static_cast<int>(std::lround(buf[x] * amp))};
  }
  p.scope_peak = peak;
  DrawPolyline(fb, p.curve_pts, n, kBright);
}

void DrawCyclePlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  const int mid = h / 2;
  const int amp = static_cast<int>(h * 0.42f);
  DrawHLine(fb, ox, oy + mid, w, kDim);

  const int period = static_cast<int>(engine::kSampleRate / p.freq);
  if (period < 8) return;
  const int count = 3 * period;
  if (count > kCycleBufSize) return;
  p.scope_ring.ReadLast(p.cycle_buf, count, 1);

  int trigger = -1;
  for (int i = 1; i < count; ++i) {
    if (p.cycle_buf[i - 1] < 0.0f && p.cycle_buf[i] >= 0.0f) {
      trigger = i;
      break;
    }
  }
  if (trigger < 0 || trigger + period > count) return;

  int n = 0;
  for (int x = 0; x < w; ++x) {
    const int idx = trigger + x * period / w;
    p.curve_pts[n++] = Point{ox + x, oy + mid - static_cast<int>(std::lround(p.cycle_buf[idx] * amp))};
  }
  DrawPolyline(fb, p.curve_pts, n, kBright);
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
  DrawHLine(fb, ox, oy + B, w, kDim);
  DrawVLine(fb, ox, oy + B, oy + T, kDim);

  if (peak < 1e-12f) return;

  const float db_top = 0.0f, db_bot = -90.0f;
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
    DrawVLine(fb, ox + x, oy + B - bar, bar, kBright);
  }
}

void DrawOutPlot(FrameBuffer &fb, int ox, int oy, int w, int h, Panel &p) {
  switch (p.scope_mode) {
    case ScopeMode::kScope:
      DrawScopePlot(fb, ox, oy, w, h, p);
      break;
    case ScopeMode::kCycle:
      DrawCyclePlot(fb, ox, oy, w, h, p);
      break;
    case ScopeMode::kSpectrum:
      DrawSpectrumPlot(fb, ox, oy, w, h, p);
      break;
  }
}

// ---- readouts ----

void FmtTime(float norm, engine::ParamId id, char *buf, int n) {
  const float s = engine::ParamNormToDisp(&engine::g_params[static_cast<int>(id)], norm);
  if (s <= 0.0f)
    std::snprintf(buf, n, "0ms");
  else if (s < 1.0f)
    std::snprintf(buf, n, "%.0fms", s * 1000.0f);
  else
    std::snprintf(buf, n, "%.2fs", s);
}

void ReadoutText(Panel &p, int idx, char *buf, int n) {
  switch (idx) {
    case 0:  // oscillator
      std::snprintf(buf, n, "%.0fHz", p.freq);
      break;
    case 1: {  // filter
      const float hz = NormToHz(p.cutoff);
      if (hz >= 1000.0f)
        std::snprintf(buf, n, "%.2fkHz  RES %.0f%%", hz / 1000.0f, p.resonance * 100.0f);
      else
        std::snprintf(buf, n, "%.0fHz  RES %.0f%%", hz, p.resonance * 100.0f);
      break;
    }
    case 2: {  // envelope
      char a[16], d[16], r[16];
      FmtTime(p.attack, engine::ParamId::kAttack, a, sizeof(a));
      FmtTime(p.decay, engine::ParamId::kDecay, d, sizeof(d));
      FmtTime(p.release, engine::ParamId::kRelease, r, sizeof(r));
      std::snprintf(buf, n, "A %s  D %s  S %.0f%%  R %s", a, d, p.sustain * 100.0f, r);
      break;
    }
    default:  // output
      switch (p.scope_mode) {
        case ScopeMode::kScope:
          if (p.scope_peak > 0.01f)
            std::snprintf(buf, n, "env %.0f%%", p.scope_peak * 100.0f);
          else
            std::snprintf(buf, n, "NO SIGNAL");
          break;
        case ScopeMode::kCycle:
          std::snprintf(buf, n, "1 cycle  %.0fHz", p.freq);
          break;
        case ScopeMode::kSpectrum:
          std::snprintf(buf, n, "FFT 8192  Hann");
          break;
      }
      break;
  }
}

void DrawReadout(FrameBuffer &fb, Panel &p, int idx) {
  char buf[96];
  ReadoutText(p, idx, buf, sizeof(buf));
  TextRight(fb, buf, kPx0[idx] + kReadoutX, kReadoutY, kPrimaryFont,
            idx == 3 ? kMid : kBright);
}

// ---- module draw hooks (DynRegion callbacks) ----

void PlotOsc(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[0];
  DrawOscPlot(fb, r.x, r.y, r.w, r.h, *p);
  DrawReadout(fb, *p, 0);
}

void PlotFilter(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[1];
  DrawFilterPlot(fb, r.x, r.y, r.w, r.h, *p);
  DrawReadout(fb, *p, 1);
}

void PlotEnv(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[2];
  DrawEnvPlot(fb, r.x, r.y, r.w, r.h, *p);
  DrawReadout(fb, *p, 2);
}

void PlotOut(FrameBuffer &fb, const Rect &r, void *state) {
  auto *p = static_cast<Panel *>(state);
  ++p->draw_counts[3];
  DrawOutPlot(fb, r.x, r.y, r.w, r.h, *p);
  DrawReadout(fb, *p, 3);
}

// ---- static chrome ----

constexpr int kModeY = kModY + 26;
constexpr int kModeH = 18;

void DrawModeArea(FrameBuffer &fb, Panel &p) {
  // Output module: SCOPE / CYCLE / SPEC buttons (highlight the active mode).
  const char *names[3] = {"SCOPE", "CYCLE", "SPEC"};
  int bx = kPx0[3] + 6;
  for (int i = 0; i < 3; ++i) {
    const int bw = TextW(kSecondaryFont, names[i]) + 6;
    const bool active = static_cast<int>(p.scope_mode) == i;
    if (active) FillRect(fb, bx, kModeY, bw, kModeH, kBright);
    TextLeft(fb, names[i], bx + 3, kModeY + 2, kSecondaryFont,
             active ? kBg : kMid);
    bx += bw + 6;
  }
}

void DrawChrome(FrameBuffer &fb, Panel &p) {
  // Titlebar.
  FillRect(fb, kTitleX, kTitleY, kTitleW, kTitleH, kDim);
  TextLeft(fb, "SIGNAL FLOW", kTitleX + 8, kTitleY + 3, kPrimaryFont, kBright);
  TextRight(fb, "VOICE 01/16  NOMINAL", kTitleX + kTitleW - 8, kTitleY + 3,
            kPrimaryFont, kMid);

  // Four modules.
  for (int m = 0; m < 4; ++m) {
    const int X = kPx0[m];
    Brackets(fb, X, kModY, kModW, kModH, kMid);
    PanelHeader(fb, X, kModY, kModW, kLabels[m]);
    if (kModes[m]) {
      TextLeft(fb, kModes[m], X + 6, kModY + 29, kSecondaryFont, kMid);
    } else {
      DrawModeArea(fb, p);
    }
    DrawHLine(fb, X, kModY + 50, kModW, kDim);
    PlotFrame(fb, X + kPlotDX, kModY + kPlotDY, kPlotW, kPlotH);
    DrawHLine(fb, X, kModY + 298, kModW, kDim);
  }

  // Keyboard.
  DrawHLine(fb, kTitleX, kKeyY, kTitleW, kDim);
  const char *keys[13] = {"C", "C#", "D", "D#", "E", "F", "F#",
                          "G", "G#", "A", "A#", "B", "C"};
  const bool black[13] = {false, true,  false, true,  false, false, true,
                          false, true,  false, true,  false, false};
  for (int i = 0; i < 13; ++i) {
    const int X = kTitleX + i * kKeyW;
    if (black[i]) FillRect(fb, X, kKeyY + 1, kKeyW, 54, kDim);
    const int tx = X + (kKeyW - TextW(kPrimaryFont, keys[i])) / 2;
    TextLeft(fb, keys[i], tx, kKeyY + 19, kPrimaryFont, black[i] ? kBright : kMid);
  }
  DrawHLine(fb, kTitleX, kKeyY + 56, kTitleW, kDim);

  // Nav tabs.
  DrawHLine(fb, kTitleX, kNavY, kTitleW, kDim);
  const char *tabs[5] = {"SIGNAL", "MATRIX", "PATCH", "ARP", "SYS"};
  for (int i = 0; i < 5; ++i) {
    const int cx = kTitleX + i * 198 + 99;
    TextLeft(fb, tabs[i], cx - TextW(kPrimaryFont, tabs[i]) / 2, kNavY + 12,
             kPrimaryFont, i == 0 ? kBright : kMid);
    if (i == 0) FillRect(fb, cx - 62, kNavY + 38, 124, 3, kBright);
  }
  DrawHLine(fb, kTitleX, kNavY + 44, kTitleW, kDim);
  TextLeft(fb, "ENC1 CUTOFF   ENC2 RES   ENC3 ENV AMT   ENC4 LEVEL", kTitleX,
           kNavY + 54, kSecondaryFont, kDim);
}

// ---- Panel API ----

void DrawDyn(DynRegion &d, FrameBuffer &fb) {
  if (d.dirty) d.draw(fb, d.rect, d.state);
}

#ifdef TWANG_UI_SDRAM
/// Fixed SDRAM address for the Panel on the target. SDRAM spans
/// 0x68000000..0x6c000000 (64 MiB); the GLCDC frame buffer occupies the first
/// ~2.4 MB (ext-ram) and the IPC block sits at 0x68400000 (engine/ipc_shared.h),
/// so the Panel lands at +5 MB — clear of both.
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
  const Rect plot_rects[4] = {
      {kPx0[0] + kPlotDX, kModY + kPlotDY, kPlotW, kPlotH},
      {kPx0[1] + kPlotDX, kModY + kPlotDY, kPlotW, kPlotH},
      {kPx0[2] + kPlotDX, kModY + kPlotDY, kPlotW, kPlotH},
      {kPx0[3] + kPlotDX, kModY + kPlotDY, kPlotW, kPlotH},
  };
  void (*hooks[4])(FrameBuffer &, const Rect &, void *) = {
      PlotOsc, PlotFilter, PlotEnv, PlotOut};
  for (int i = 0; i < 4; ++i)
    p->plots[i] = DynRegion{plot_rects[i], hooks[i], p, true};
  return p;
}

void MarkDirty(Panel *p, int idx) {
  p->plots[idx].dirty = true;
  p->damage.Add(p->plots[idx].rect);
}

void PanelDraw(Panel *p, FrameBuffer &fb) {
  if (p->full_redraw) {
    DrawChrome(fb, *p);
    for (int i = 0; i < 4; ++i) {
      p->plots[i].draw(fb, p->plots[i].rect, p->plots[i].state);
      p->plots[i].dirty = false;
    }
    p->full_redraw = false;
    p->damage.Repaint();  // consume the initial damage
    return;
  }

  const int n = p->damage.Repaint();
  if (n == 0) return;

  // Redraw dirty plots intersecting a repaint rect.
  for (int i = 0; i < n; ++i) {
    const Rect &r = p->damage.Rects()[i];
    for (int k = 0; k < 4; ++k) {
      if (!p->plots[k].dirty) continue;
      const Rect &pr = p->plots[k].rect;
      const bool hit = r.x < pr.x + pr.w && pr.x < r.x + r.w &&
                       r.y < pr.y + pr.h && pr.y < r.y + r.h;
      if (hit) p->plots[k].draw(fb, pr, p->plots[k].state);
    }
  }
  for (int k = 0; k < 4; ++k) p->plots[k].dirty = false;
}

// ---- pointer (touch/drag) ----

void ApplyFilterDrag(int x, int y, int w, int h) {
  const int L = 14, R = w - 14, T = 12, B = h - 18;
  const float cutoff = Clamp01(static_cast<float>(x - L) / static_cast<float>(R - L));
  const float db = kDbTop - static_cast<float>(y - T) / static_cast<float>(B - T) * (kDbTop - kDbBot);
  const float resonance = Clamp01(DbToRes(db));
  engine::EngineSetParam(0, engine::ParamId::kCutoff, cutoff);
  engine::EngineSetParam(0, engine::ParamId::kResonance, resonance);
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
    engine::EngineSetParam(0, engine::ParamId::kAttack,
        Clamp01(static_cast<float>(x - L) / (0.25f * W)));
  } else if (handle == 1) {
    const int xA = L + static_cast<int>(p->attack * 0.25f * W);
    engine::EngineSetParam(0, engine::ParamId::kDecay,
        Clamp01(static_cast<float>(x - xA) / (0.25f * W)));
    engine::EngineSetParam(0, engine::ParamId::kSustain,
        Clamp01(1.0f - static_cast<float>(y - T) / H));
  } else {
    const int xH = L + static_cast<int>(p->attack * 0.25f * W) +
                   static_cast<int>(p->decay * 0.25f * W) +
                   static_cast<int>(0.20f * W);
    engine::EngineSetParam(0, engine::ParamId::kRelease,
        Clamp01(static_cast<float>(x - xH) / (0.30f * W)));
  }
}

// Sync the cached state back from the engine (MIDI/drag write the engine
// directly; the display reads back).
void SyncFromEngine(Panel *p) {
  p->cutoff = engine::EngineGetParam(0, engine::ParamId::kCutoff);
  p->resonance = engine::EngineGetParam(0, engine::ParamId::kResonance);
  p->attack = engine::EngineGetParam(0, engine::ParamId::kAttack);
  p->decay = engine::EngineGetParam(0, engine::ParamId::kDecay);
  p->sustain = engine::EngineGetParam(0, engine::ParamId::kSustain);
  p->release = engine::EngineGetParam(0, engine::ParamId::kRelease);
}

void PanelPointer(Panel *p, PointerEvent e) {
  const int X = kPx0[0];  // not used directly; see below

  // Keyboard: note on/off.
  if (e.kind == PointerKind::kPress && e.y >= kKeyY && e.y < kKeyY + 56) {
    const int idx = (e.x - kTitleX) / kKeyW;
    if (idx >= 0 && idx < 13) {
      const float freq = 261.63f * std::pow(2.0f, idx / 12.0f);
      PanelNoteOn(p, freq);
      return;
    }
  }

  // Locate the plot under the pointer.
  for (int m = 0; m < 4; ++m) {
    const Rect &pr = p->plots[m].rect;
    if (e.x < pr.x || e.x >= pr.x + pr.w || e.y < pr.y || e.y >= pr.y + pr.h)
      continue;
    const int x = e.x - pr.x;
    const int y = e.y - pr.y;
    if (m == 1) {  // filter XY pad
      if (e.kind == PointerKind::kPress) p->filter_drag = true;
      if (p->filter_drag && e.kind != PointerKind::kRelease) {
        ApplyFilterDrag(x, y, pr.w, pr.h);
        SyncFromEngine(p);
        MarkDirty(p, 1);
      }
      if (e.kind == PointerKind::kRelease) p->filter_drag = false;
      return;
    }
    if (m == 2) {  // envelope handles
      if (e.kind == PointerKind::kPress) p->drag_handle = HitTestEnv(x, y, pr.w, pr.h, *p);
      if (p->drag_handle >= 0 && e.kind != PointerKind::kRelease) {
        ApplyEnvDrag(p, p->drag_handle, x, y, pr.w, pr.h);
        SyncFromEngine(p);
        MarkDirty(p, 2);
      }
      if (e.kind == PointerKind::kRelease) p->drag_handle = -1;
      return;
    }
    return;
  }

  // Output-mode buttons (SCOPE / CYCLE / SPEC).
  if (e.kind == PointerKind::kPress && e.y >= kModeY && e.y < kModeY + kModeH) {
    const char *names[3] = {"SCOPE", "CYCLE", "SPEC"};
    int bx = kPx0[3] + 6;
    for (int i = 0; i < 3; ++i) {
      const int bw = TextW(kSecondaryFont, names[i]) + 6;
      if (e.x >= bx && e.x < bx + bw) {
        if (static_cast<int>(p->scope_mode) != i) {
          p->scope_mode = static_cast<ScopeMode>(i);
          p->full_redraw = true;  // mode highlight is chrome
          MarkDirty(p, 3);
        }
        return;
      }
      bx += bw + 6;
    }
    return;
  }
  (void)X;
}

void PanelNoteOn(Panel *p, float freq_hz) {
  engine::EngineNoteOn(0, freq_hz);
  p->note_on = true;
  p->note_at = NowMs();
  p->freq = freq_hz;
  MarkDirty(p, 0);
  MarkDirty(p, 2);
  MarkDirty(p, 3);
}

void PanelNoteOff(Panel *p, float freq_hz) {
  p->release_from = EnvLevel(NowMs(), *p);
  p->note_on = false;
  p->release_at = NowMs();
  engine::EngineNoteOff(0, freq_hz);
  MarkDirty(p, 2);
  MarkDirty(p, 3);
}

void PanelAudioTap(Panel *p, const float *samples, int n) {
  p->scope_ring.Write(samples, n);
}

int PanelPlotDraws(const Panel *p, int idx) {
  return p->draw_counts[idx];
}

}  // namespace spike
