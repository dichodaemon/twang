// mockup_screens.cc — all four panel screens, drawn with the real spike
// primitives and the real Terminus atlases.
//
// These are mockups, not panel code: they take no Panel state and draw fixed
// content. But every pixel goes through FillRect/DrawHLine/DrawVLine/
// DrawGlyphRun and the shipped 10x20 and 8x14 atlases, so the geometry,
// density and legibility are exactly what the panel will produce. Browser
// renders with a substitute font are not.
//
// Signal flow is included even though nostromo/panel.cc already draws it live.
// Two reasons: it is itself a prototype and belongs with the others as a
// design surface, and having all four screens expressed once as straight-line
// primitive calls gives a reference set to validate an opcode/descriptor
// encoding against (see engine-recommendations.md section 5).
//
//   ./mockup_screens --check      render all four, compare against the baked
//                                 hashes, exit non-zero on mismatch
//
// Build (add next to panel_shot in CMakeLists):
//   add_executable(mockup_screens tools/mockup_screens.cc)
//   target_link_libraries(mockup_screens PRIVATE spike)
//
// Usage:
//   ./mockup_screens <outdir> [scale]
//   -> <outdir>/mockup_signal.png, mockup_matrix.png, mockup_patch.png,
//      mockup_save.png

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "fb.h"
#include "font.h"
#include "mockup_screens.h"
#include "palette.h"
#include "png.h"
#include "screens.h"

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

// ---- layout (matches nostromo/panel.cc where the screens overlap) ---------

constexpr int kFrameW = 1024, kFrameH = 600;
constexpr int kTitleX = 16;
constexpr int kBlockY = 88;              // top of the content blocks
constexpr int kBlockH = 388;             // shared, so corner brackets align
constexpr int kLeftX = 16, kLeftW = 598;
constexpr int kRightX = 652, kRightW = 356;

// Signal-flow module geometry, mirroring nostromo/panel.cc exactly.
constexpr int kModY = 84, kModW = 242;
constexpr int kPlotDX = 6, kPlotDY = 58, kPlotW = 230, kPlotH = 232;
constexpr int kReadoutX = kModW - 6, kReadoutY = kModY + 306;
constexpr int kPx0[4] = {16, 266, 516, 766};

// ---- text helpers ------------------------------------------------------

void TextLeft(FrameBuffer &fb, const char *s, int x, int y, const spike::Font &f,
              Color c, int tracking = 0) {
  DrawGlyphRun(fb, x, y, s, static_cast<int>(std::strlen(s)), f, c, tracking);
}

void TextRight(FrameBuffer &fb, const char *s, int xr, int y, const spike::Font &f,
               Color c) {
  const int n = static_cast<int>(std::strlen(s));
  DrawGlyphRun(fb, xr - n * f.w, y, s, n, f, c, 0);
}

void TextCenter(FrameBuffer &fb, const char *s, int cx, int y, const spike::Font &f,
                Color c) {
  const int n = static_cast<int>(std::strlen(s));
  DrawGlyphRun(fb, cx - n * f.w / 2, y, s, n, f, c, 0);
}

// ---- component vocabulary (the DYN content's shared helpers) -----------

// Focus cursor: four corner marks around a cell. Level 2 of the emphasis
// ladder -- transient, moves with the encoder.
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

// Header treatment: a single label above a field or panel.
void BlockTail(FrameBuffer &fb, int x, int y, int w, const char *label) {
  const int bw = static_cast<int>(std::strlen(label)) * kPrimaryFont.w + 12;
  FillRect(fb, x, y, bw, 22, kDim);
  TextLeft(fb, label, x + 6, y + 1, kPrimaryFont, kBright);
  DrawHLine(fb, x + bw + 6, y + 20, w - bw - 6, kDim);
  DrawHLine(fb, x + bw + 6, y + 21, w - bw - 6, kDim);
}

// Bipolar split well: two dim wells separated by a gap, filled outward from
// the gap by sign. The gap guarantees a minimum separation, so the smallest
// positive and smallest negative can never be confused; it also gives zero a
// representation distinct from "unassigned".
constexpr int kWellW = 22, kWellGap = 6, kBarW = 2 * kWellW + kWellGap;

