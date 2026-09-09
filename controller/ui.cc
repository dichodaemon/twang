/// @file ui.cc
/// @brief Signal-flow panel UI, ported from the HTML mockup (ui/mockup).
///
/// Layout: titlebar, then Oscillator → Filter → Envelope → Output modules
/// with flat-color plots, then an equal-width keyboard. Touch-drag the filter
/// XY pad (cutoff/resonance) and the three envelope handles (attack,
/// decay/sustain, release); tap a key to play. Parameter names, display
/// curves and default values come from the engine's descriptor table
/// (engine/params.h) — the UI never hardcodes a parameter.
///
/// The plots are drawn with LVGL draw primitives (lines, dots, labels) so
/// the same vocabulary works on the target's software renderer.
///
/// All mutable state lives in the single `Ui` object created by ui_create().
/// Functions take the Ui* (or a reference into it) explicitly; nothing is
/// file-scope or static except immutable constants.

#include "ui.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>

#include "fft.h"
#include "params.h"

namespace {

using engine::ParamId;

// ---- palette (flat colors only; reproducible on lv_canvas) ----
// lv_color_hex is a plain function (color-depth dependent), so these are
// static-storage constants, not constexpr. Immutable, so file-scope is fine.
static const lv_color_t C_BG     = lv_color_hex(0xeef0f3);
static const lv_color_t C_PANEL  = lv_color_hex(0xffffff);
static const lv_color_t C_EDGE   = lv_color_hex(0xd8dbe1);
static const lv_color_t C_TEXT   = lv_color_hex(0x1c1e24);
static const lv_color_t C_MUTED  = lv_color_hex(0x5c6068);
static const lv_color_t C_SIGNAL = lv_color_hex(0x0e9f9f);
static const lv_color_t C_HANDLE = lv_color_hex(0xe07b00);
static const lv_color_t C_GRID   = lv_color_hex(0xdfe2e8);
static const lv_color_t C_BLACK  = lv_color_hex(0x2a2c30);
static const lv_color_t C_KEYLIT = lv_color_hex(0xf0f0f4);

// ---- math helpers (ported from the mockup) ----

float clamp01(float v) {
    if (!(v >= 0.0f)) return 0.0f;   // NaN or negative → 0
    if (v > 1.0f) return 1.0f;
    return v;
}

// Filter plot frequency axis. Identical to the engine's kCutoff curve
// (20..20000 Hz, ratio 1000); routed through the table so the UI never
// hardcodes it.
float norm_to_hz(float n) {
    return engine::ParamNormToDisp(
        &engine::g_params[static_cast<int>(ParamId::kCutoff)], n);
}
float hz_to_norm(float h) {
    return engine::ParamDispToNorm(
        &engine::g_params[static_cast<int>(ParamId::kCutoff)], h);
}

float q_of(float res) { return 0.5f + res * res * 20.0f; }
constexpr float kDbTop = 30.0f;
constexpr float kDbBot = -48.0f;
float res_to_db(float res) { return 20.0f * log10f(0.5f + res * res * 20.0f); }
float db_to_res(float db) {
    // resonance 0 == Q 0.5 == -6.02 dB; clamp there so the sqrt is never
    // negative (which would yield NaN).
    const float q = powf(10.0f, db / 20.0f);
    if (q <= 0.5f) return 0.0f;
    return sqrtf((q - 0.5f) / 20.0f);
}

// ---- low-level draw helpers ----

void draw_line(lv_layer_t *layer, int ox, int oy, int x0, int y0, int x1, int y1,
               lv_color_t color, int width, lv_opa_t opa = LV_OPA_COVER) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa = opa;
    dsc.p1.x = ox + x0;
    dsc.p1.y = oy + y0;
    dsc.p2.x = ox + x1;
    dsc.p2.y = oy + y1;
    lv_draw_line(layer, &dsc);
}

// Filled circle; if `fill` is set, the circle is filled with *fill and gets a
// 2 px border in `color` (the mockup's handle dots).
void draw_dot(lv_layer_t *layer, int ox, int oy, int x, int y, int r, lv_color_t color,
              const lv_color_t *fill) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_color = fill ? *fill : color;
    dsc.bg_opa = LV_OPA_COVER;
    if (fill) {
        dsc.border_color = color;
        dsc.border_width = 2;
        dsc.border_opa = LV_OPA_COVER;
    }
    lv_area_t a;
    a.x1 = static_cast<lv_coord_t>(ox + x - r);
    a.y1 = static_cast<lv_coord_t>(oy + y - r);
    a.x2 = static_cast<lv_coord_t>(ox + x + r);
    a.y2 = static_cast<lv_coord_t>(oy + y + r);
    lv_draw_rect(layer, &dsc, &a);
}

void draw_text(lv_layer_t *layer, int ox, int oy, int x, int y, const char *text,
               lv_color_t color, const lv_font_t *font) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font = font ? font : LV_FONT_DEFAULT;
    dsc.opa = LV_OPA_COVER;
    dsc.align = LV_TEXT_ALIGN_LEFT;
    dsc.text = text;
    lv_area_t a;
    a.x1 = static_cast<lv_coord_t>(ox + x);
    a.y1 = static_cast<lv_coord_t>(oy + y);
    a.x2 = static_cast<lv_coord_t>(ox + x + 200);
    a.y2 = static_cast<lv_coord_t>(oy + y + 30);
    lv_draw_label(layer, &dsc, &a);
}

