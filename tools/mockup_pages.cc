// mockup_pages.cc -- the interaction model's pages as a design surface.
//
// Fixed content, no Panel state, no engine: this renders chrome through the
// real primitives and atlases so geometry and legibility match the panel.
// Every coordinate comes from nostromo/geom.h; there are no literal layout
// numbers below (invariant 8 of nostromo-interaction_arch-design.md).
//
//   ./mockup_pages <outdir> [scale]   render every page
//   ./mockup_pages --check            compare against the baked hashes
//
// Separate from mockup_screens.cc, which holds the pre-interaction-model
// screens and their own golden hashes. Those are superseded but not yet
// removed; this file does not touch them.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "fb.h"
#include "font.h"
#include "geom.h"
#include "palette.h"
#include "png.h"

namespace {

using spike::Color;
using spike::DrawGlyphRun;
using spike::DrawHLine;
using spike::DrawVLine;
using spike::FillRect;
using spike::FrameBuffer;
using spike::kPrimaryFont;
using spike::kSecondaryFont;

using nostromo::kBg;
using nostromo::kBright;
using nostromo::kDim;
using nostromo::kFaint;
using nostromo::kMid;

namespace g = nostromo::geom;

// ---- text --------------------------------------------------------------

void TextLeft(FrameBuffer &fb, const char *s, int x, int y, const spike::Font &f,
              Color c, int tracking = 0) {
  DrawGlyphRun(fb, x, y, s, static_cast<int>(std::strlen(s)), f, c, tracking);
}

void TextRight(FrameBuffer &fb, const char *s, int xr, int y,
               const spike::Font &f, Color c) {
  const int n = static_cast<int>(std::strlen(s));
  DrawGlyphRun(fb, xr - n * f.w, y, s, n, f, c, 0);
}

const char *kViews[3] = {"SCOPE", "CYCLE", "SPECTRUM"};
int kViewSel = 0;
const char *g_screen = "FILTER";

// Every screen name must fit the fixed title cell, or it will run into the
// patch name. Checked here because geom.h does not know the names.
constexpr const char *kScreenNames[] = {
    "PART", "FILTER", "AMPLIFIER", "MODULATION", "OSCILLATOR 2", "ENVELOPE 3",
    "LFO 3", "SCOPE", "CYCLE", "SPECTRUM", "EFFECTS", "PATCH",
    "CONFIGURATION"};
constexpr int LongestName() {
  int m = 0;
  for (const char *s : kScreenNames) {
    int n = 0;
    while (s[n]) ++n;
    if (n > m) m = n;
  }
  return m;
}
static_assert(LongestName() <= g::kTitleNameChars,
              "a screen name overruns the title cell and would collide with "
              "the patch name");

// ---- vocabulary (same treatments as mockup_screens.cc) -----------------

void Cursor(FrameBuffer &fb, int x, int y, int w, int h, Color c) {
  constexpr int kLeg = 7, kTh = 2;
  FillRect(fb, x, y, kLeg, kTh, c);
  FillRect(fb, x + w - kLeg, y, kLeg, kTh, c);
  FillRect(fb, x, y + h - kTh, kLeg, kTh, c);
  FillRect(fb, x + w - kLeg, y + h - kTh, kLeg, kTh, c);
  FillRect(fb, x, y, kTh, kLeg, c);
  FillRect(fb, x + w - kTh, y, kTh, kLeg, c);
  FillRect(fb, x, y + h - kLeg, kTh, kLeg, c);
  FillRect(fb, x + w - kTh, y + h - kLeg, kTh, kLeg, c);
}

// Column header: label block plus a rule running to the column edge.
void ColumnHeader(FrameBuffer &fb, int col, const char *label) {
  const int x = g::kColX(col);
  const int bw = static_cast<int>(std::strlen(label)) * kPrimaryFont.w + 12;
  FillRect(fb, x, g::kHeaderY, bw, g::kHeaderH - 4, kDim);
  TextLeft(fb, label, x + 6, g::kHeaderY + 1, kPrimaryFont, kBright);
  const int rx = x + bw + 2, rw = g::kColW - bw - 2 - 8;
  if (rw > 0) {
    DrawHLine(fb, rx, g::kHeaderY + g::kHeaderH - 6, rw, kDim);
    DrawHLine(fb, rx, g::kHeaderY + g::kHeaderH - 5, rw, kDim);
  }
}

// Well spans the column's usable width, so it reads as belonging to the
// column rather than sitting inside it.
// The well's edges align with the header block's left and the header rule's
// right, so the two read as one object rather than two stacked ones.
constexpr int kColPad = 6;
constexpr int kHeadEnd = 8;                    // matches ColumnHeader's rule
constexpr int kBarW = g::kColW - kHeadEnd;
constexpr int kWellGap = 6;
constexpr int kWellSegW = (kBarW - kWellGap) / 2;

void SplitWell(FrameBuffer &fb, int x, int y, int value) {
  FillRect(fb, x, y + 2, kWellSegW, 4, kDim);
  FillRect(fb, x + kWellSegW + kWellGap, y + 2, kWellSegW, 4, kDim);
  if (value == 0) {
    // Zero is a state, not a small value: it gets a different shape, not a
    // shorter bar. A full-height tick overshooting the well reads at a glance
    // and can never be mistaken for a nearly-zero fill.
    FillRect(fb, x + kWellSegW, y - 3, kWellGap, g::kWellH + 6, kBright);
  } else {
    int len = (std::abs(value) * kWellSegW + 50) / 99;
    if (len < 2) len = 2;
    if (value > 0)
      FillRect(fb, x + kWellSegW + kWellGap, y + 2, len, 4, kBright);
    else
      FillRect(fb, x + kWellSegW - len, y + 2, len, 4, kBright);
  }
  DrawVLine(fb, x, y, g::kWellH, kMid);
  DrawVLine(fb, x + kBarW - 1, y, g::kWellH, kMid);
}

// ---- pane --------------------------------------------------------------

// A pane row is a class. Rows with instances carry a strip beneath them: a
// newspaper-reflow of the instance axis, one cell per instance. The pane
// renders hierarchically and navigates flat: NAV1 walks every selectable
// subject in reading order, cells included, and skips the class labels.
struct PaneEntry {
  const char *label;
  int         instances;   // 0 = no strip
  bool        global;      // below the rule, and not owned by a part
  const char *const *cells;  // nullptr = number the cells 1..n
};

// 18 subjects, pane order. Matches SubjectId in the arch-design.
// A row is either a SUBJECT (selectable, normal weight) or a class HEADER
// (not selectable, dim) whose instances live in the strip beneath it. The two
// are never drawn alike, so "is this a page or a label?" is answered by
// weight alone.
// Singletons first, instanced classes after. Mixing the two shapes made the
// list read as arbitrary; separating them makes each block homogeneous.
// OUT's three views are not one page with an item axis: only SOURCE is
// shared, and SCALE means different units in each. They are three subjects
// under one class, which is exactly what the strip idiom already expresses.
const char *const kOutCells[3] = {"SC", "CY", "SP"};

const PaneEntry kPane[] = {
    {"PART", 0, false, nullptr},
    {"FILT", 0, false, nullptr}, {"AMP", 0, false, nullptr},
    {"MOD", 0, false, nullptr},
    {"OSC", 4, false, nullptr},
    {"ENV", 3, false, nullptr},
    {"LFO", 3, false, nullptr},
    {"OUT", 3, true, kOutCells},
    {"FX", 0, true, nullptr},
    {"PATCH", 0, true, nullptr}, {"CONF", 0, true, nullptr},
};
constexpr int kPaneN = static_cast<int>(sizeof(kPane) / sizeof(kPane[0]));

// NAV1 walks all 20 subjects in reading order — rows and strip cells alike.
// One detent, one subject. The pixel step varies; the semantic step does not.
// Which page the mockup draws, and where the pane cursor sits for it.
enum Page { kPageFilt = 0, kPageOsc, kPageMod, kPageOut, kPageCount };
int g_page = kPageFilt;

// pane row index per page, and strip cell (-1 when the row is a subject)
const int kRowFor[kPageCount]  = {1, 4, 3, 7};   // FILT, OSC, MOD, OUT
const int kCellFor[kPageCount] = {-1, 1, -1, -1};
int kSelRow = 1;
int kSelCell = -1;
constexpr int kActivePart = 0;


// Instance strip: n cells across the pane width, separated by rules. The
// active cell is a filled band, matching the class row's treatment one level
// down.
// Instance strip. `sel` is the focused cell, or -1 when focus is elsewhere:
// a class you are not on shows no highlight at all, because remembered state
// competing with focus is what made v6 read as five cursors.
// Cells begin where the label begins, so the first digit sits directly under
// the first letter. The rail (drawn by the caller) runs down the left of both.


void DrawStrip(FrameBuffer &fb, int y, int n, int sel,
               const char *const *cells) {
  // Fixed cell pitch, not a division of the available width: with a division,
  // a 4-cell strip and a 3-cell strip put their digits at different
  // intervals, and the columns of digits refuse to line up between classes.
  int chars = 1;
  if (cells)
    for (int k = 0; k < n; ++k) {
      const int len = static_cast<int>(std::strlen(cells[k]));
      if (len > chars) chars = len;
    }
  const int cw = g::StripCellW(chars);
  for (int k = 0; k < n; ++k) {
    const int cx = g::kPaneX + g::kStripX0 + k * cw;
    char num[2] = {static_cast<char>('1' + k), 0};
    const char *d = cells ? cells[k] : num;
    const int dw = static_cast<int>(std::strlen(d)) * kPrimaryFont.w;
    const int dx = cx + 2, dy = y + (g::kStripH - kPrimaryFont.h) / 2;
    // The highlight is the digit's own cell, not the whole column: the cell
    // is spacing, the digit is the subject.
    if (k == sel)
      FillRect(fb, dx - 2, dy - 1, dw + 4, kPrimaryFont.h + 2, kDim);
    TextLeft(fb, d, dx, dy, kPrimaryFont, k == sel ? kBright : kMid);
    // No separator rules: the rail already says these cells are one object,
    // and the rules made the spacing read as ragged.
  }
}

// Part swatches: four marks that read as hardware, not as a cursor. The
// active one is filled with its hue, the rest outlined. Selection is by
// button, so nothing here should look navigable.
// Part swatches, now in the title bar where width is free: a row of four
// tall marks. Tall enough to read as hardware indicators rather than cells,
// and the row makes "four parts" legible at a glance.
int DrawPartSwatches(FrameBuffer &fb, int x0, int y, int active) {
  constexpr Color kHue[4] = {
      spike::Rgb565(120, 200, 255), spike::Rgb565(255, 190, 90),
      spike::Rgb565(190, 140, 255), spike::Rgb565(120, 255, 170)};
  constexpr int kSw = 8, kSh = 16, kGap = 4;
  for (int k = 0; k < 4; ++k) {
    const int x = x0 + k * (kSw + kGap);
    if (k == active) {
      FillRect(fb, x, y, kSw, kSh, kHue[k]);
    } else {
      DrawHLine(fb, x, y, kSw, kFaint);
      DrawHLine(fb, x, y + kSh - 1, kSw, kFaint);
      DrawVLine(fb, x, y, kSh, kFaint);
      DrawVLine(fb, x + kSw - 1, y, kSh, kFaint);
    }
  }
  return x0 + 4 * (kSw + kGap);
}

void DrawPane(FrameBuffer &fb) {
  const int tx = g::kPaneX + g::kLabelX;
  int y = g::kPaneY;
  bool rule_drawn = false;

  for (int i = 0; i < kPaneN; ++i) {
    if (kPane[i].global && !rule_drawn) {
      DrawHLine(fb, g::kPaneX, y + 4, g::kPaneW - 8, kDim);
      y += g::kPaneRule + 6;
      rule_drawn = true;
    }
    const bool header = kPane[i].instances > 0;

    if (header) {
      // A class label and its instances are one object, joined by a rail down
      // their left edge. Same atlas and same weight as a subject: the rail and
      // the strip say "this owns the cells below", not a smaller font.
      const int hdr_h = g::kPanePitch - 6;
      TextLeft(fb, kPane[i].label, tx, y, kPrimaryFont, kMid);
      DrawStrip(fb, y + hdr_h, kPane[i].instances,
                i == kSelRow ? kSelCell : -1, kPane[i].cells);
      DrawVLine(fb, g::kPaneX + 1, y, hdr_h + g::kStripH, kMid);
      y += hdr_h + g::kStripH + 8;
      continue;
    }

    const int ty = y + (g::kPanePitch - kPrimaryFont.h) / 2;
    const bool sel = (i == kSelRow);
    if (sel) {
      FillRect(fb, g::kPaneX, y + 1, g::kPaneW - 8, g::kPanePitch - 2, kDim);
      TextLeft(fb, kPane[i].label, tx, ty, kPrimaryFont, kBright);
    } else {
      TextLeft(fb, kPane[i].label, tx, ty, kPrimaryFont, kMid);
    }
    y += g::kPanePitch;
  }
}

// ---- plot --------------------------------------------------------------

void DrawFilterPlot(FrameBuffer &fb) {
  const int x = g::kPlotX, y = g::kPlotY, w = g::kPlotW, h = g::kPlotH;

  // Graticule cells are visually square: pixels are kAspect wider than tall,
  // so a cell w px wide reads square when w = h / kAspect.
  const int rows = 6;
  const int cell_h = h / rows;
  const int cell_w = static_cast<int>(cell_h / g::kAspect + 0.5f);
  for (int i = 1; i < rows; ++i)
    DrawHLine(fb, x, y + i * cell_h, w, kFaint);
  for (int cx = x + cell_w; cx < x + w; cx += cell_w)
    DrawVLine(fb, cx, y, h, kFaint);
  DrawHLine(fb, x, y + h - 1, w, kDim);
  DrawVLine(fb, x, y, h, kDim);

  // A 2-pole lowpass magnitude sweep, plotted as one polyline.
  const double fc = 0.42;   // normalised corner along the x axis
  const double q = 1.9;
  std::vector<spike::Point> pts;
  pts.reserve(w);
  for (int px = 0; px < w; ++px) {
    const double u = static_cast<double>(px) / (w - 1);
    const double r = std::pow(10.0, (u - fc) * 2.2);  // f / fc, log-ish
    const double mag = 1.0 / std::sqrt(std::pow(1.0 - r * r, 2.0) +
                                       std::pow(r / q, 2.0));
    double db = 20.0 * std::log10(mag);
    if (db > 18.0) db = 18.0;
    if (db < -30.0) db = -30.0;
    const int py = y + static_cast<int>((18.0 - db) / 48.0 * (h - 2));
    pts.push_back({x + px, py});
  }
  spike::DrawPolyline(fb, pts.data(), static_cast<int>(pts.size()), kBright);

  TextLeft(fb, "+18dB", x + 6, y + 4, kSecondaryFont, kDim);
  TextLeft(fb, "20Hz", x + 6, y + h - kSecondaryFont.h - 4, kSecondaryFont, kDim);
  TextRight(fb, "20k", x + w - 6, y + h - kSecondaryFont.h - 4, kSecondaryFont,
            kDim);
}

void DrawOscPlot(FrameBuffer &fb) {
  const int x = g::kPlotX, y = g::kPlotY, w = g::kPlotW, h = g::kPlotH;
  const int rows = 6, cell_h = h / rows;
  const int cell_w = static_cast<int>(cell_h / g::kAspect + 0.5f);
  for (int i = 1; i < rows; ++i) DrawHLine(fb, x, y + i * cell_h, w, kFaint);
  for (int cx = x + cell_w; cx < x + w; cx += cell_w)
    DrawVLine(fb, cx, y, h, kFaint);
  DrawHLine(fb, x, y + h / 2, w, kDim);
  DrawVLine(fb, x, y, h, kDim);

  // Three sawtooth ramps: hard vector segments, the shape the engine makes.
  const int cyc = w / 3, top = y + 10, bot = y + h - 10;
  std::vector<spike::Point> pts;
  for (int k = 0; k < 3; ++k) {
    pts.push_back({x + k * cyc, bot});
    pts.push_back({x + (k + 1) * cyc - 1, top});
    pts.push_back({x + (k + 1) * cyc - 1, bot});
  }
  spike::DrawPolyline(fb, pts.data(), static_cast<int>(pts.size()), kBright);
  TextLeft(fb, "SAW  440Hz", x + 6, y + 4, kSecondaryFont, kDim);
}

void DrawScope(FrameBuffer &fb) {
  const int x = g::kPlotX, y = g::kPlotY, w = g::kPlotW, h = g::kPlotH;
  const int rows = 6, cell_h = h / rows;
  const int cell_w = static_cast<int>(cell_h / g::kAspect + 0.5f);
  for (int i = 1; i < rows; ++i) DrawHLine(fb, x, y + i * cell_h, w, kFaint);
  for (int cx = x + cell_w; cx < x + w; cx += cell_w)
    DrawVLine(fb, cx, y, h, kFaint);
  DrawHLine(fb, x, y + h / 2, w, kDim);   // zero line
  DrawVLine(fb, x, y, h, kDim);

  // Vertical scale: without labelled divisions the trace's height means
  // nothing, and every other page's plot is self-describing.
  const char *lab[3] = {"+1.0", "0", "-1.0"};
  const int ly[3] = {y + 2, y + h / 2 - kSecondaryFont.h - 2,
                     y + h - kSecondaryFont.h - 2};
  for (int i = 0; i < 3; ++i)
    TextLeft(fb, lab[i], x + 4, ly[i], kSecondaryFont, kDim);

  // Trigger marker: a full-height rule, not a stub lost in the graticule.
  const int tx = x + cell_w;
  DrawVLine(fb, tx, y, h, kMid);
  FillRect(fb, tx - 3, y + h / 2 - 3, 7, 7, kBright);

  const int mid = y + h / 2, amp = h / 2 - 24;
  std::vector<spike::Point> pts;
  pts.reserve(w);
  for (int px = 0; px < w; ++px) {
    const double u = (px - (tx - x)) * 2.0 * 3.14159265 * 3.0 / w;
    const double v = 0.62 * std::sin(u) + 0.26 * std::sin(2 * u + 0.7) +
                     0.12 * std::sin(3 * u + 1.9);
    pts.push_back({x + px, mid - static_cast<int>(v * amp)});
  }
  spike::DrawPolyline(fb, pts.data(), static_cast<int>(pts.size()), kBright);

  TextRight(fb, "-6.2dBFS  PEAK -1.8", x + w - 6, y + 4, kSecondaryFont, kDim);
  TextRight(fb, "2.0ms/div", x + w - 6, y + h - kSecondaryFont.h - 4,
            kSecondaryFont, kDim);
}

void DrawSpectrum(FrameBuffer &fb) {
  const int x = g::kPlotX, y = g::kPlotY, w = g::kPlotW, h = g::kPlotH;
  const int rows = 6, cell_h = h / rows;
  const int cell_w = static_cast<int>(cell_h / g::kAspect + 0.5f);
  for (int i = 1; i < rows; ++i) DrawHLine(fb, x, y + i * cell_h, w, kFaint);
  for (int cx = x + cell_w; cx < x + w; cx += cell_w)
    DrawVLine(fb, cx, y, h, kFaint);
  DrawHLine(fb, x, y + h - 1, w, kDim);
  DrawVLine(fb, x, y, h, kDim);
  const char *lab[3] = {"0dB", "-36", "-72"};
  const int ly[3] = {y + 2, y + h / 2 - kSecondaryFont.h / 2,
                     y + h - kSecondaryFont.h - 2};
  for (int i = 0; i < 3; ++i)
    TextLeft(fb, lab[i], x + 4, ly[i], kSecondaryFont, kDim);

  // Harmonic series with a falling envelope, drawn as bins.
  for (int k = 1; k <= 24; ++k) {
    const double f = std::log10(k * 1.0) / std::log10(28.0);
    const int bx = x + 48 + static_cast<int>(f * (w - 72));
    double a = 1.0 / (k * 0.9);
    if (k % 2 == 0) a *= 0.45;
    const int bh = static_cast<int>(a * (h - 20));
    if (bh < 2) continue;
    FillRect(fb, bx, y + h - bh, 3, bh, kBright);
  }
  TextRight(fb, "20Hz - 20kHz", x + w - 6, y + 4, kSecondaryFont, kDim);
}

void DrawPlot(FrameBuffer &fb) {
  if (g_page == kPageOsc) DrawOscPlot(fb);
  else if (g_page == kPageOut)
    (kViewSel == 2) ? DrawSpectrum(fb) : DrawScope(fb);
  else DrawFilterPlot(fb);
}

// MOD page: an edge list of active slots. Five columns, one row per route,
// NAV2 walking rows. No plot, no wells — the columns are route fields.
struct Route { const char *src, *dst, *amt, *crv, *en; };
const Route kRoutes[] = {
    {"LFO2", "OSC3.PIT", "-030", "LIN", "ON"},
    {"ENV3", "FILT.CUT", "+048", "EXP", "ON"},
    {"VEL", "AMP", "+100", "LIN", "ON"},
    {"MODW", "LFO2.RTE", "+064", "LIN", "ON"},
    {"KEY", "FILT.CUT", "+035", "LIN", "OFF"},
    {"ENV2", "OSC2.SHP", "-020", "EXP", "ON"},
    {"LFO1", "OSC1.PIT", "-012", "LIN", "ON"},
    {"LFO1", "OSC2.PIT", "+012", "LIN", "ON"},
    {"PBND", "OSC1.PIT", "+002", "LIN", "ON"},
    {"AT", "FILT.CUT", "+018", "LIN", "ON"},
    {"", "", "", "", ""},
};
constexpr int kNRoutes = static_cast<int>(sizeof(kRoutes) / sizeof(kRoutes[0]));
constexpr int kSelRoute = 1;

void DrawModRows(FrameBuffer &fb) {
  for (int r = 0; r < kNRoutes; ++r) {
    const int y = g::kListY + r * g::kRowPitch;
    if (y + g::kRowPitch > g::kBottom) break;
    const bool sel = (r == kSelRoute);
    // The row cursor is level 2 of the ladder — corner brackets, transient,
    // moved by NAV2. The filled band is level 1 and belongs to the pane's
    // selected subject; using it here made two different things look alike.
    const int span = g::kColX(g::kColumns - 1) + g::kColW - 8 - g::kPlotX;
    // Corner brackets do not read across 880 px — the corners are too far
    // apart to group anything. A row cursor is an outline: unmistakably
    // weaker than the filled band of level 1, but legible at any width.
    if (sel) {
      const int ry = y - 3, rh = g::kRowPitch - 2;
      DrawHLine(fb, g::kPlotX, ry, span, kMid);
      DrawHLine(fb, g::kPlotX, ry + rh, span, kMid);
      DrawVLine(fb, g::kPlotX, ry, rh, kMid);
      DrawVLine(fb, g::kPlotX + span - 1, ry, rh, kMid);
    }
    const Color c = sel ? kBright : kMid;
    // No slot index: the slot is engine storage, not something to read, and
    // a number in column 0 would sit under the SOURCE header driving it.
    const char *f[5] = {kRoutes[r].src, kRoutes[r].dst, kRoutes[r].amt,
                        kRoutes[r].crv, kRoutes[r].en};
    for (int c2 = 0; c2 < 5; ++c2) {
      const char *s = f[c2][0] ? f[c2] : "--";
      // The trailing empty row is the only way to add a route, so it is an
      // affordance, not an unassigned value. kFaint is correct for "nothing
      // here" and wrong for "turn this to make something": at kFaint nobody
      // finds it. Empties in an edge list are always trailing, so every
      // empty row is the add target.
      TextLeft(fb, s, g::kColX(c2) + 6, y, kPrimaryFont,
               f[c2][0] ? c : kDim);
    }
  }
}

// ---- page --------------------------------------------------------------

struct ColumnContent {
  const char *header;
  const char *value;
  int         well;  // -99..99
};

const ColumnContent kColsFilt[g::kColumns] = {
    {"CUTOFF", "2.40k", 34}, {"RESO", "0.31", 18}, {"ENVAMT", "+48", 48},
    {"DRIVE", "1.20", 22},   {"KEYTRK", "50%", 0},
};
const ColumnContent kColsOsc[g::kColumns] = {
    {"WAVE", "SAW", 0},   {"COARSE", "+12", 50}, {"FINE", "-07", -14},
    {"LEVEL", "098", 88}, {"SHAPE", "0.42", 42},
};
// VIEW is not a column: it is this page's item axis, drawn as a strip below
// the values. The five columns are the selected view's settings, so the page
// needs no value-dependent column list — the same shape MOD already has.
// One column set per view. NAV2 selects the view; the five headers and their
// values change with it. SOURCE and SCALE are shared, the middle three are not.
const ColumnContent kColsOutV[3][g::kColumns] = {
    {{"SOURCE", "MASTER", 0}, {"TIMEBASE", "2.0ms", 0}, {"SCALE", "0dB", 0},
     {"TRIGGER", "RISE", 0},  {"HOLD", "OFF", 0}},
    {{"SOURCE", "MASTER", 0}, {"CYCLES", "3", 0}, {"SCALE", "0dB", 0},
     {"ALIGN", "ZERO", 0},    {"HOLD", "OFF", 0}},
    {{"SOURCE", "MASTER", 0}, {"RANGE", "20k", 0}, {"SCALE", "-72dB", 0},
     {"AVERAGE", "4", 0},     {"WINDOW", "HANN", 0}},
};
const ColumnContent *kColsOut = kColsOutV[0];


// The view strip: three cells across the content width, in the band the
// wells and route summary occupy on other pages.
void DrawViewStrip(FrameBuffer &fb) {
  const int x0 = g::kPlotX;
  const int w = g::kColX(g::kColumns - 1) + g::kColW - 8 - x0;
  const int n = 3, cw = w / n;
  const int y = g::kWellY - 4, h = 26;
  for (int k = 0; k < n; ++k) {
    const int cx = x0 + k * cw;
    const int tw = static_cast<int>(std::strlen(kViews[k])) * kPrimaryFont.w;
    if (k == kViewSel) FillRect(fb, cx, y, tw + 12, h, kDim);
    TextLeft(fb, kViews[k], cx + 6, y + (h - kPrimaryFont.h) / 2,
             kPrimaryFont, k == kViewSel ? kBright : kMid);
  }
  DrawHLine(fb, x0, y + h + 4, w, kFaint);
}
const ColumnContent kColsMod[g::kColumns] = {
    {"SOURCE", "", 0}, {"DEST", "", 0}, {"AMOUNT", "", 0},
    {"CURVE", "", 0},  {"ENABLE", "", 0},
};
const ColumnContent *kCols = kColsFilt;
int g_groups = 1, g_group = 0;

// Inbound routes, shown in edit mode so "what modulates this" needs no mode.
// Two lines; overflow is a count, and the full list is one push deeper.
const char *kSumFilt[g::kColumns][g::kSumLines] = {
    {"<-ENV2 +48", "<-LFO1 -12"}, {nullptr, nullptr}, {nullptr, nullptr},
    {"<-VEL  +20", nullptr},      {nullptr, nullptr},
};
const char *kSumOsc[g::kColumns][g::kSumLines] = {
    {nullptr, nullptr}, {"<-LFO2 -30", nullptr}, {nullptr, nullptr},
    {"<-ENV1 +99", nullptr}, {"<-LFO3 +18", "<-MODW +40"},
};
const char *kSumNone[g::kColumns][g::kSumLines] = {
    {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr},
    {nullptr, nullptr}, {nullptr, nullptr},
};
const char *(*kSummary)[g::kSumLines] = kSumFilt;
// Routes beyond the two-line budget collapse to a count; the full list is one
// push deeper. Column 0 stands in for the overflow case.
const char *kMoreFilt[g::kColumns] = {"(+3)", nullptr, nullptr, nullptr, nullptr};
const char *kMoreNone[g::kColumns] = {nullptr, nullptr, nullptr, nullptr, nullptr};
const char **kMore = kMoreFilt;

void DrawPage(FrameBuffer &fb) {
  FillRect(fb, 0, 0, g::kFbWidth, g::kFbHeight, kBg);

  // Title bar.
  FillRect(fb, g::kTitleX, g::kTitleY, g::kTitleW, g::kTitleH - 4, kDim);
  // A global subject is not owned by a part, so the part indicator says so
  // rather than showing a part that has nothing to do with what is on screen.
  const bool global = kPane[kSelRow].global;
  const int nx = DrawPartSwatches(fb, g::kTitleX + 8, g::kTitleY + 3,
                                  global ? -1 : kActivePart);
  char pn[3] = {'P', static_cast<char>('1' + kActivePart), 0};
  if (global) { pn[0] = 'G'; pn[1] = 'L'; }
  TextLeft(fb, pn, nx + 4, g::kTitleY + 2, kPrimaryFont,
           global ? kMid : kBright);

  // The navigator/page boundary runs the full height of the screen. In the
  // title bar the field is filled, so the same rule is cut out of it rather
  // than drawn onto it — one gesture, inverted.
  const int gx = g::kPaneX + g::kPaneW - 5;
  DrawVLine(fb, gx, g::kTitleY, g::kTitleH - 4, kBg);
  DrawVLine(fb, gx + 1, g::kTitleY, g::kTitleH - 4, kBg);

  // The screen's name in full. With it here, the pane's abbreviated cells
  // (SC CY SP) are a position indicator rather than a code to memorise: the
  // expansion is always on screen.
  TextLeft(fb, g_screen, g::kTitleNameX, g::kTitleY + 2, kPrimaryFont,
           kBright);
  // A cut in the filled bar, matching the navigator boundary: the same
  // gesture, inverted, separating the screen's name from the patch's.
  DrawVLine(fb, g::kTitleSepX, g::kTitleY, g::kTitleH - 4, kBg);
  DrawVLine(fb, g::kTitleSepX + 1, g::kTitleY, g::kTitleH - 4, kBg);
  TextLeft(fb, "GLASS BELLS MK2", g::kTitlePatchX, g::kTitleY + 2,
           kPrimaryFont, kMid);
  int rx = g::kTitleX + g::kTitleW - 8;
  TextRight(fb, "A007", rx, g::kTitleY + 2, kPrimaryFont, kMid);
  if (g_groups > 1) {
    // Column groups only announce themselves when there is more than one.
    char gs[24];  // sized past what the compiler can prove about %d
    std::snprintf(gs, sizeof(gs), "GROUP %d/%d", g_group + 1, g_groups);
    rx -= 4 * kPrimaryFont.w + 16;
    TextRight(fb, gs, rx, g::kTitleY + 2, kPrimaryFont, kBright);
  }

  DrawPane(fb);
  // The navigator and the page are different kinds of thing; the gutter says
  // so. A rule plus clear space reads as a boundary, a rule alone as chrome.
  DrawVLine(fb, g::kPaneX + g::kPaneW - 5, g::kPaneY, g::kPaneH, kDim);
  DrawVLine(fb, g::kPaneX + g::kPaneW - 4, g::kPaneY, g::kPaneH, kDim);

  for (int c = 0; c < g::kColumns; ++c) {
    ColumnHeader(fb, c, kCols[c].header);
    if (g_page == kPageMod) continue;
    const int x = g::kColX(c);
    // No column cursor: in edit mode every encoder is live at once, so there
    // is nothing to focus. focus_col exists only in mod view.
    TextLeft(fb, kCols[c].value, x + kColPad, g::kValueY, kPrimaryFont, kMid);
    if (g_page != kPageOut) SplitWell(fb, x, g::kWellY, kCols[c].well);
    for (int l = 0; l < g::kSumLines; ++l) {
      if (!kSummary[c][l]) continue;
      TextLeft(fb, kSummary[c][l], x + kColPad, g::kSumY + l * g::kSumPitch,
               kSecondaryFont, kDim);
    }
    if (kMore[c]) {
      int last = -1;
      for (int l = 0; l < g::kSumLines; ++l)
        if (kSummary[c][l]) last = l;
      const int lx = x + kColPad +
                     (last >= 0 ? static_cast<int>(std::strlen(kSummary[c][last]))
                                : 0) * kSecondaryFont.w + kSecondaryFont.w;
      TextLeft(fb, kMore[c], lx,
               g::kSumY + (last < 0 ? 0 : last) * g::kSumPitch,
               kSecondaryFont, kDim);
    }
  }

  if (g_page == kPageMod) {
    DrawModRows(fb);
    return;
  }
  DrawPlot(fb);
}



struct PageShot {
  const char   *name;
  int           page;
  int           view;   // OUT only
  std::uint32_t hash;   // FNV-1a over the RGB565 buffer
};

// Golden hashes. They change whenever the drawing changes, which is the
// point: they catch a primitive or atlas change silently altering the pages.
const PageShot kShots[] = {
    {"mockup_filt.png", kPageFilt, 0, 0xFA68E0B4u},
    {"mockup_osc.png", kPageOsc, 0, 0x83AE72E3u},
    {"mockup_mod.png", kPageMod, 0, 0x0F4E2148u},
    {"mockup_out_scope.png", kPageOut, 0, 0x578C3FFBu},
    {"mockup_out_cycle.png", kPageOut, 1, 0x61F8BAF2u},
    {"mockup_out_spec.png", kPageOut, 2, 0xBB68B63Eu},
};
constexpr int kNShots = static_cast<int>(sizeof(kShots) / sizeof(kShots[0]));

std::uint32_t Fnv1a(const std::vector<std::uint16_t> &b) {
  std::uint32_t h = 2166136261u;
  for (std::uint16_t v : b) {
    h = (h ^ (v & 0xFF)) * 16777619u;
    h = (h ^ (v >> 8)) * 16777619u;
  }
  return h;
}

void Select(const PageShot &s) {
  g_page = s.page;
  kSelRow = kRowFor[s.page];
  kSelCell = kCellFor[s.page];
  kViewSel = s.view;
  if (s.page == kPageFilt) {
    kCols = kColsFilt; kSummary = kSumFilt; kMore = kMoreFilt;
    g_groups = 2; g_group = 0; g_screen = "FILTER";
  } else if (s.page == kPageOsc) {
    kCols = kColsOsc; kSummary = kSumOsc; kMore = kMoreNone;
    g_groups = 2; g_group = 0; g_screen = "OSCILLATOR 2";
  } else if (s.page == kPageMod) {
    kCols = kColsMod; kSummary = kSumNone; kMore = kMoreNone;
    g_groups = 1; g_group = 0; g_screen = "MODULATION";
  } else {
    kSelCell = s.view;
    kCols = kColsOutV[s.view]; kSummary = kSumNone; kMore = kMoreNone;
    g_groups = 1; g_group = 0; g_screen = kViews[s.view];
  }
}

void Render(const PageShot &s, std::vector<std::uint16_t> &buf) {
  Select(s);
  buf.assign(static_cast<std::size_t>(g::kFbWidth) * g::kFbHeight, kBg);
  FrameBuffer fb{buf.data(), g::kFbWidth, g::kFbHeight, g::kFbWidth,
                 {0, 0, g::kFbWidth, g::kFbHeight}};
  DrawPage(fb);
}

}  // namespace