void SplitWell(FrameBuffer &fb, int x, int y, int value) {
  FillRect(fb, x, y + 2, kWellW, 4, kDim);
  FillRect(fb, x + kWellW + kWellGap, y + 2, kWellW, 4, kDim);
  if (value == 0) {
    FillRect(fb, x + kWellW, y + 2, kWellGap, 4, kBright);
  } else {
    int len = (std::abs(value) * kWellW + 50) / 99;
    if (len < 2) len = 2;
    if (value > 0)
      FillRect(fb, x + kWellW + kWellGap, y + 2, len, 4, kBright);
    else
      FillRect(fb, x + kWellW - len, y + 2, len, 4, kBright);
  }
  DrawVLine(fb, x, y, 8, kMid);                 // bracketed well limits
  DrawVLine(fb, x + kBarW - 1, y, 8, kMid);
}

}  // namespace  (helpers above stay private)

namespace mockup {

// =======================================================================
// Screen 0 — signal flow
// =======================================================================
//
// Draws the same static-chrome descriptor the panel interprets, so the chrome
// is identical by construction (one description, two consumers). The canned
// plot content below stands in for the live engine state.

void Readout(FrameBuffer &fb, int idx, const char *l0, const char *l1, Color c) {
  FillRect(fb, kPx0[idx] + kPlotDX, kReadoutY, kReadoutX - kPlotDX,
           2 * kPrimaryFont.h + 2, kBg);
  TextRight(fb, l0, kPx0[idx] + kReadoutX, kReadoutY, kPrimaryFont, c);
  if (l1)
    TextRight(fb, l1, kPx0[idx] + kReadoutX, kReadoutY + kPrimaryFont.h + 2,
              kPrimaryFont, c);
}

void DrawSignal(FrameBuffer &fb) {
  // Static chrome from the descriptor — byte-identical to the panel by
  // construction (one description, two consumers). The DYN rects land in
  // `dyn`; the canned plot content below uses the same plot origins.
  spike::DynSlot dyn[nostromo::kNumSlots] = {};
  spike::Interpret(nostromo::SignalScreen(), fb,
                   nostromo::MakeCtx(dyn, nostromo::kNumSlots));

  const int gx0 = kPx0[0] + kPlotDX, gy = kModY + kPlotDY;

  // Oscillator: three sawtooth ramps, hard vector segments.
  {
    const spike::Point pts[8] = {{gx0 + 4, gy + 206}, {gx0 + 70, gy + 18},
                                 {gx0 + 70, gy + 206}, {gx0 + 136, gy + 18},
                                 {gx0 + 136, gy + 206}, {gx0 + 202, gy + 18},
                                 {gx0 + 202, gy + 206}, {gx0 + 226, gy + 116}};
    spike::DrawPolyline(fb, pts, 8, kBright);
    Readout(fb, 0, "440Hz", nullptr, kBright);
  }

  // Filter: magnitude response with a bracket cursor at the cutoff.
  {
    const int x = kPx0[1] + kPlotDX;
    TextLeft(fb, "0dB", x + 3, gy + 14, kSecondaryFont, kMid);
    TextLeft(fb, "20Hz", x + 3, gy + 202, kSecondaryFont, kMid);
    TextRight(fb, "20k", x + 227, gy + 202, kSecondaryFont, kMid);
    const spike::Point pts[5] = {{x + 6, gy + 52},   {x + 128, gy + 52},
                                 {x + 166, gy + 70}, {x + 190, gy + 108},
                                 {x + 204, gy + 152}};
    spike::DrawPolyline(fb, pts, 5, kBright);
    DrawVLine(fb, x + 198, gy, 8, kMid);
    DrawVLine(fb, x + 198, gy + kPlotH - 8, 8, kMid);
    Cursor(fb, x + 186, gy + 112, 26, 40, kBright);
    Readout(fb, 1, "20.00kHz", "RES 0%", kBright);
  }

  // Envelope: ADSR with three draggable node cursors.
  {
    const int x = kPx0[2] + kPlotDX;
    const spike::Point pts[6] = {{x + 6, gy + 212},   {x + 30, gy + 18},
                                 {x + 92, gy + 108},  {x + 148, gy + 108},
                                 {x + 172, gy + 142}, {x + 212, gy + 208}};
    spike::DrawPolyline(fb, pts, 6, kBright);
    Cursor(fb, x + 18, gy + 6, 26, 26, kBright);
    Cursor(fb, x + 80, gy + 96, 26, 26, kBright);
    Cursor(fb, x + 200, gy + 196, 26, 26, kBright);
    Readout(fb, 2, "A 265ms  D 475ms", "S 46%  R 250ms", kBright);
  }

  // Output: silent scope trace.
  {
    const int x = kPx0[3] + kPlotDX;
    DrawHLine(fb, x + 4, gy + 116, kPlotW - 8, kBright);
    DrawHLine(fb, x + 4, gy + 117, kPlotW - 8, kBright);
    Readout(fb, 3, "NO SIGNAL", nullptr, kMid);
  }

  // Mode buttons (canned SCOPE active) — the descriptor reserved the rect.
  nostromo::DrawModeButtons(fb, dyn[nostromo::kSlotMode].rect, 0);
}

// =======================================================================
// Screen 1 — modulation matrix
// =======================================================================
//
// A monospace grid needs no grid lines: columns align because every glyph
// advances 10 px, so 96 cells cost zero rules. A conventional matrix would
// want 8 vertical dividers at ~340 px each -- about 2,700 strided writes for
// decoration.

void DrawMatrix(FrameBuffer &fb) {
  constexpr int kMk = 12, kSrc = 90, kCol = 62, kRowH = 32, kRows = 11;
  constexpr int kCursorRow = 3, kCursorCol = 1;

  struct Row { const char *src; int v[8]; };  // -128 == unassigned
  static const Row kRowData[kRows] = {
      {"LFO1",   {  12, -128, -128, -128, -128,   40, -128, -128}},
      {"LFO2",   {-128, -128, -128, -128,   28, -128, -128, -128}},
      {"ENV1",   {-128, -128, -128,   99, -128, -128, -128, -128}},
      {"ENV2",   {-128,   64, -128, -128, -128, -128, -128, -128}},
      {"ENV3",   {-128, -128, -128, -128, -128, -128,   31, -128}},
      {"VELO",   {-128,   22, -128,   55, -128, -128, -128, -128}},
      {"KEYTRK", {-128,   40, -128, -128, -128, -128, -128, -128}},
      {"MODWHL", {-128, -128, -128, -128, -128,    0, -128,   70}},
      {"AFTTCH", {-128,   18,   45, -128, -128, -128, -128, -128}},
      {"PBEND",  {  24, -128, -128, -128, -128, -128, -128, -128}},
      {"RAND",   {-128, -128, -128, -128,  -34,   -9, -128, -128}},
  };

  // Static chrome (frames, headers, labels) from the descriptor; the cell
  // grid and detail values below are the canned DYN content.
  spike::Interpret(nostromo::MatrixScreen(), fb,
                   nostromo::MakeCtx(nullptr, 0));

  const int ry = kBlockY + 31;
  for (int r = 0; r < kRows; ++r) {
    const int y = ry + r * kRowH;
    const bool sel = (r == kCursorRow);
    if (sel) {
      FillRect(fb, kLeftX, y, kLeftW, kRowH, kDim);
      TextLeft(fb, ">", kLeftX + 1, y + 3, kPrimaryFont, kBright);
    }
    TextLeft(fb, kRowData[r].src, kLeftX + kMk, y + 3, kPrimaryFont,
             sel ? kBright : kMid);
    for (int c = 0; c < 8; ++c) {
      const int cx = kLeftX + kMk + kSrc + c * kCol;
      const int v = kRowData[r].v[c];
      if (v == -128) {
        const char dot = static_cast<char>(0xB7);
        DrawGlyphRun(fb, cx + kCol - 6 - kPrimaryFont.w, y + 3, &dot, 1,
                     kPrimaryFont, sel ? kMid : kFaint, 0);
        continue;
      }
      char lab[12];
      if (v == 0)
        std::snprintf(lab, sizeof lab, "0");
      else
        std::snprintf(lab, sizeof lab, "%c%02d", v > 0 ? '+' : '-', std::abs(v));
      TextRight(fb, lab, cx + kCol - 6, y + 2, kPrimaryFont, kBright);
      SplitWell(fb, cx + 6, y + 24, v);
      if (sel && c == kCursorCol) Cursor(fb, cx, y, kCol - 6, kRowH, kBright);
    }
  }

  // Detail panel content: the slot title and its values. The frame, field
  // labels and separators are descriptor chrome.
  BlockTail(fb, kRightX, kBlockY, kRightW, "SLOT 07");
  static const char *kV[4] = {"ENV2", "CUTOF", "LIN", "MODWHL"};
  for (int i = 0; i < 4; ++i)
    TextLeft(fb, kV[i], kRightX + 102, kBlockY + 34 + i * 26, kPrimaryFont,
             kBright);
  TextLeft(fb, "+64", kRightX + 102, kBlockY + 158, kPrimaryFont, kBright);
  // The scaled split well's fill (its frame + limits + axis labels are chrome).
  {
    const int bx = kRightX + 8, by = kBlockY + 192, half = 165;
    FillRect(fb, bx + half + 10, by, 107, 14, kBright);
  }
}

// =======================================================================
// Screen 2 — patch browser
// =======================================================================
//
// Focus and state are separate: the CURSOR is a bracket + dim band (transient,
// moves with the encoder); the LOADED patch is a filled block in the marker
// column (persistent, changes only on push). With endless encoders these
// diverge the instant you turn the knob, so one visual cannot serve both.

void DrawPatch(FrameBuffer &fb) {
  constexpr int kMk = 16, kNo = 54, kNm = 310, kCt = 120, kMd = 60, kFg = 42;
  constexpr int kRowH = 26, kRows = 13;
  constexpr int kCursorRow = 10, kLoadedRow = 6;

  struct P { const char *no, *name, *cat; int mod; bool fav; };
  static const P kList[kRows] = {
      {"001", "INIT PATCH",   "BASIC",   0, false},
      {"002", "HOLLOW KEYS",  "PAD",     4, true },
      {"003", "GLASS BELL",   "KEYS",    3, false},
      {"004", "SUB DRIVER",   "BASS",    2, true },
      {"005", "TAPE STRINGS", "PAD",     7, false},
      {"006", "PLUCK FIFTH",  "LEAD",    5, false},
      {"007", "DUST MOTOR",   "TEXTURE", 6, true },
      {"008", "RESO SWEEP",   "FX",      9, false},
      {"009", "WOOD MALLET",  "PERC",    2, false},
      {"010", "CHORAL WASH",  "PAD",     8, true },
      {"011", "ACID LINE",    "BASS",    4, false},
      {"012", "BRASS STACK",  "BRASS",   6, false},
      {"013", "RAIN NOISE",   "TEXTURE", 3, false},
  };

  // Static chrome (title, filter strip, frames, column headers) from the
  // descriptor; the list rows, position bar and detail values are DYN content.
  spike::Interpret(nostromo::PatchScreen(), fb,
                   nostromo::MakeCtx(nullptr, 0));

  constexpr int kLw = 602;
  const int cx[5] = {kMk, kMk + kNo, kMk + kNo + kNm, kMk + kNo + kNm + kCt,
                     kMk + kNo + kNm + kCt + kMd};

  const int ry = kBlockY + 31;
  for (int r = 0; r < kRows; ++r) {
    const int y = ry + r * kRowH;
    const bool sel = (r == kCursorRow);
    if (sel) FillRect(fb, kLeftX, y, kLw, kRowH, kDim);
    if (r == kLoadedRow)
      FillRect(fb, kLeftX + 4, y + 9, 8, 8, kBright);  // loaded marker
    TextRight(fb, kList[r].no, kLeftX + cx[1] - 12, y + 3, kPrimaryFont,
              sel ? kBright : kMid);
    TextLeft(fb, kList[r].name, kLeftX + cx[1], y + 3, kPrimaryFont, kBright);
    TextLeft(fb, kList[r].cat, kLeftX + cx[2], y + 3, kPrimaryFont,
             sel ? kBright : kMid);
    char m[4];
    std::snprintf(m, sizeof m, "%d", kList[r].mod);
    TextRight(fb, m, kLeftX + cx[4] - 12, y + 3, kPrimaryFont,
              sel ? kBright : kMid);
    if (kList[r].fav)
      TextCenter(fb, "*", kLeftX + cx[4] + kFg / 2, y + 3, kPrimaryFont, kMid);
    if (sel) Cursor(fb, kLeftX, y, kLw, kRowH, kBright);
  }

  // Horizontal position bar, not a scrollbar: a vertical scrollbar would be
  // the only strided element on the screen.
  {
    const int y = ry + kRows * kRowH + 12;
    DrawHLine(fb, kLeftX, y - 6, kLw, kDim);
    FillRect(fb, kLeftX, y + 3, 440, 2, kFaint);
    FillRect(fb, kLeftX, y, 45, 8, kMid);
    TextLeft(fb, "001-013 OF 128", kLeftX + 456, y - 2, kSecondaryFont, kMid);
  }

  // Detail panel content: the under-cursor patch name + field values + loaded
  // patch + the edited alert. The frame, field labels and separators are
  // descriptor chrome.
  BlockTail(fb, kRightX, kBlockY, kRightW, "A11 ACID LINE");
  TextLeft(fb, "UNDER CURSOR - NOT LOADED", kRightX + 6, kBlockY + 28,
           kSecondaryFont, kMid);
  static const char *kV[8] = {"BASS",   "DIZAN",    "4",          "4/32",
                              "SQUARE", "LPF 18dB", "FAST DECAY", "DRIVE"};
  for (int i = 0; i < 4; ++i)
    TextLeft(fb, kV[i], kRightX + 116, kBlockY + 52 + i * 26, kPrimaryFont,
             kBright);
  for (int i = 4; i < 8; ++i)
    TextLeft(fb, kV[i], kRightX + 116, kBlockY + 174 + (i - 4) * 26,
             kPrimaryFont, kBright);
  TextLeft(fb, "A07 DUST MOTOR", kRightX + 136, kBlockY + 296, kPrimaryFont,
           kBright);
  FillRect(fb, kRightX, kBlockY + 326, kRightW, 26, kBright);
  TextLeft(fb, "EDITED - NOT SAVED", kRightX + 6, kBlockY + 329, kPrimaryFont,
           kBg);
}

// =======================================================================
// Screen 3 — save dialogue
// =======================================================================
//
// The ribbon is the whole idea: forty characters laid out at once with the
// current one inverse, so turning ENC2 moves a highlight along a row you can
// see. Without it you are scrolling a hidden list. Costs 40 short text runs
// and one inverse cell.

void DrawSave(FrameBuffer &fb) {
  // Static chrome (title, field frames, group rules, target labels, action
  // row) from the descriptor; the cells, ribbon, values and occupancy are DYN.
  spike::Interpret(nostromo::SaveScreen(), fb,
                   nostromo::MakeCtx(nullptr, 0));

  // Name field: per-cell underline rather than a box, so it reads as sixteen
  // fixed positions without sixteen vertical borders.
  {
    constexpr int kCells = 16, kCw = 20, kGap = 3, kCh = 34;
    const int total = kCells * (kCw + kGap) - kGap;
    const int x0 = (kFrameW - total) / 2, y = 92;
    static const char *kName = "DUST MOTOR MK2  ";
    constexpr int kCursor = 11;
    for (int i = 0; i < kCells; ++i) {
      const int x = x0 + i * (kCw + kGap);
      const bool cur = (i == kCursor);
      if (cur) FillRect(fb, x, y, kCw, kCh, kBright);
      DrawHLine(fb, x, y + kCh, kCw, cur ? kBright : kDim);
      DrawHLine(fb, x, y + kCh + 1, kCw, cur ? kBright : kDim);
      if (kName[i] != ' ')
        DrawGlyphRun(fb, x + (kCw - kPrimaryFont.w) / 2, y + 7, &kName[i], 1,
                     kPrimaryFont, cur ? kBg : kBright, 0);
    }
    TextLeft(fb, "POSITION 12/16", x0, y + 44, kSecondaryFont, kMid);
  }

  // Character ribbon.
  {
    constexpr int kN = 40, kCw = 20, kGap = 4, kCh = 28;
    static const char *kSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. ";
    constexpr int kSel = 12;  // 'M'
    const int x0 = 32, y = 200;
    for (int i = 0; i < kN; ++i) {
      const int x = x0 + i * (kCw + kGap);
      const bool on = (i == kSel);
      if (on) FillRect(fb, x, y, kCw, kCh, kBright);
      DrawGlyphRun(fb, x + (kCw - kPrimaryFont.w) / 2, y + 4, &kSet[i], 1,
                   kPrimaryFont, on ? kBg : kMid, 0);
    }
  }

  // Target values + overwrite warning (the frame and SLOT/CURRENTLY labels
  // are descriptor chrome).
  {
    const int y = 284;
    TextLeft(fb, "A07", kTitleX + 156, y + 34, kPrimaryFont, kBright);
    TextLeft(fb, "DUST MOTOR", kTitleX + 156, y + 60, kPrimaryFont, kBright);
    FillRect(fb, 528, y + 30, 480, 26, kBright);  // the one bright fill
    TextLeft(fb, "SLOT OCCUPIED - WILL OVERWRITE", 536, y + 33, kPrimaryFont,
             kBg);
    TextLeft(fb, "TURN ENC4 FOR THE NEXT FREE SLOT (A31)", 528, y + 66,
             kSecondaryFont, kMid);
  }

  // Bank occupancy: one tick per slot, tall for used, short for free, with a
  // bracket over the target. A horizontal run pattern, so nearly free -- and
  // it answers "where are the gaps" without a second screen.
  {
    const int y = 402;
    TextLeft(fb, "BANK A OCCUPANCY - 92 OF 128 USED", kTitleX, y,
             kSecondaryFont, kMid);
    const int by = y + 26;  // clears the target bracket above
    for (int i = 0; i < 128; ++i) {
      const int x = kTitleX + i * 992 / 128;
      const bool used = ((i * 7) % 11) > 2;
      if (used)
        FillRect(fb, x, by, 5, 14, kMid);
      else
        FillRect(fb, x, by + 11, 5, 3, kDim);
    }
    const int tx = kTitleX + 6 * 992 / 128;
    FillRect(fb, tx, by, 5, 14, kBright);
    FillRect(fb, tx - 2, by - 6, 9, 2, kBright);
    DrawVLine(fb, tx - 2, by - 6, 4, kBright);
    DrawVLine(fb, tx + 6, by - 6, 4, kBright);
  }
}

}  // namespace mockup