// ---- oscillator plot (3-cycle saw, scrolled by phase) ----

void draw_osc(lv_layer_t *layer, int ox, int oy, int w, int h, const UiState &s) {
    const int mid = h / 2;
    const int amp = static_cast<int>(h * 0.34f);
    const int x0 = 10, x1 = w - 10;
    const float cyc = 3.0f;

    draw_line(layer, ox, oy, x0, mid, x1, mid, C_GRID, 1);

    const float ph = s.phase - floorf(s.phase);   // [0,1)
    int prev_x, prev_y;
    for (int x = x0; x <= x1; ++x) {
        const float t = static_cast<float>(x - x0) / static_cast<float>(x1 - x0) * cyc;
        float p = t + ph;
        p -= floorf(p);
        const int y = mid - static_cast<int>((2.0f * p - 1.0f) * amp);
        if (x == x0) {
            prev_x = x;
            prev_y = y;
        } else {
            draw_line(layer, ox, oy, prev_x, prev_y, x, y, C_SIGNAL, 2);
            prev_x = x;
            prev_y = y;
        }
    }
}

// ---- filter plot (2-pole lowpass magnitude; XY pad) ----

void draw_filter(lv_layer_t *layer, int ox, int oy, int w, int h, const UiState &s) {
    const int L = 14, R = w - 14, T = 12, B = h - 18;

    draw_line(layer, ox, oy, L, B, R, B, C_GRID, 1);
    draw_line(layer, ox, oy, L, B, L, T, C_GRID, 1);

    const float fc = norm_to_hz(s.cutoff);
    const float q = q_of(s.resonance);
    const auto x_for = [&](float f) { return L + hz_to_norm(f) * (R - L); };
    const auto y_for = [&](float db) {
        return T + (kDbTop - db) / (kDbTop - kDbBot) * (B - T);
    };

    const int y0 = static_cast<int>(lroundf(y_for(0.0f)));
    draw_line(layer, ox, oy, L, y0, R, y0, C_GRID, 1, LV_OPA_40);

    draw_text(layer, ox, oy, L, h - 14, "20Hz", C_MUTED, &lv_font_montserrat_10);
    draw_text(layer, ox, oy, R - 24, h - 14, "20k", C_MUTED, &lv_font_montserrat_10);
    draw_text(layer, ox, oy, L + 4, y0 - 12, "0dB", C_MUTED, &lv_font_montserrat_10);

    int prev_x, prev_y;
    for (int x = L; x <= R; ++x) {
        const float f = norm_to_hz(static_cast<float>(x - L) / static_cast<float>(R - L));
        const float r = f / fc;
        const float den = sqrtf((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q));
        float db = 20.0f * log10f(den > 1e-6f ? 1.0f / den : 1e6f);
        if (db > kDbTop) db = kDbTop;
        if (db < kDbBot) db = kDbBot;
        const int y = static_cast<int>(lroundf(y_for(db)));
        if (x == L) {
            prev_x = x;
            prev_y = y;
        } else {
            draw_line(layer, ox, oy, prev_x, prev_y, x, y, C_SIGNAL, 2);
            prev_x = x;
            prev_y = y;
        }
    }

    const int cutoff_x = static_cast<int>(lroundf(x_for(fc)));
    const int peak_y = static_cast<int>(lroundf(y_for(res_to_db(s.resonance))));
    draw_line(layer, ox, oy, cutoff_x, T, cutoff_x, B, C_HANDLE, 1);
    draw_dot(layer, ox, oy, cutoff_x, peak_y, 7, C_HANDLE, &C_PANEL);
}

// ---- envelope plot (3-handle ADSR) ----

struct EnvLayout {
    int L, R, T, B, W, H, xA, xD, xH, xR, yS;
};

EnvLayout env_layout(int w, int h, const UiState &s) {
    EnvLayout e;
    e.L = 14;
    e.R = w - 14;
    e.T = 12;
    e.B = h - 20;
    e.W = e.R - e.L;
    e.H = e.B - e.T;
    e.xA = e.L + static_cast<int>(s.attack * 0.25f * e.W);
    e.xD = e.xA + static_cast<int>(s.decay * 0.25f * e.W);
    e.xH = e.xD + static_cast<int>(0.20f * e.W);
    e.xR = e.xH + static_cast<int>(s.release * 0.30f * e.W);
    e.yS = e.T + static_cast<int>((1.0f - s.sustain) * e.H);
    return e;
}

struct Playhead { int x, y; bool active; };

// Release decay convention: the release segment spans kReleaseTau time
// constants of the engine's exponential release (level = e^(-t/release_s)).
// The curve and the playhead share this shape, so the dot follows the curve.
constexpr float kReleaseTau = 5.0f;

