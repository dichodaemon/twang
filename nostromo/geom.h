// geom.h — parameterised panel geometry.
//
// Every layout number in nostromo lives here. Screens derive their
// coordinates from these; a literal coordinate anywhere else is a defect
// (nostromo-interaction_arch-design.md, invariant 8).
//
// Change the parameters in the first block and everything below re-derives.
// A configuration that cannot be built fails a static_assert rather than
// producing a silently overlapping layout.

#ifndef NOSTROMO_GEOM_H_
#define NOSTROMO_GEOM_H_

namespace nostromo::geom {

// ---- parameters --------------------------------------------------------

inline constexpr int   kFbWidth     = 1024;
inline constexpr int   kFbHeight    = 600;
inline constexpr float kPanelWmm    = 154.2144f;  // ER-TFT070-6 active area
inline constexpr float kPanelHmm    = 85.92f;
inline constexpr int   kColumns     = 5;          // E
inline constexpr int   kMargin      = 16;
inline constexpr int   kPaneW       = 92;
inline constexpr float kKnobDiaMm   = 20.0f;
inline constexpr float kFingerGapMm = 5.0f;

inline constexpr int kTitleH    = 26;
inline constexpr int kBandGap   = 16;
inline constexpr int kHeaderH   = 26;
inline constexpr int kHeaderGap = 6;
inline constexpr int kValueH    = 20;
inline constexpr int kValueGap  = 4;
inline constexpr int kWellH     = 8;
inline constexpr int kSumGap    = 6;
inline constexpr int kSumLines  = 2;    // inbound-route summary, edit mode
inline constexpr int kSumPitch  = 18;   // secondary atlas + leading
inline constexpr int kPlotGap   = 16;
inline constexpr int kRowPitch  = 26;  // list rows: 20px cell + leading
inline constexpr int kPanePitch = 26;
inline constexpr int kPaneRule  = 3;
inline constexpr int kPrimaryH  = 20;   // ter-u20n cell height
inline constexpr int kPlotMinH  = 120;

// Selectable subjects in the pane, in pane order (NAV1 walks index order;
// matches SubjectId in interaction.h, arch-design §7.6). Class labels
// (OSC, ENV, LFO) are presentation, not subjects: NAV1 skips them.
// 17 = PART, OSC1-4, FILT, AMP, ENV1-3, LFO1-3, MOD, FX, PATCH, CONF. OUT is
// gone — the output view is an embedded plot plus a latched full-screen
// mode, not a subject. pages.h static_asserts SubjectId::kCount against this.
inline constexpr int kSubjectCount = 17;

// ---- derived -----------------------------------------------------------

inline constexpr float kPxMm   = kPanelWmm / kFbWidth;   // 0.150600
inline constexpr float kPyMm   = kPanelHmm / kFbHeight;  // 0.143200
inline constexpr float kAspect = kPxMm / kPyMm;          // 1.0517

inline constexpr int   kColW = (kFbWidth - 2 * kMargin - kPaneW) / kColumns;
inline constexpr int   kColX(int n) { return kMargin + kPaneW + n * kColW; }
inline constexpr float kPitchMm = kColW * kPxMm;

// The pane owns the full left column, top margin to bottom margin. The title
// bar starts where the columns start, so no vertical space is spent on a band
// the pane could be using.
// The title bar spans the full width again: the strip pane freed enough
// vertical space that the pane no longer needs the top band, and a full-width
// title gives the part swatches a home outside the navigator.
inline constexpr int kTitleY   = kMargin;                           //  16
inline constexpr int kTitleX   = kMargin;                           //  16
inline constexpr int kTitleW   = kFbWidth - 2 * kMargin;            // 992
inline constexpr int kContentY = kTitleY + kTitleH + kBandGap;      //  58
inline constexpr int kHeaderY  = kContentY;                         //  58
inline constexpr int kValueY   = kHeaderY + kHeaderH + kHeaderGap;  //  90
inline constexpr int kWellY    = kValueY + kValueH + kValueGap;     // 114
inline constexpr int kSumY     = kWellY + kWellH + kSumGap;         // 128
inline constexpr int kSumH     = kSumLines * kSumPitch;             //  36
inline constexpr int kPlotY    = kSumY + kSumH + kPlotGap;          // 180
inline constexpr int kBottom   = kFbHeight - kMargin;               // 584

inline constexpr int kPlotX = kMargin + kPaneW;                     // 108

inline constexpr int kPlotW = kColumns * kColW;                     // 900
inline constexpr int kPlotH = kBottom - kPlotY;                     // 404

// Embedded output plot: the plot band splits into two equal halves with a
// gap. Half 0 hosts the page's own plot; half 1 hosts the output view. Both
// are 440 px wide; kOutView re-tiles the full 900 px into the output slot.
inline constexpr int kEmbedGap = 20;
inline constexpr int kEmbedW   = (kPlotW - kEmbedGap) / 2;             // 440
inline constexpr int kEmbedX(int half) {
  return kPlotX + half * (kEmbedW + kEmbedGap);                        // 108 / 568
}

// Corner brackets that frame each plot region (and separate the two embedded
// halves). The filter/envelope cursor reuses the same shape at 7 px legs.
inline constexpr int kPlotBracketLeg = 12;  // arm length
inline constexpr int kPlotBracketTh  = 2;   // arm thickness

// Label insets: how far the output plot's text (mode name + axis labels) sits
// from the plot edge so it clears the corner brackets. The bracket arm is
// kPlotBracketTh thick, so the visible gap is pad - kPlotBracketTh.
inline constexpr int kPlotLabelPadX = 6;  // left/right edge -> text
inline constexpr int kPlotLabelPadY = 8;  // top/bottom edge -> text

inline constexpr int kListY    = kValueY;                           //  90
inline constexpr int kListH    = kBottom - kListY;                  // 494
inline constexpr int kListRows = kListH / kRowPitch;                //  19

inline constexpr int kRouteY     = kWellY;                          // 114
inline constexpr int kRouteH     = kBottom - kRouteY;               // 470
inline constexpr int kRouteLines = kRouteH / kRowPitch;             //  18

inline constexpr int kPaneX    = kMargin;                           //  16
inline constexpr int kPaneY    = kContentY;                         //  58
inline constexpr int kPaneH    = kBottom - kPaneY;                  // 526
// Instance strips. Cells have a fixed pitch so digits line up between
// classes; padding shrinks as the token grows, since a lone digit needs air
// to read as a cell and a two-letter token already has width of its own.
inline constexpr int kStripH    = 22;
inline constexpr int kStripX0   = 6;
inline constexpr int kLabelX    = 8;
inline constexpr int kStripAvail = kPaneW - 8 - kStripX0;           //  78
inline constexpr int kGlyphW    = 10;   // primary atlas cell width

// Title bar fields. The screen name occupies a fixed-width slot sized for the
// longest compound title ("MOD VIEW OSCILLATOR 2"), so the patch name never
// moves and a mode prefix never forces the subject to be abbreviated. A cut in
// the filled bar separates them, at the same x as the navigator boundary.
inline constexpr int kTitleNameChars = 22;
inline constexpr int kTitleNameX = kPlotX + 6;                      // 114
inline constexpr int kTitleNameW = kTitleNameChars * kGlyphW;       // 220
inline constexpr int kTitleSepX  = kTitleNameX + kTitleNameW + 8;   // 342
inline constexpr int kTitlePatchX = kTitleSepX + 10;                // 352

inline constexpr int StripCellW(int chars) {
  return chars * kGlyphW + (chars == 1 ? 8 : 4);
}
inline constexpr int StripW(int cells, int chars) {
  return cells * StripCellW(chars);
}

// Pane height requirement. Rows are class labels and singletons; each class
// with instances adds a strip and its trailing gap. Computed, not assumed.
inline constexpr int kPaneRowsN   = 10;  // PART FILT AMP MOD OSC ENV LFO FX PATCH CONF
inline constexpr int kPaneStripsN = 3;   // OSC ENV LFO
inline constexpr int kPaneNeedH   = kPaneRowsN * kPanePitch +
                                    kPaneStripsN * (kStripH + 8) +
                                    kPaneRule + 6;

// ---- guards ------------------------------------------------------------

static_assert(kColumns * kColW + kPaneW + 2 * kMargin == kFbWidth,
              "columns must tile the content width exactly");
static_assert(kPitchMm >= kKnobDiaMm + kFingerGapMm,
              "encoder pitch below the ergonomic floor: reduce kColumns "
              "or widen the panel");
static_assert(kPlotH >= kPlotMinH,
              "plot band collapsed: the bands above it have grown past what "
              "a plot-dominant layout allows");
static_assert(kValueH >= kPrimaryH && kHeaderH >= kPrimaryH,
              "a text band is shorter than the primary atlas cell");
static_assert(kRouteLines >= 6,
              "mod view cannot show a useful number of routes per column");
static_assert(kPaneH >= kPaneNeedH,
              "the subject pane cannot show every class and its instance "
              "strips without scrolling");
// Every strip must fit the pane. A four-cell alphabetic strip is 96 px and
// would silently overflow, which is how the OUT strip first shipped wrong.
static_assert(StripW(4, 1) <= kStripAvail, "digit strip overflows the pane");
// The embedded output halves must tile the plot band exactly, so half 0 and
// half 1 together span the full plot width with no stray pixels.
static_assert(kEmbedW * 2 + kEmbedGap == kPlotW,
              "embedded plot halves must tile the plot width exactly");

}  // namespace nostromo::geom

#endif  // NOSTROMO_GEOM_H_