#ifndef TWANG_MOCKUP_NO_MAIN
namespace {

struct Screen {
  const char *name;
  void (*fn)(FrameBuffer &);
  std::uint32_t hash;  // FNV-1a over the RGB565 buffer
};

// Golden hashes. These change whenever the drawing changes -- which is the
// point: they catch a primitive or atlas change silently altering the mockups,
// and the mockups only have value while they predict the panel.
constexpr Screen kScreens[4] = {
    {"mockup_signal.png", mockup::DrawSignal, 0xE6B7D4F2u},
    {"mockup_matrix.png", mockup::DrawMatrix, 0x2987002Eu},
    {"mockup_patch.png", mockup::DrawPatch, 0xEC738A07u},
    {"mockup_save.png", mockup::DrawSave, 0x64E4DF78u},
};

std::uint32_t Fnv1a(const std::vector<std::uint16_t> &b) {
  std::uint32_t h = 2166136261u;
  for (std::uint16_t v : b) {
    h = (h ^ (v & 0xFF)) * 16777619u;
    h = (h ^ (v >> 8)) * 16777619u;
  }
  return h;
}

void Render(const Screen &s, std::vector<std::uint16_t> &buf) {
  buf.assign(static_cast<std::size_t>(kFrameW) * kFrameH, kBg);
  FrameBuffer fb{buf.data(), kFrameW, kFrameH, kFrameW,
                 spike::Rect{0, 0, kFrameW, kFrameH}};
  s.fn(fb);
}

}  // namespace