Playhead env_playhead(int w, int h, const UiState &s) {
    const EnvLayout e = env_layout(w, h, s);
    const std::uint32_t now = lv_tick_get();

    // Actual envelope times (seconds), matching the engine.
    const float attack_s = engine::ParamNormToDisp(
        &engine::g_params[static_cast<int>(ParamId::kAttack)], s.attack);
    const float decay_s = engine::ParamNormToDisp(
        &engine::g_params[static_cast<int>(ParamId::kDecay)], s.decay);
    const float release_s = engine::ParamNormToDisp(
        &engine::g_params[static_cast<int>(ParamId::kRelease)], s.release);

    Playhead ph = {0, 0, false};

    if (s.note_on) {
        float t = static_cast<float>(now - s.note_at) / 1000.0f;
        if (attack_s > 0.0f && t < attack_s) {
            // attack: linear 0 -> 1
            const float u = t / attack_s;
            ph.x = e.L + static_cast<int>(u * (e.xA - e.L));
            ph.y = e.B - static_cast<int>(u * (e.B - e.T));
            ph.active = true;
        } else {
            t -= attack_s;  // seconds into decay
            if (decay_s > 0.0f && t < decay_s) {
                // decay: linear 1 -> sustain
                const float u = t / decay_s;
                const float lvl = 1.0f - (1.0f - s.sustain) * u;
                ph.x = e.xA + static_cast<int>(u * (e.xD - e.xA));
                ph.y = e.B - static_cast<int>(lvl * (e.B - e.T));
                ph.active = true;
            } else {
                // sustain: settle from the decay end onto the hold segment
                const float u = fminf(t / 0.25f, 1.0f);
                ph.x = e.xD + static_cast<int>(u * (e.xH - e.xD));
                ph.y = e.yS;
                ph.active = true;
            }
        }
    } else if (release_s > 0.0f) {
        const float t = static_cast<float>(now - s.release_at) / 1000.0f;
        // Traverse the release segment over kReleaseTau time constants.
        const float u = t / (kReleaseTau * release_s);
        if (u < 1.0f) {
            // release: exponential decay toward 0 (matches the engine)
            const float lvl = s.release_from * expf(-t / release_s);
            ph.x = e.xH + static_cast<int>(u * (e.xR - e.xH));
            ph.y = e.B - static_cast<int>(lvl * (e.B - e.T));
            ph.active = true;
        }
    }
    return ph;
}

void draw_env(lv_layer_t *layer, int ox, int oy, int w, int h, const UiState &s) {
    const EnvLayout e = env_layout(w, h, s);

    draw_line(layer, ox, oy, e.L, e.B, e.R, e.B, C_GRID, 1);
    draw_line(layer, ox, oy, e.L, e.T, e.L, e.B, C_GRID, 1);

    draw_line(layer, ox, oy, e.L, e.B, e.xA, e.T, C_SIGNAL, 2);
    draw_line(layer, ox, oy, e.xA, e.T, e.xD, e.yS, C_SIGNAL, 2);
    draw_line(layer, ox, oy, e.xD, e.yS, e.xH, e.yS, C_SIGNAL, 2);
    // release: exponential decay from sustain to ~0 (same shape the dot follows)
    constexpr int kSteps = 32;
    int px = e.xH, py = e.yS;
    for (int i = 1; i <= kSteps; ++i) {
        const float u = static_cast<float>(i) / kSteps;
        const float lvl = expf(-kReleaseTau * u);  // 1 -> ~0.007
        const int x = e.xH + static_cast<int>(u * (e.xR - e.xH));
        const int y = e.yS + static_cast<int>((1.0f - lvl) * (e.B - e.yS));
        draw_line(layer, ox, oy, px, py, x, y, C_SIGNAL, 2);
        px = x;
        py = y;
    }

    draw_dot(layer, ox, oy, e.xA, e.T, 8, C_HANDLE, &C_PANEL);
    draw_dot(layer, ox, oy, e.xD, e.yS, 8, C_HANDLE, &C_PANEL);
    draw_dot(layer, ox, oy, e.xR, e.B, 8, C_HANDLE, &C_PANEL);

    const Playhead ph = env_playhead(w, h, s);
    if (ph.active) draw_dot(layer, ox, oy, ph.x, ph.y, 4, C_TEXT, &C_TEXT);
}

// ---- scope plot (one-pole lowpassed saw × envelope) ----

float env_level(std::uint32_t now, const UiState &s) {
    // Real envelope level, mirroring the engine: linear attack, linear decay,
    // exponential release, using the actual display times.
    const float attack_s = engine::ParamNormToDisp(
        &engine::g_params[static_cast<int>(ParamId::kAttack)], s.attack);
    const float decay_s = engine::ParamNormToDisp(
        &engine::g_params[static_cast<int>(ParamId::kDecay)], s.decay);
    const float release_s = engine::ParamNormToDisp(
        &engine::g_params[static_cast<int>(ParamId::kRelease)], s.release);

    if (!s.note_on) {
        if (release_s <= 0.0f) return 0.0f;
        const float t = static_cast<float>(now - s.release_at) / 1000.0f;
        return s.release_from * expf(-t / release_s);
    }

    float t = static_cast<float>(now - s.note_at) / 1000.0f;
    if (attack_s > 0.0f && t < attack_s) return t / attack_s;
    t -= attack_s;  // seconds into decay (0 when attack is instant)

    if (decay_s > 0.0f && t < decay_s)
        return 1.0f - (1.0f - s.sustain) * (t / decay_s);
    return s.sustain;
}

// ---- output plot: streaming scope ----