int main(int argc, char **argv) {
  const std::string arg1 = (argc > 1) ? argv[1] : ".";

  if (arg1 == "--check") {
    int bad = 0;
    for (int i = 0; i < kNShots; ++i) {
      std::vector<std::uint16_t> buf;
      Render(kShots[i], buf);
      const std::uint32_t h = Fnv1a(buf);
      const bool ok = (h == kShots[i].hash);
      std::printf("%-24s 0x%08X %s\n", kShots[i].name, h,
                  ok ? "ok" : "MISMATCH (update the table if intended)");
      if (!ok) ++bad;
    }
    if (bad) std::printf("\n%d page(s) changed\n", bad);
    return bad ? 1 : 0;
  }

  const int scale = (argc > 2) ? std::atoi(argv[2]) : 1;
  for (int i = 0; i < kNShots; ++i) {
    std::vector<std::uint16_t> buf;
    Render(kShots[i], buf);
    FrameBuffer fb{buf.data(), g::kFbWidth, g::kFbHeight, g::kFbWidth,
                   {0, 0, g::kFbWidth, g::kFbHeight}};
    const std::string path = arg1 + "/" + kShots[i].name;
    if (spike::WriteFramePng(fb, path.c_str(), scale))
      std::printf("wrote %s (%dx%d)\n", path.c_str(), g::kFbWidth * scale,
                  g::kFbHeight * scale);
    else
      std::fprintf(stderr, "cannot write %s\n", path.c_str());
  }
  return 0;
}
