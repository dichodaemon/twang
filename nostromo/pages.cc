// pages.cc — the page table, one PageDesc per subject in pane order.
//
// The single read-only source of truth for what each subject's screen shows
// (nostromo-interaction_arch-design.md §7.6). Columns are ordered: the head of
// each list is the page's hot set and stays group 0 at any column count.
// Grouping into columns is derived from geom::kColumns by GroupCount/Column in
// pages.h — never authored here.

#include "pages.h"

#include "params.h"

namespace nostromo {

namespace {

// Column count helper: the table never repeats a count that the array already
// states, so the two cannot drift.
template <std::size_t N>
constexpr std::uint8_t ColCount(const ColumnSpec (&)[N]) {
  return static_cast<std::uint8_t>(N);
}

// The module instance a subject addresses: osc 1-4 → 0-3, env/lfo 1-3 → 0-2,
// single-instance subjects → 0. A column declares a ParamId (kind); the
// instance comes from the subject (arch-design §7.5). This is what makes
// ResolveBinding return a complete ParamRef.
constexpr std::uint8_t InstanceOf(SubjectId s) {
  switch (s) {
    case SubjectId::kOsc1: return 0;
    case SubjectId::kOsc2: return 1;
    case SubjectId::kOsc3: return 2;
    case SubjectId::kOsc4: return 3;
    case SubjectId::kEnv1: return 0;
    case SubjectId::kEnv2: return 1;
    case SubjectId::kEnv3: return 2;
    case SubjectId::kLfo1: return 0;
    case SubjectId::kLfo2: return 1;
    case SubjectId::kLfo3: return 2;
    default: return 0;
  }
}

// kPending columns render dim and inert until the engine's parameter surface
// grows (arch-design §13.3); they keep the page taxonomy honest.

constexpr ColumnSpec kColsPart[] = {
    {ColumnKind::kPending, "CHAN", {}},
    {ColumnKind::kPending, "VOICES", {}},
    {ColumnKind::kPending, "TRANSPOSE", {}},
    {ColumnKind::kPending, "GLIDE", {}},
    {ColumnKind::kPending, "MONO", {}},
    {ColumnKind::kPending, "BEND", {}},
};

constexpr ColumnSpec kColsOsc[] = {
    {ColumnKind::kPending, "WAVE", {}},
    {ColumnKind::kParam, nullptr, {engine::ParamId::kPitchCoarse}},
    {ColumnKind::kPending, "FINE", {}},
    {ColumnKind::kPending, "LEVEL", {}},
    {ColumnKind::kPending, "SHAPE", {}},
    {ColumnKind::kPending, "PAN", {}},
    {ColumnKind::kPending, "SYNC", {}},
};

constexpr ColumnSpec kColsFilt[] = {
    {ColumnKind::kParam, nullptr, {engine::ParamId::kCutoff}},
    {ColumnKind::kParam, nullptr, {engine::ParamId::kResonance}},
    {ColumnKind::kPending, "ENVAMT", {}},
    {ColumnKind::kParam, nullptr, {engine::ParamId::kDrive}},
    {ColumnKind::kParam, nullptr, {engine::ParamId::kKeyFollowDepth}},
    {ColumnKind::kPending, "MODE", {}},
};

constexpr ColumnSpec kColsAmp[] = {
    {ColumnKind::kParam, nullptr, {engine::ParamId::kAmp}},
    {ColumnKind::kPending, "PAN", {}},
    {ColumnKind::kPending, "VELO", {}},
    {ColumnKind::kPending, "SENDA", {}},
    {ColumnKind::kPending, "SENDB", {}},
};

constexpr ColumnSpec kColsEnv[] = {
    {ColumnKind::kParam, nullptr, {engine::ParamId::kAttack}},
    {ColumnKind::kParam, nullptr, {engine::ParamId::kDecay}},
    {ColumnKind::kParam, nullptr, {engine::ParamId::kSustain}},
    {ColumnKind::kParam, nullptr, {engine::ParamId::kRelease}},
    {ColumnKind::kPending, "CURVE", {}},
    {ColumnKind::kPending, "VELO", {}},
};

constexpr ColumnSpec kColsLfo[] = {
    {ColumnKind::kPending, "RATE", {}},
    {ColumnKind::kPending, "SHAPE", {}},
    {ColumnKind::kPending, "DEPTH", {}},
    {ColumnKind::kPending, "SYNC", {}},
    {ColumnKind::kPending, "FADE", {}},
    {ColumnKind::kPending, "PHASE", {}},
    {ColumnKind::kPending, "RETRIG", {}},
};

// The MOD page's source/dest/amount are the three fields ModRoute has; curve
// and enable are declared kPending until ModRoute grows (Design Decisions).
constexpr ColumnSpec kColsMod[] = {
    {ColumnKind::kRouteField, "SOURCE", {.field = RouteField::kSource}},
    {ColumnKind::kRouteField, "DEST", {.field = RouteField::kDest}},
    {ColumnKind::kRouteField, "AMOUNT", {.field = RouteField::kAmount}},
    {ColumnKind::kPending, "CURVE", {}},
    {ColumnKind::kPending, "ENABLE", {}},
};

// The kOutView settings, one four-column table per scope_mode (arch-design
// §7.4). No SOURCE column — the output view always monitors the current part.
// TIMEBASE and CYCLES are implemented (kViewCtl, backing OutputSettings); the
// remaining seven are display features not yet built, so they render kPending
// rather than as live-but-inert controls.
constexpr ColumnSpec kColsOutScope[] = {
    {ColumnKind::kViewCtl, "TIMEBASE", {.ctl = ViewCtl::kTimebase}},
    {ColumnKind::kPending, "SCALE", {}},
    {ColumnKind::kPending, "TRIGGER", {}},
    {ColumnKind::kPending, "HOLD", {}},
};

constexpr ColumnSpec kColsOutCycle[] = {
    {ColumnKind::kViewCtl, "CYCLES", {.ctl = ViewCtl::kCycles}},
    {ColumnKind::kPending, "SCALE", {}},
    {ColumnKind::kPending, "ALIGN", {}},
    {ColumnKind::kPending, "HOLD", {}},
};

constexpr ColumnSpec kColsOutSpec[] = {
    {ColumnKind::kPending, "RANGE", {}},
    {ColumnKind::kPending, "SCALE", {}},
    {ColumnKind::kPending, "AVERAGE", {}},
    {ColumnKind::kPending, "WINDOW", {}},
};

constexpr ColumnSpec kColsPatch[] = {
    {ColumnKind::kViewCtl, "CATEGORY", {.ctl = ViewCtl::kCategory}},
    {ColumnKind::kViewCtl, "SORT", {.ctl = ViewCtl::kSort}},
    {ColumnKind::kViewCtl, "FAV", {.ctl = ViewCtl::kFavourite}},
    {ColumnKind::kViewCtl, "ACTION", {.ctl = ViewCtl::kAction}},
};

constexpr ColumnSpec kColsConf[] = {
    {ColumnKind::kViewCtl, "DETENTS", {.ctl = ViewCtl::kDetents}},
    {ColumnKind::kViewCtl, "ACCEL", {.ctl = ViewCtl::kAccelMax}},
    {ColumnKind::kViewCtl, "THRESH", {.ctl = ViewCtl::kAccelThresh}},
    {ColumnKind::kViewCtl, "PRESS", {.ctl = ViewCtl::kLongPress}},
    {ColumnKind::kViewCtl, "FINE", {.ctl = ViewCtl::kFineDiv}},
    {ColumnKind::kViewCtl, "REFRESH", {.ctl = ViewCtl::kScopeRefresh}},
};

// Whether the page has at least one modulatable parameter column — the MOD
// applicability gate (arch-design §7.7). Membership is a descriptor flag
// (k_params[].modulatable), not an enum split, so this reads the table.
bool HasModulatableColumn(SubjectId s) {
  const PageDesc &page = k_pages[static_cast<int>(s)];
  for (std::uint8_t i = 0; i < page.n_cols; ++i) {
    if (page.cols[i].kind == ColumnKind::kParam &&
        engine::k_params[static_cast<std::size_t>(page.cols[i].param)]
            .modulatable)
      return true;
  }
  return false;
}

}  // namespace

// The kOutView column set per scope_mode (indexed by ScopeMode; kOff shares
// scope's table — it never coexists with kOutView after the kOff->kScope
// entry fix, but the slot is filled so the index is total).
const ColumnSpec *kOutColumns[4] = {
    kColsOutScope,  // kOff
    kColsOutScope,  // kScope
    kColsOutCycle,  // kCycle
    kColsOutSpec,   // kSpectrum
};

const PageDesc k_pages[static_cast<int>(SubjectId::kCount)] = {
    [static_cast<int>(SubjectId::kPart)] =
        {SubjectId::kPart, "PART", "PART", kColsPart, ColCount(kColsPart),
         ItemAxis::kNone, -1},
    [static_cast<int>(SubjectId::kOsc1)] =
        {SubjectId::kOsc1, "OSC1", "OSCILLATOR 1", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kOsc2)] =
        {SubjectId::kOsc2, "OSC2", "OSCILLATOR 2", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kOsc3)] =
        {SubjectId::kOsc3, "OSC3", "OSCILLATOR 3", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kOsc4)] =
        {SubjectId::kOsc4, "OSC4", "OSCILLATOR 4", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kFilt)] =
        {SubjectId::kFilt, "FILT", "FILTER", kColsFilt, ColCount(kColsFilt),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotFilter)},
    [static_cast<int>(SubjectId::kAmp)] =
        {SubjectId::kAmp, "AMP", "AMPLIFIER", kColsAmp, ColCount(kColsAmp),
         ItemAxis::kNone, -1},
    [static_cast<int>(SubjectId::kEnv1)] =
        {SubjectId::kEnv1, "ENV1", "ENVELOPE 1", kColsEnv, ColCount(kColsEnv),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kEnv2)] =
        {SubjectId::kEnv2, "ENV2", "ENVELOPE 2", kColsEnv, ColCount(kColsEnv),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kEnv3)] =
        {SubjectId::kEnv3, "ENV3", "ENVELOPE 3", kColsEnv, ColCount(kColsEnv),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kLfo1)] =
        {SubjectId::kLfo1, "LFO1", "LFO 1", kColsLfo, ColCount(kColsLfo),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kLfo2)] =
        {SubjectId::kLfo2, "LFO2", "LFO 2", kColsLfo, ColCount(kColsLfo),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kLfo3)] =
        {SubjectId::kLfo3, "LFO3", "LFO 3", kColsLfo, ColCount(kColsLfo),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kMod)] =
        {SubjectId::kMod, "MOD", "MODULATION", kColsMod, ColCount(kColsMod),
         ItemAxis::kSlots, -1},
    [static_cast<int>(SubjectId::kFx)] =
        {SubjectId::kFx, "FX", "EFFECTS", nullptr, 0, ItemAxis::kNone, -1},
    [static_cast<int>(SubjectId::kPatch)] =
        {SubjectId::kPatch, "PATCH", "PATCH", kColsPatch, ColCount(kColsPatch),
         ItemAxis::kPatches, -1},
    [static_cast<int>(SubjectId::kConf)] =
        {SubjectId::kConf, "CONF", "CONFIGURATION", kColsConf, ColCount(kColsConf),
         ItemAxis::kNone, -1},
};