void draw_scope(lv_layer_t *layer, int ox, int oy, int w, int h, Ui *ui) {
    const int mid = h / 2;
    const int amp = static_cast<int>(h * 0.42f);

    draw_line(layer, ox, oy, 0, mid, w, mid, C_GRID, 1);

    // Read the trailing scope window (real engine output), one sample per
    // pixel, and track the peak for the readout.
    constexpr int kStride = 48;        // samples per pixel (~221 ms window)
    constexpr int kMaxWidth = 1024;    // plot width is ~221 px; guard for safety
    float buf[kMaxWidth];
    ui->scope_ring.ReadLast(buf, w, kStride);

    float peak = 0.0f;
    int prev_x, prev_y;
    for (int x = 0; x < w; ++x) {
        const float v = buf[x];
        const float av = fabsf(v);
        if (av > peak) peak = av;
        const int y = mid - static_cast<int>(lroundf(v * amp));
        if (x == 0) {
            prev_x = x;
            prev_y = y;
        } else {
            draw_line(layer, ox, oy, prev_x, prev_y, x, y, C_SIGNAL, 1);
            prev_x = x;
            prev_y = y;
        }
    }
    ui->state.scope_peak = peak;
}

// ---- output plot: triggered single cycle ----

void draw_cycle(lv_layer_t *layer, int ox, int oy, int w, int h, Ui *ui) {
    const int mid = h / 2;
    const int amp = static_cast<int>(h * 0.42f);
    draw_line(layer, ox, oy, 0, mid, w, mid, C_GRID, 1);

    const int period = static_cast<int>(engine::kSampleRate / ui->state.freq);
    if (period < 8) return;  // no note (or too high a frequency) to show

    // Read three periods and lock onto the first rising zero-crossing, so a
    // full cycle is always captured regardless of where the window began.
    // Sized for the lowest expected note (a few octaves below the keyboard).
    float *buf = ui->cycle_buf;
    const int count = 3 * period;
    if (count > kCycleBufSize) return;
    ui->scope_ring.ReadLast(buf, count, 1);

    int trigger = -1;
    for (int i = 1; i < count; ++i) {
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) { trigger = i; break; }
    }
    if (trigger < 0 || trigger + period > count) return;  // silence: just the axis

    int prev_x, prev_y;
    for (int x = 0; x < w; ++x) {
        const int idx = trigger + x * period / w;
        const int y = mid - static_cast<int>(lroundf(buf[idx] * amp));
        if (x == 0) {
            prev_x = x;
            prev_y = y;
        } else {
            draw_line(layer, ox, oy, prev_x, prev_y, x, y, C_SIGNAL, 1);
            prev_x = x;
            prev_y = y;
        }
    }
}

// ---- output plot: spectrum analyzer ----

void draw_spectrum(lv_layer_t *layer, int ox, int oy, int w, int h, Ui *ui) {
    constexpr int kN = kFftSize;
    constexpr int kBinCount = kN / 2;
    float *re = ui->fft_re;
    float *im = ui->fft_im;
    float *mag = ui->fft_mag;

    ui->scope_ring.ReadLast(re, kN, 1);
    for (int i = 0; i < kN; ++i) {
        re[i] *= 0.5f - 0.5f * cosf(2.0f * controller::kPi * static_cast<float>(i) / static_cast<float>(kN - 1));
        im[i] = 0.0f;
    }
    controller::Fft(re, im, kN);

    // Power per bin (skip DC), then normalize to the peak.
    float peak = 0.0f;
    for (int b = 1; b < kBinCount; ++b) {
        mag[b] = re[b] * re[b] + im[b] * im[b];
        if (mag[b] > peak) peak = mag[b];
    }

    // Axes (log frequency, same span as the filter plot).
    const int T = 12, B = h - 18;
    draw_line(layer, ox, oy, 0, B, w, B, C_GRID, 1);
    draw_line(layer, ox, oy, 0, B, 0, T, C_GRID, 1);

    if (peak < 1e-12f) return;  // silence: just the axes

    const float db_top = 0.0f, db_bot = -90.0f;
    for (int x = 0; x < w; ++x) {
        // Log-frequency range of this pixel column, in FFT bins.
        const float f0 = norm_to_hz(static_cast<float>(x) / w);
        const float f1 = norm_to_hz(static_cast<float>(x + 1) / w);
        int b0 = static_cast<int>(f0 * kN / engine::kSampleRate);
        int b1 = static_cast<int>(f1 * kN / engine::kSampleRate);
        if (b0 < 1) b0 = 1;
        if (b1 >= kBinCount) b1 = kBinCount - 1;
        if (b1 < b0) b1 = b0;
        float m = 0.0f;
        for (int b = b0; b <= b1; ++b) if (mag[b] > m) m = mag[b];

        float db = 10.0f * log10f(m / peak);  // power → dB
        if (db < db_bot) db = db_bot;
        if (db > db_top) db = db_top;
        const int bar = static_cast<int>((db - db_bot) / (db_top - db_bot) * (B - T));
        draw_line(layer, ox, oy, x, B, x, B - bar, C_SIGNAL, 1);
    }
}

void draw_out(lv_layer_t *layer, int ox, int oy, int w, int h, Ui *ui) {
    switch (ui->scope_mode) {
    case ScopeMode::kScope:    draw_scope(layer, ox, oy, w, h, ui); break;
    case ScopeMode::kCycle:    draw_cycle(layer, ox, oy, w, h, ui); break;
    case ScopeMode::kSpectrum: draw_spectrum(layer, ox, oy, w, h, ui); break;
    default: break;
    }
}

// ---- readouts (walk the engine parameter table) ----

