// nostromo/screens.cc — the four screens as static-chrome descriptors.
//
// The chrome is a byte stream (§5.2) produced once by the encoder below and
// interpreted by spike::Interpret. Everything variable — plot curves, matrix
// cells, split wells, cursors, the mode buttons — is drawn by the consumers'
// DYN hooks (live state in the panel, canned state in the mockup), never
// encoded here. That is the §5.1 split: the descriptor says *where*, C says
// *what*.
//
// The encoder mirrors the old hand-written chrome arithmetic exactly (same
// integer expressions, same draw order), so the interpreter output is
// byte-identical to what nostromo/panel.cc and tools/mockup_screens.cc used
// to draw.

#include "screens.h"

#include <array>
#include <cstring>

#include "geom.h"
#include "palette.h"

namespace nostromo {

using spike::kOpCall;
using spike::kOpDyn;
using spike::kOpEnd;
using spike::kOpHLine;
using spike::kOpRect;
using spike::kOpText;
using spike::kOpVLine;

// ---- frame / layout constants (1024x600) -------------------------------
// The edit screen's geometry (pane, columns, plot band) comes from geom.h;
// these remain for the shared title/nav chrome and the superseded
// matrix/patch/save screens.

constexpr int kFrameW = 1024, kFrameH = 600;
constexpr int kTitleX = 16, kTitleY = 16, kTitleW = 992, kTitleH = 26;
constexpr int kNavY = 512;

// Matrix / patch / save shared block geometry (mirrors mockup_screens.cc).
constexpr int kBlockY = 88, kBlockH = 388;
constexpr int kLeftX = 16, kLeftW = 598;
constexpr int kRightX = 652, kRightW = 356;

// ---- palette + fonts ---------------------------------------------------

// The five-entry descriptor palette (ColorIdx order), resolved to RGB565.
// Namespace-scope const — not a function-local static.
const spike::Color kPalette[5] = {kBg, kFaint, kDim, kMid, kBright};

const spike::Color *DescriptorPalette() { return kPalette; }

namespace {
const spike::Font kFonts[2] = {spike::kPrimaryFont, spike::kSecondaryFont};

// Text width (monospace: len * advance). File-local; panel.cc has its own.
int TextW(const spike::Font &f, const char *s) {
  return static_cast<int>(std::strlen(s)) * f.w;
}
}  // namespace

spike::DescriptorCtx MakeCtx(spike::DynSlot *slots, int n_slots) {
  spike::DescriptorCtx ctx;
  ctx.palette = DescriptorPalette();
  ctx.n_palette = 5;
  ctx.fonts = kFonts;
  ctx.templates = nullptr;
  ctx.slots = slots;
  ctx.n_slots = n_slots;
  return ctx;
}

// ---- descriptor emitter --------------------------------------------------

// Maximum encoded screen size. The four screens are static chrome; their
// byte streams are small (a few hundred bytes) and fixed.
constexpr std::size_t kMaxScreenBytes = 1024;

// A tiny fixed-capacity byte-stream writer (no heap). All coordinates are u16
// (the panel is 1024x600); palette/font indices are u8.
struct Enc {
  std::array<std::uint8_t, kMaxScreenBytes> b{};
  std::size_t n = 0;
  void U8(std::uint8_t v) { b[n++] = v; }
  void U16(std::uint16_t v) {
    b[n++] = static_cast<std::uint8_t>(v & 0xFF);
    b[n++] = static_cast<std::uint8_t>(v >> 8);
  }
  void Rect(int x, int y, int w, int h, int c) {
    U8(kOpRect); U16(x); U16(y); U16(w); U16(h); U8(c);
  }
  void HLine(int x, int y, int w, int c) {
    U8(kOpHLine); U16(x); U16(y); U16(w); U8(c);
  }
  void VLine(int x, int y, int h, int c) {
    U8(kOpVLine); U16(x); U16(y); U16(h); U8(c);
  }
  void Text(int x, int y, int font, int c, const char *s) {
    const std::uint8_t len = static_cast<std::uint8_t>(std::strlen(s));
    U8(kOpText); U16(x); U16(y); U8(font); U8(c); U8(len);
    for (std::uint8_t i = 0; i < len; ++i) b[n++] = s[i];
  }
  void Dyn(int slot, int x, int y, int w, int h) {
    U8(kOpDyn); U8(slot); U16(x); U16(y); U16(w); U16(h);
  }
  void End() { U8(kOpEnd); }
};

// ---- component vocabulary (chrome, as emitted ops) -----------------------

void TitleBar(Enc &e, const char *left, const char *right) {
  e.Rect(kTitleX, kTitleY, kTitleW, kTitleH, kC_Dim);
  e.Text(kTitleX + 8, kTitleY + 3, kF_Primary, kC_Bright, left);
  e.Text(kTitleX + kTitleW - 8 - TextW(spike::kPrimaryFont, right),
         kTitleY + 3, kF_Primary, kC_Mid, right);
}

// Corner brackets, 6 px outside the content (registration marks).
void Brackets(Enc &e, int x, int y, int w, int h, int c) {
  const int leg = 10, th = 2, off = 6;
  e.Rect(x - off, y - off, leg, th, c);
  e.Rect(x + w + off - leg, y - off, leg, th, c);
  e.Rect(x - off, y + h + off - th, leg, th, c);
  e.Rect(x + w + off - leg, y + h + off - th, leg, th, c);
  e.Rect(x - off, y - off, th, leg, c);
  e.Rect(x + w + off - th, y - off, th, leg, c);
  e.Rect(x - off, y + h + off - leg, th, leg, c);
  e.Rect(x + w + off - th, y + h + off - leg, th, leg, c);
}

// Block + tail rule: a label above a field or panel.
void BlockTail(Enc &e, int x, int y, int w, const char *label) {
  const int bw = TextW(spike::kPrimaryFont, label) + 12;
  e.Rect(x, y, bw, 22, kC_Dim);
  e.Text(x + 6, y + 1, kF_Primary, kC_Bright, label);
  e.HLine(x + bw + 6, y + 20, w - bw - 6, kC_Dim);
  e.HLine(x + bw + 6, y + 21, w - bw - 6, kC_Dim);
}

// Header broken at column boundaries (the header rules the grid horizontally).
void SegmentedHeader(Enc &e, int x, int y, int w, const int *bounds, int n) {
  int prev = 0;
  for (int i = 0; i < n; ++i) {
    e.Rect(x + prev, y, bounds[i] - 2 - prev, 22, kC_Dim);
    prev = bounds[i] + 2;
  }
  e.Rect(x + prev, y, w - prev, 22, kC_Dim);
}

// 5 px column index ticks (graduate the grid without ruling it).
void IndexTicks(Enc &e, int x, int y, const int *bounds, int n) {
  for (int i = 0; i < n; ++i) e.VLine(x + bounds[i], y, 5, kC_Dim);
}

void NavBar(Enc &e, int active) {
  static const char *const kTabs[5] = {"SIGNAL", "MATRIX", "PATCH", "ARP", "SYS"};
  e.HLine(kTitleX, kNavY, kTitleW, kC_Dim);
  for (int i = 0; i < 5; ++i) {
    const int cx = kTitleX + i * 198 + 99;
    const bool on = (i == active);
    const int tx = cx - TextW(spike::kPrimaryFont, kTabs[i]) / 2;
    e.Text(tx, kNavY + 12, kF_Primary, on ? kC_Bright : kC_Mid, kTabs[i]);
    if (on) e.Rect(cx - 62, kNavY + 38, 124, 3, kC_Bright);
  }
  e.HLine(kTitleX, kNavY + 44, kTitleW, kC_Dim);
}

void EncoderLegend(Enc &e, const char *s) {
  e.Text(kTitleX, kNavY + 54, kF_Secondary, kC_Dim, s);
}

// ---- screen 0: edit (pane + columns + plot band) ------------------------

// The static chrome of the edit screen: the background, the pane gutter, and
// the four plot DYN slots (which share the one 900x404 band). Everything else
// — the title, the pane contents, the column headers/values — is dynamic and
// drawn per-frame by the panel's DrawEditChrome (it reads NavState).
void EncodeSignal(Enc &e) {
  e.Rect(0, 0, kFrameW, kFrameH, kC_Bg);

  // Pane gutter: the boundary between the navigator and the page.
  e.VLine(geom::kPaneX + geom::kPaneW - 5, geom::kPaneY, geom::kPaneH, kC_Dim);
  e.VLine(geom::kPaneX + geom::kPaneW - 4, geom::kPaneY, geom::kPaneH, kC_Dim);

  // The four plots share the one plot band; the page's dyn_slot picks which
  // hook draws. The mode slot (4) is reserved but unused by the edit screen.
  for (int m = 0; m < 4; ++m)
    e.Dyn(m, geom::kPlotX, geom::kPlotY, geom::kPlotW, geom::kPlotH);
  e.Dyn(kSlotMode, 0, 0, 0, 0);

  e.End();
}

// ---- screen 1: modulation matrix ----------------------------------------

void EncodeMatrix(Enc &e) {
  constexpr int kMk = 12, kSrc = 90, kCol = 62;
  static const char *const kDest[8] = {"PITCH", "CUTOF", "RESO",  "LEVEL",
                                       "PAN",   "PWM",   "FMAMT", "LFO2R"};
  int bounds[8];
  for (int i = 0; i < 8; ++i) bounds[i] = kMk + kSrc + i * kCol;

  TitleBar(e, "MOD MATRIX", "15/32 SLOTS  PAGE 1/2  VOICE 01");

  // Left block: frame, segmented header, column headers, index ticks. The
  // 11x8 cell grid (values + split wells + cursor) is DYN content.
  Brackets(e, kLeftX, kBlockY, kLeftW, kBlockH, kC_Mid);
  SegmentedHeader(e, kLeftX, kBlockY, kLeftW, bounds, 8);
  e.Text(kLeftX + kMk, kBlockY + 1, kF_Primary, kC_Bright, "SOURCE");
  for (int i = 0; i < 8; ++i)
    e.Text(kLeftX + bounds[i] + kCol - 6 - TextW(spike::kPrimaryFont, kDest[i]),
           kBlockY + 1, kF_Primary, kC_Bright, kDest[i]);
  IndexTicks(e, kLeftX, kBlockY + 24, bounds, 8);

  // Right block: frame, field labels, separators, scaled-well frame. The
  // BlockTail title, field values, and the well fill are DYN content.
  Brackets(e, kRightX, kBlockY, kRightW, kBlockH, kC_Mid);
  static const char *const kK[4] = {"SOURCE", "DEST", "CURVE", "VIA"};
  for (int i = 0; i < 4; ++i)
    e.Text(kRightX + 6, kBlockY + 34 + i * 26, kF_Primary, kC_Mid, kK[i]);
  e.HLine(kRightX, kBlockY + 146, kRightW, kC_Dim);
  e.Text(kRightX + 6, kBlockY + 158, kF_Primary, kC_Mid, "AMOUNT");
  {
    const int bx = kRightX + 8, by = kBlockY + 192, half = 165;
    e.Rect(bx + 4, by, half, 14, kC_Dim);
    e.Rect(bx + half + 10, by, half, 14, kC_Dim);
    e.VLine(bx + 2, by - 6, 26, kC_Mid);
    e.HLine(bx + 2, by - 6, 6, kC_Mid);
    e.HLine(bx + 2, by + 19, 6, kC_Mid);
    e.VLine(bx + 2 * half + 12, by - 6, 26, kC_Mid);
    e.HLine(bx + 2 * half + 7, by - 6, 6, kC_Mid);
    e.HLine(bx + 2 * half + 7, by + 19, 6, kC_Mid);
    e.Text(bx, by + 24, kF_Secondary, kC_Mid, "-99");
    e.Text(bx + half + 8, by + 24, kF_Secondary, kC_Mid, "0");
    e.Text(bx + 2 * half + 14 - TextW(spike::kSecondaryFont, "+99"), by + 24,
           kF_Secondary, kC_Mid, "+99");
  }
  e.HLine(kRightX, kBlockY + 250, kRightW, kC_Dim);
  e.Text(kRightX + 6, kBlockY + 260, kF_Secondary, kC_Mid,
         "DEPTH SCALED BY MODWHL");
  e.Text(kRightX + 6, kBlockY + 278, kF_Secondary, kC_Mid,
         "BIPOLAR  UNSMOOTHED");

  NavBar(e, 1);
  EncoderLegend(e,
                "ENC1 AMOUNT   ENC2 CURVE   ENC3 VIA   ENC4 SLOT   PUSH CLEAR");
  e.End();
}

// ---- screen 2: patch browser --------------------------------------------

void EncodePatch(Enc &e) {
  constexpr int kMk = 16, kNo = 54, kNm = 310, kCt = 120, kMd = 60, kFg = 42;

  TitleBar(e, "PATCHES", "128 IN BANK  VOICE 01/16");

  // Filter strip: segmented by filter group (the active values are DYN).
  {
    const int y = 50;
    const int fb2[2] = {230, 558};
    SegmentedHeader(e, kTitleX, y, kTitleW, fb2, 2);
    e.Text(kTitleX + 6, y + 1, kF_Primary, kC_Mid, "BANK");
    e.Text(kTitleX + 76, y + 1, kF_Primary, kC_Bright, "A");
    e.Text(kTitleX + 244, y + 1, kF_Primary, kC_Mid, "CATEGORY");
    e.Text(kTitleX + 344, y + 1, kF_Primary, kC_Bright, "ALL");
    e.Text(kTitleX + 572, y + 1, kF_Primary, kC_Mid, "SORT");
    e.Text(kTitleX + 642, y + 1, kF_Primary, kC_Bright, "NUMBER");
  }

  // Left block: list frame + column headers + ticks. The 13 rows (values,
  // selection, loaded marker, cursor) and the position bar are DYN content.
  constexpr int kLw = 602;
  const int cx[5] = {kMk, kMk + kNo, kMk + kNo + kNm, kMk + kNo + kNm + kCt,
                     kMk + kNo + kNm + kCt + kMd};
  const int bounds[4] = {cx[1], cx[2], cx[3], cx[4]};
  Brackets(e, kLeftX, kBlockY, kLw, kBlockH, kC_Mid);
  SegmentedHeader(e, kLeftX, kBlockY, kLw, bounds, 4);
  e.Text(kLeftX + cx[1] - 12 - TextW(spike::kPrimaryFont, "NO"), kBlockY + 1,
         kF_Primary, kC_Bright, "NO");
  e.Text(kLeftX + cx[1], kBlockY + 1, kF_Primary, kC_Bright, "NAME");
  e.Text(kLeftX + cx[2], kBlockY + 1, kF_Primary, kC_Bright, "CATEGORY");
  e.Text(kLeftX + cx[4] - 12 - TextW(spike::kPrimaryFont, "MOD"), kBlockY + 1,
         kF_Primary, kC_Bright, "MOD");
  e.Text(kLeftX + cx[4] + kFg / 2 - TextW(spike::kPrimaryFont, "F") / 2,
         kBlockY + 1, kF_Primary, kC_Bright, "F");
  IndexTicks(e, kLeftX, kBlockY + 24, bounds, 4);

  // Right block: frame, field labels, separators, LOADED label. The BlockTail
  // title, field values, and the loaded patch name are DYN content.
  Brackets(e, kRightX, kBlockY, kRightW, kBlockH, kC_Mid);
  static const char *const kK[8] = {"CATEGORY", "AUTHOR",   "VOICES",
                                    "MOD SLOTS", "OSC",      "FILTER",
                                    "ENV",       "FX"};
  for (int i = 0; i < 4; ++i)
    e.Text(kRightX + 6, kBlockY + 52 + i * 26, kF_Primary, kC_Mid, kK[i]);
  e.HLine(kRightX, kBlockY + 162, kRightW, kC_Dim);
  for (int i = 4; i < 8; ++i)
    e.Text(kRightX + 6, kBlockY + 174 + (i - 4) * 26, kF_Primary, kC_Mid, kK[i]);
  e.HLine(kRightX, kBlockY + 284, kRightW, kC_Dim);
  e.Text(kRightX + 6, kBlockY + 296, kF_Primary, kC_Mid, "LOADED");

  NavBar(e, 2);
  EncoderLegend(
      e, "ENC1 SCROLL   ENC2 BANK   ENC3 CATEGORY   ENC4 TARGET SLOT   PUSH LOAD");
  e.End();
}

// ---- screen 3: save dialogue --------------------------------------------

void EncodeSave(Enc &e) {
  TitleBar(e, "SAVE PATCH", "BANK A");

  // Name field frame; the 16 cells (underlines + glyphs + cursor) are DYN.
  BlockTail(e, kTitleX, 56, kTitleW, "NAME");

  // Character ribbon frame + group rules + group labels; the 40 glyphs and
  // the highlight are DYN content.
  BlockTail(e, kTitleX, 164, kTitleW, "CHARACTER");
  {
    constexpr int kCw = 20, kGap = 4, kCh = 28;
    const int x0 = 32, y = 200;
    e.HLine(x0, y + kCh + 6, 26 * (kCw + kGap) - kGap, kC_Dim);
    e.HLine(x0 + 26 * (kCw + kGap), y + kCh + 6, 10 * (kCw + kGap) - kGap,
            kC_Dim);
    e.HLine(x0 + 36 * (kCw + kGap), y + kCh + 6, 4 * (kCw + kGap) - kGap,
            kC_Dim);
    e.Text(x0, y + kCh + 12, kF_Secondary, kC_Mid, "A-Z");
    e.Text(x0 + 26 * (kCw + kGap), y + kCh + 12, kF_Secondary, kC_Mid, "0-9");
    e.Text(x0 + 36 * (kCw + kGap), y + kCh + 12, kF_Secondary, kC_Mid,
           "SYMBOL");
  }

  // Target frame + field labels; the slot/name values and the overwrite
  // warning are DYN content.
  {
    const int y = 284;
    BlockTail(e, kTitleX, y, kTitleW, "TARGET");
    e.Text(kTitleX + 6, y + 34, kF_Primary, kC_Mid, "SLOT");
    e.Text(kTitleX + 6, y + 60, kF_Primary, kC_Mid, "CURRENTLY");
  }

  // Action row replaces the nav bar (this screen is modal).
  e.HLine(kTitleX, kNavY, kTitleW, kC_Dim);
  static const char *const kActs[3] = {"CANCEL", "SAVE AS NEW", "OVERWRITE"};
  for (int i = 0; i < 3; ++i) {
    const int cx = kTitleX + i * 331 + 165;
    e.Text(cx - TextW(spike::kPrimaryFont, kActs[i]) / 2, kNavY + 12,
           kF_Primary, i == 2 ? kC_Bright : kC_Mid, kActs[i]);
    if (i == 2) e.Rect(cx - 70, kNavY + 38, 140, 3, kC_Bright);
  }
  e.HLine(kTitleX, kNavY + 44, kTitleW, kC_Dim);
  EncoderLegend(e, "ENC1 POSITION   ENC2 CHARACTER   ENC3 CHAR SET   ENC4 TARGET "
                   "SLOT   PUSH CONFIRM");
  e.End();
}

// ---- shared DYN hook: mode buttons --------------------------------------

void DrawModeButtons(spike::FrameBuffer &fb, const spike::Rect &r, int mode) {
  static const char *const kNames[3] = {"SCOPE", "CYCLE", "SPEC"};
  int bx = r.x;
  for (int i = 0; i < 3; ++i) {
    const int bw = TextW(spike::kSecondaryFont, kNames[i]) + 6;
    const bool active = (i == mode);
    if (active) FillRect(fb, bx, r.y, bw, r.h, kBright);
    DrawGlyphRun(fb, bx + 3, r.y + 2, kNames[i],
                 static_cast<int>(std::strlen(kNames[i])), spike::kSecondaryFont,
                 active ? kBg : kMid, 0);
    bx += bw + 6;
  }
}

// ---- built-once caches (no heap, no function-local static) ---------------

namespace {

// Build a screen's byte stream into a fixed-size array at static init. The
// encoded size is deterministic (static chrome), so a fixed buffer replaces
// the std::vector heap allocation and its lazy init.
std::array<std::uint8_t, kMaxScreenBytes> BuildBytes(void (*encode)(Enc &)) {
  std::array<std::uint8_t, kMaxScreenBytes> a{};
  Enc e;
  encode(e);
  std::memcpy(a.data(), e.b.data(), e.n);
  return a;
}

const std::array<std::uint8_t, kMaxScreenBytes> kSignalBytes =
    BuildBytes(EncodeSignal);
const std::array<std::uint8_t, kMaxScreenBytes> kMatrixBytes =
    BuildBytes(EncodeMatrix);
const std::array<std::uint8_t, kMaxScreenBytes> kPatchBytes =
    BuildBytes(EncodePatch);
const std::array<std::uint8_t, kMaxScreenBytes> kSaveBytes =
    BuildBytes(EncodeSave);

}  // namespace

const std::uint8_t *SignalScreen() { return kSignalBytes.data(); }
const std::uint8_t *MatrixScreen() { return kMatrixBytes.data(); }
const std::uint8_t *PatchScreen() { return kPatchBytes.data(); }
const std::uint8_t *SaveScreen() { return kSaveBytes.data(); }

}  // namespace nostromo