Binding ResolveBinding(const NavState &nav, Control c) {
  Binding b{};
  b.kind = BindKind::kNone;
  b.slot = -1;
  b.column = -1;

  // Parameter-column encoders.
  if (c >= Control::kEnc0 && c <= Control::kEncLast) {
    const int n = static_cast<int>(c) - static_cast<int>(Control::kEnc0);
    b.column = static_cast<std::int8_t>(n);

    // kOutView: encoders address the per-mode output settings, not the page.
    if (nav.mode == ViewMode::kOutView) {
      if (n >= 4) return b;  // kNone: the output view has exactly four settings
      const ColumnSpec col =
          kOutColumns[static_cast<int>(nav.scope_mode)][n];
      b.kind = BindKind::kViewCtl;
      b.ctl = col.ctl;
      return b;
    }

    const PageDesc &page = k_pages[static_cast<int>(nav.subject)];
    const ColumnSpec col = Column<>(page, nav.group, n);

    if (nav.mode == ViewMode::kPerform) return b;  // reserved

    // MOD held: a parameter column becomes a route destination — encoder n
    // writes armed_source → cols[n]. The modulatable filter is applied at
    // dispatch (EngineControl::SetRoute rejects non-modulatable destinations),
    // keeping this function free of engine reads.
    if (nav.mode == ViewMode::kModArm && col.kind == ColumnKind::kParam) {
      b.kind = BindKind::kRouteAmount;
      b.param = engine::ParamRef{InstanceOf(nav.subject), col.param};
      return b;
    }

    switch (col.kind) {
      case ColumnKind::kNone:
        b.kind = BindKind::kNone;
        break;
      case ColumnKind::kPending:
        b.kind = BindKind::kPending;
        break;
      case ColumnKind::kParam:
        b.kind = BindKind::kParam;
        b.param = engine::ParamRef{InstanceOf(nav.subject), col.param};
        break;
      case ColumnKind::kRouteField:
        b.kind = BindKind::kRouteField;
        b.field = col.field;
        b.slot = static_cast<std::int8_t>(
            nav.item[static_cast<int>(nav.subject)]);
        break;
      case ColumnKind::kViewCtl:
        b.kind = BindKind::kViewCtl;
        b.ctl = col.ctl;
        break;
    }
    return b;
  }

  // Buttons and navigation.
  switch (c) {
    case Control::kNav1:
      b.kind = BindKind::kNavSubject;
      break;
    case Control::kNav2: {
      // MOD held turns the pane into the source list, so NAV2 walks sources
      // regardless of the page's item axis.
      if (nav.mode == ViewMode::kModArm) {
        b.kind = BindKind::kNavItem;
        break;
      }
      const PageDesc &page = k_pages[static_cast<int>(nav.subject)];
      b.kind = (page.item_axis == ItemAxis::kNone) ? BindKind::kNone
                                                   : BindKind::kNavItem;
      break;
    }
    case Control::kPart0:
    case Control::kPart1:
    case Control::kPart2:
    case Control::kPart3:
      b.kind = BindKind::kPartSelect;
      break;
    case Control::kMod:
      // MOD acts only where a route can land: a page with no modulatable
      // parameter column (the MOD page itself, PART/AMP/FX/PATCH/CONF) keeps
      // arming inert.
      b.kind = HasModulatableColumn(nav.subject) ? BindKind::kModeToggle
                                                 : BindKind::kNone;
      break;
    case Control::kGroup:
      // kOutView replaces the columns with the four output settings (one
      // group), so there is nothing to cycle.
      b.kind = (nav.mode == ViewMode::kOutView) ? BindKind::kNone
                                                 : BindKind::kGroupCycle;
      break;
    case Control::kOut: {
      // OUT acts only where there is a plot to embed the output into.
      const PageDesc &page = k_pages[static_cast<int>(nav.subject)];
      b.kind = (page.dyn_slot >= 0) ? BindKind::kOutToggle : BindKind::kNone;
      break;
    }
    case Control::kPerf:  // reserved (§2 non-goals)
    default:
      b.kind = BindKind::kNone;
      break;
  }
  return b;
}

}  // namespace nostromo