int main(int argc, char **argv) {
  const std::string arg1 = (argc > 1) ? argv[1] : ".";

  if (arg1 == "--check") {
    int bad = 0;
    for (const auto &s : kScreens) {
      std::vector<std::uint16_t> buf;
      Render(s, buf);
      const std::uint32_t h = Fnv1a(buf);
      const bool ok = (h == s.hash);
      std::printf("%-22s 0x%08X %s\n", s.name, h,
                  ok ? "ok" : "MISMATCH (update the table if intended)");
      if (!ok) ++bad;
    }
    if (bad) std::printf("\n%d screen(s) changed\n", bad);
    return bad ? 1 : 0;
  }

  const int scale = (argc > 2) ? std::atoi(argv[2]) : 1;
  for (const auto &s : kScreens) {
    std::vector<std::uint16_t> buf;
    Render(s, buf);
    const std::string path = arg1 + "/" + s.name;
    spike::FrameBuffer fb{buf.data(), kFrameW, kFrameH, kFrameW,
                          spike::Rect{0, 0, kFrameW, kFrameH}};
    if (spike::WriteFramePng(fb, path.c_str(), scale))
      std::printf("wrote %s (%dx%d)\n", path.c_str(), kFrameW * scale,
                  kFrameH * scale);
    else
      std::fprintf(stderr, "cannot write %s\n", path.c_str());
  }
  return 0;
}
#endif  // TWANG_MOCKUP_NO_MAIN