void fmt_time(float norm, ParamId id, char *buf, int n) {
    const float s = engine::ParamNormToDisp(&engine::g_params[static_cast<int>(id)], norm);
    if (s <= 0.0f) std::snprintf(buf, n, "0ms");
    else if (s < 1.0f) std::snprintf(buf, n, "%.0fms", s * 1000.0f);
    else std::snprintf(buf, n, "%.2fs", s);
}

void set_osc_readout(Ui *ui) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0fHz", ui->state.freq);
    lv_label_set_text(ui->readouts[static_cast<int>(PlotKind::kOsc)], buf);
}

void set_filter_readout(Ui *ui) {
    const float hz = norm_to_hz(ui->state.cutoff);
    char b1[32], buf[64];
    if (hz >= 1000.0f) std::snprintf(b1, sizeof(b1), "%.2fkHz", hz / 1000.0f);
    else std::snprintf(b1, sizeof(b1), "%.0fHz", hz);
    std::snprintf(buf, sizeof(buf), "fc %s | res %.0f%%", b1,
                  ui->state.resonance * 100.0f);
    lv_label_set_text(ui->readouts[static_cast<int>(PlotKind::kFilter)], buf);
}

void set_env_readout(Ui *ui) {
    char a[16], d[16], r[16];
    fmt_time(ui->state.attack, ParamId::kAttack, a, sizeof(a));
    fmt_time(ui->state.decay, ParamId::kDecay, d, sizeof(d));
    fmt_time(ui->state.release, ParamId::kRelease, r, sizeof(r));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "A %s | D %s | S %.0f%% | R %s",
                  a, d, ui->state.sustain * 100.0f, r);
    lv_label_set_text(ui->readouts[static_cast<int>(PlotKind::kEnv)], buf);
}

void set_out_readout(Ui *ui) {
    char buf[48];
    switch (ui->scope_mode) {
    case ScopeMode::kScope:
        if (ui->state.scope_peak > 0.01f)
            std::snprintf(buf, sizeof(buf), "env %.0f%%", ui->state.scope_peak * 100.0f);
        else std::snprintf(buf, sizeof(buf), "silent");
        break;
    case ScopeMode::kCycle:
        std::snprintf(buf, sizeof(buf), "1 cycle | %.0f Hz", ui->state.freq);
        break;
    case ScopeMode::kSpectrum:
        std::snprintf(buf, sizeof(buf), "FFT 8192 | Hann");
        break;
    default:
        buf[0] = '\0';
        break;
    }
    lv_label_set_text(ui->readouts[static_cast<int>(PlotKind::kOut)], buf);
}

// Sync the UI's cached parameter state from the engine. MIDI (and any future
// control source) writes the engine directly, so the display reads back from
// the engine rather than trusting only drag updates.
void ui_sync_from_engine(Ui *ui) {
    UiState &s = ui->state;
    s.cutoff = engine::EngineGetParam(0, ParamId::kCutoff);
    s.resonance = engine::EngineGetParam(0, ParamId::kResonance);
    s.attack = engine::EngineGetParam(0, ParamId::kAttack);
    s.decay = engine::EngineGetParam(0, ParamId::kDecay);
    s.sustain = engine::EngineGetParam(0, ParamId::kSustain);
    s.release = engine::EngineGetParam(0, ParamId::kRelease);
    set_filter_readout(ui);
    set_env_readout(ui);
    lv_obj_invalidate(ui->plot_objs[static_cast<int>(PlotKind::kFilter)]);
    lv_obj_invalidate(ui->plot_objs[static_cast<int>(PlotKind::kEnv)]);
}

// ---- drag handling ----

void apply_filter_drag(int x, int y, int w, int h) {
    const int L = 14, R = w - 14, T = 12, B = h - 18;
    const float cutoff = clamp01(static_cast<float>(x - L) / static_cast<float>(R - L));
    const float db = kDbTop - static_cast<float>(y - T) / static_cast<float>(B - T) * (kDbTop - kDbBot);
    const float resonance = clamp01(db_to_res(db));
    // Write the engine only; the display is synced back by the anim timer.
    engine::EngineSetParam(0, ParamId::kCutoff, cutoff);
    engine::EngineSetParam(0, ParamId::kResonance, resonance);
}

int hit_test_env(int x, int y, int w, int h, const UiState &s) {
    const EnvLayout e = env_layout(w, h, s);
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

void apply_env_drag(const UiState &s, int handle, int x, int y, int w, int h) {
    const int L = 14, R = w - 14, T = 12, B = h - 20;
    const int W = R - L, H = B - T;
    // Write the engine only; the display is synced back by the anim timer.
    if (handle == 0) {   // attack
        engine::EngineSetParam(0, ParamId::kAttack,
            clamp01(static_cast<float>(x - L) / (0.25f * W)));
    } else if (handle == 1) {   // decay + sustain
        const int xA = L + static_cast<int>(s.attack * 0.25f * W);
        engine::EngineSetParam(0, ParamId::kDecay,
            clamp01(static_cast<float>(x - xA) / (0.25f * W)));
        engine::EngineSetParam(0, ParamId::kSustain,
            clamp01(1.0f - static_cast<float>(y - T) / H));
    } else {   // release
        const int xH = L + static_cast<int>(s.attack * 0.25f * W) +
                       static_cast<int>(s.decay * 0.25f * W) +
                       static_cast<int>(0.20f * W);
        engine::EngineSetParam(0, ParamId::kRelease,
            clamp01(static_cast<float>(x - xH) / (0.30f * W)));
    }
}

void plot_event_cb(lv_event_t *e) {
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    auto *pd = static_cast<PlotData *>(lv_obj_get_user_data(obj));
    Ui *ui = pd->ui;
    UiState *s = &ui->state;

    if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) {
        lv_layer_t *layer = lv_event_get_layer(e);
        const int w = lv_obj_get_width(obj);
        const int h = lv_obj_get_height(obj);
        // DRAW_MAIN draws in absolute display coordinates; the plot's local
        // (0,0) is at the object's top-left corner.
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        const int ox = a.x1, oy = a.y1;
        switch (pd->kind) {
        case PlotKind::kOsc:    draw_osc(layer, ox, oy, w, h, *s); break;
        case PlotKind::kFilter: draw_filter(layer, ox, oy, w, h, *s); break;
        case PlotKind::kEnv:    draw_env(layer, ox, oy, w, h, *s); break;
        case PlotKind::kOut:    draw_out(layer, ox, oy, w, h, ui); break;
        default: break;
        }
        return;
    }

    if (pd->kind != PlotKind::kFilter && pd->kind != PlotKind::kEnv) return;

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_event_get_indev(e);
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        const int x = pt.x - a.x1;
        const int y = pt.y - a.y1;
        const int w = lv_obj_get_width(obj);
        const int h = lv_obj_get_height(obj);

        if (pd->kind == PlotKind::kFilter) {
            apply_filter_drag(x, y, w, h);
        } else if (pd->kind == PlotKind::kEnv) {
            if (code == LV_EVENT_PRESSED) pd->drag_handle = hit_test_env(x, y, w, h, *s);
            if (pd->drag_handle >= 0) apply_env_drag(*s, pd->drag_handle, x, y, w, h);
        }
    } else if (code == LV_EVENT_RELEASED) {
        pd->drag_handle = -1;
    }
}

// ---- keyboard ----

struct KeyDef { const char *name; int semi; bool black; };

constexpr KeyDef kKeys[] = {
    {"C", 0, false},  {"C#", 1, true},  {"D", 2, false},  {"D#", 3, true},
    {"E", 4, false},  {"F", 5, false},  {"F#", 6, true},  {"G", 7, false},
    {"G#", 8, true},  {"A", 9, false},  {"A#", 10, true}, {"B", 11, false},
    {"C", 12, false},
};
constexpr float kBaseFreq = 261.63f;

float key_freq(int semi) { return kBaseFreq * powf(2.0f, semi / 12.0f); }

void key_event_cb(lv_event_t *e) {
    lv_obj_t *key = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const auto *kd = static_cast<const KeyDef *>(lv_obj_get_user_data(key));
    // The keyboard container holds the Ui*; the key's parent is that container.
    Ui *ui = static_cast<Ui *>(lv_obj_get_user_data(lv_obj_get_parent(key)));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        ui_note_on(ui, key_freq(kd->semi));
    } else if (code == LV_EVENT_RELEASED) {
        ui_note_off(ui, key_freq(kd->semi));
    }
}

// ---- layout ----

void style_transparent(lv_obj_t *obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

// ---- output mode switch (segmented header buttons) ----

constexpr const char *kModeNames[] = {"SCOPE", "CYCLE", "SPEC"};

void refresh_mode_buttons(Ui *ui) {
    for (int i = 0; i < static_cast<int>(ScopeMode::kCount); ++i) {
        const bool active = (static_cast<int>(ui->scope_mode) == i);
        lv_obj_set_style_bg_color(ui->mode_btns[i],
                                  active ? C_SIGNAL : C_EDGE, 0);
        lv_obj_set_style_text_color(ui->mode_labels[i],
                                    active ? lv_color_white() : C_MUTED, 0);
    }
}

void mode_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Ui *ui = static_cast<Ui *>(lv_obj_get_user_data(btn));
    for (int i = 0; i < static_cast<int>(ScopeMode::kCount); ++i) {
        if (ui->mode_btns[i] != btn) continue;
        ui->scope_mode = static_cast<ScopeMode>(i);
        refresh_mode_buttons(ui);
        lv_obj_invalidate(ui->plot_objs[static_cast<int>(PlotKind::kOut)]);
        set_out_readout(ui);
        return;
    }
}

void make_mode_buttons(Ui *ui, lv_obj_t *header) {
    lv_obj_t *group = lv_obj_create(header);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_style_pad_column(group, 2, 0);

    for (int i = 0; i < static_cast<int>(ScopeMode::kCount); ++i) {
        lv_obj_t *btn = lv_button_create(group);
        lv_obj_set_style_radius(btn, 3, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_hor(btn, 5, 0);
        lv_obj_set_style_pad_ver(btn, 1, 0);
        lv_obj_set_user_data(btn, ui);
        lv_obj_add_event_cb(btn, mode_btn_event_cb, LV_EVENT_ALL, nullptr);
        ui->mode_btns[i] = btn;

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, kModeNames[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        ui->mode_labels[i] = lbl;
    }
    refresh_mode_buttons(ui);
}

void make_module(Ui *ui, lv_obj_t *parent, const char *title, const char *subtitle,
                 PlotKind kind) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_style_bg_color(panel, C_PANEL, 0);
    lv_obj_set_style_border_color(panel, C_EDGE, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_pad_row(panel, 0, 0);
    lv_obj_set_style_pad_column(panel, 0, 0);

    // header
    lv_obj_t *hd = lv_obj_create(panel);
    lv_obj_set_flex_flow(hd, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hd, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(hd, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hd, 0, 0);
    lv_obj_set_style_border_side(hd, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hd, C_EDGE, 0);
    lv_obj_set_style_border_width(hd, 1, 0);
    lv_obj_set_style_pad_ver(hd, 7, 0);
    lv_obj_set_style_pad_hor(hd, 12, 0);
    lv_obj_set_width(hd, LV_PCT(100));
    lv_obj_set_height(hd, LV_SIZE_CONTENT);

    lv_obj_t *ht = lv_label_create(hd);
    lv_label_set_text(ht, title);
    lv_obj_set_style_text_color(ht, C_MUTED, 0);
    lv_obj_set_style_text_font(ht, &lv_font_montserrat_10, 0);

    if (kind == PlotKind::kOut) {
        make_mode_buttons(ui, hd);
    } else {
        lv_obj_t *hs = lv_label_create(hd);
        lv_label_set_text(hs, subtitle);
        lv_obj_set_style_text_color(hs, C_TEXT, 0);
        lv_obj_set_style_text_font(hs, &lv_font_montserrat_12, 0);
    }

    // plot
    auto *pd = &ui->plots[static_cast<int>(kind)];
    pd->kind = kind;
    pd->ui = ui;
    pd->drag_handle = -1;

    lv_obj_t *plot = lv_obj_create(panel);
    lv_obj_set_flex_grow(plot, 1);
    lv_obj_set_width(plot, LV_PCT(100));
    lv_obj_add_flag(plot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(plot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(plot, 0, 0);
    lv_obj_set_style_pad_all(plot, 0, 0);
    lv_obj_set_user_data(plot, pd);
    lv_obj_add_event_cb(plot, plot_event_cb, LV_EVENT_ALL, nullptr);
    ui->plot_objs[static_cast<int>(kind)] = plot;

    // readout
    lv_obj_t *ro = lv_label_create(panel);
    lv_obj_set_width(ro, LV_PCT(100));
    lv_obj_set_style_bg_opa(ro, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(ro, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(ro, C_EDGE, 0);
    lv_obj_set_style_border_width(ro, 1, 0);
    lv_obj_set_style_pad_ver(ro, 7, 0);
    lv_obj_set_style_text_color(ro, C_TEXT, 0);
    lv_obj_set_style_text_font(ro, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(ro, LV_TEXT_ALIGN_CENTER, 0);
    ui->readouts[static_cast<int>(kind)] = ro;
}

void arrow_draw_cb(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const int w = lv_obj_get_width(obj);
    const int h = lv_obj_get_height(obj);
    const int cy = h / 2;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int ox = a.x1, oy = a.y1;

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = C_SIGNAL;
    dsc.opa = LV_OPA_COVER;
    dsc.p[0].x = ox;
    dsc.p[0].y = oy + cy - 8;
    dsc.p[1].x = ox;
    dsc.p[1].y = oy + cy + 8;
    dsc.p[2].x = ox + w;
    dsc.p[2].y = oy + cy;
    lv_draw_triangle(layer, &dsc);
}

void make_arrow(lv_obj_t *parent) {
    lv_obj_t *a = lv_obj_create(parent);
    lv_obj_set_width(a, 14);
    lv_obj_set_height(a, LV_PCT(100));
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a, 0, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_add_event_cb(a, arrow_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
}

void make_keyboard(Ui *ui, lv_obj_t *parent) {
    lv_obj_t *kb = lv_obj_create(parent);
    lv_obj_set_flex_flow(kb, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_hor(kb, 16, 0);
    lv_obj_set_style_pad_ver(kb, 12, 0);
    lv_obj_set_style_pad_column(kb, 0, 0);
    lv_obj_set_width(kb, LV_PCT(100));
    lv_obj_set_height(kb, 112);
    // The keyboard container carries the Ui* so key_event_cb can reach it via
    // each key's parent.
    lv_obj_set_user_data(kb, ui);

    for (const KeyDef &kd : kKeys) {
        lv_obj_t *key = lv_button_create(kb);
        lv_obj_set_flex_grow(key, 1);
        lv_obj_set_height(key, LV_PCT(100));
        lv_obj_add_flag(key, LV_OBJ_FLAG_STATE_TRICKLE);
        lv_obj_set_style_radius(key, 0, 0);
        lv_obj_set_style_shadow_width(key, 0, 0);
        lv_obj_set_style_border_width(key, 1, 0);
        lv_obj_set_style_border_color(key, kd.black ? C_BLACK : C_EDGE, 0);
        lv_obj_set_style_bg_color(key, kd.black ? C_BLACK : C_PANEL, 0);
        lv_obj_set_style_bg_color(key, C_HANDLE, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(key, C_HANDLE, LV_STATE_PRESSED);
        lv_obj_set_user_data(key, const_cast<KeyDef *>(&kd));
        lv_obj_add_event_cb(key, key_event_cb, LV_EVENT_ALL, nullptr);

        lv_obj_t *kl = lv_label_create(key);
        lv_label_set_text(kl, kd.name);
        lv_obj_center(kl);
        lv_obj_set_style_text_color(kl, kd.black ? C_KEYLIT : C_TEXT, 0);
        lv_obj_set_style_text_color(kl, lv_color_white(), LV_STATE_PRESSED);
    }
}

void anim_timer_cb(lv_timer_t *t) {
    Ui *ui = static_cast<Ui *>(lv_timer_get_user_data(t));
    ui->state.phase += 64.0f / 48000.0f;
    if (ui->state.phase >= 1.0f) ui->state.phase -= 1.0f;
    ui_sync_from_engine(ui);
    lv_obj_invalidate(ui->plot_objs[static_cast<int>(PlotKind::kOsc)]);
    lv_obj_invalidate(ui->plot_objs[static_cast<int>(PlotKind::kEnv)]);
    lv_obj_invalidate(ui->plot_objs[static_cast<int>(PlotKind::kOut)]);
    set_out_readout(ui);
}

}  // namespace

void ui_note_on(Ui *ui, float freq_hz) {
    engine::EngineNoteOn(0, freq_hz);
    ui->state.note_on = true;
    ui->state.note_at = lv_tick_get();
    ui->state.freq = freq_hz;
    set_osc_readout(ui);
}

void ui_note_off(Ui *ui, float freq_hz) {
    ui->state.release_from = env_level(lv_tick_get(), ui->state);
    ui->state.note_on = false;
    ui->state.release_at = lv_tick_get();
    engine::EngineNoteOff(0, freq_hz);
}

void ui_audio_tap(Ui *ui, const float *samples, int n) {
    ui->scope_ring.Write(samples, n);
}

#ifdef TWANG_UI_SDRAM
/// Fixed SDRAM address for the Ui object on the target. SDRAM spans
/// 0x68000000..0x6c000000 (64 MiB); the GLCDC frame buffer occupies the first
/// ~2.4 MB (ext-ram) and the IPC block sits at 0x68400000 (engine/ipc_shared.h),
/// so the Ui lands at +5 MB — clear of both.
constexpr std::uintptr_t kUiSdrAddr = 0x68500000UL;
#endif

Ui *ui_create(lv_obj_t *screen) {
#ifdef TWANG_UI_SDRAM
    // The Ui is ~160 KB of draw scratch; the M33's 640 KB SRAM is tight, so
    // place it in SDRAM (placement new — never freed in practice).
    Ui *ui = new (reinterpret_cast<void *>(kUiSdrAddr)) Ui;
#else
    Ui *ui = new Ui;
#endif

    // Initialize state from the engine's descriptor table defaults.
    ui->state.freq = 440.0f;
    ui->state.cutoff = engine::g_params[static_cast<int>(ParamId::kCutoff)].def;
    ui->state.resonance = engine::g_params[static_cast<int>(ParamId::kResonance)].def;
    ui->state.attack = engine::g_params[static_cast<int>(ParamId::kAttack)].def;
    ui->state.decay = engine::g_params[static_cast<int>(ParamId::kDecay)].def;
    ui->state.sustain = engine::g_params[static_cast<int>(ParamId::kSustain)].def;
    ui->state.release = engine::g_params[static_cast<int>(ParamId::kRelease)].def;

    lv_obj_set_style_bg_color(screen, C_BG, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(screen, 0, 0);
    lv_obj_set_style_pad_column(screen, 0, 0);

    // titlebar
    lv_obj_t *titlebar = lv_obj_create(screen);
    lv_obj_set_flex_flow(titlebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(titlebar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(titlebar, 0, 0);
    lv_obj_set_style_pad_hor(titlebar, 16, 0);
    lv_obj_set_style_pad_ver(titlebar, 8, 0);
    lv_obj_set_style_pad_column(titlebar, 10, 0);
    lv_obj_set_width(titlebar, LV_PCT(100));
    lv_obj_set_height(titlebar, LV_SIZE_CONTENT);

    lv_obj_t *t1 = lv_label_create(titlebar);
    lv_label_set_text(t1, "SIGNAL FLOW");
    lv_obj_set_style_text_color(t1, C_TEXT, 0);
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_12, 0);

    lv_obj_t *t2 = lv_label_create(titlebar);
    lv_label_set_text(t2, "touch-drag the curves | tap a key");
    lv_obj_set_style_text_color(t2, C_MUTED, 0);
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_12, 0);

    // modules row
    lv_obj_t *modules = lv_obj_create(screen);
    lv_obj_set_flex_flow(modules, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modules, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(modules, 1);
    lv_obj_set_style_bg_opa(modules, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(modules, 0, 0);
    lv_obj_set_style_pad_hor(modules, 16, 0);
    lv_obj_set_style_pad_ver(modules, 0, 0);
    lv_obj_set_style_pad_column(modules, 10, 0);
    lv_obj_set_width(modules, LV_PCT(100));

    make_module(ui, modules, "OSCILLATOR", "polyBLEP saw", PlotKind::kOsc);
    make_arrow(modules);
    make_module(ui, modules, "FILTER", "TPT SVF | lowpass", PlotKind::kFilter);
    make_arrow(modules);
    make_module(ui, modules, "ENVELOPE", "ADSR", PlotKind::kEnv);
    make_arrow(modules);
    make_module(ui, modules, "OUTPUT", "scope", PlotKind::kOut);

    // keyboard
    make_keyboard(ui, screen);

    set_osc_readout(ui);
    set_filter_readout(ui);
    set_env_readout(ui);
    set_out_readout(ui);

    lv_timer_create(anim_timer_cb, 20, ui);
    return ui;
}
