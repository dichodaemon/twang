// pages.cc — the page table, one PageDesc per subject in pane order.
//
// The single read-only source of truth for what each subject's screen shows
// (nostromo-interaction_arch-design.md §7.6). Columns are ordered: the head of
// each list is the page's hot set and stays group 0 at any column count.
// Grouping into columns is derived from geom::kColumns by GroupCount/Column in
// pages.h — never authored here.

#include "pages.h"

#include "screens.h"  // SlotIdx (kSlotOsc, …) for dyn_slot

namespace nostromo {

namespace {

// Column count helper: the table never repeats a count that the array already
// states, so the two cannot drift.
template <std::size_t N>
constexpr std::uint8_t ColCount(const ColumnSpec (&)[N]) {
  return static_cast<std::uint8_t>(N);
}

// kPending columns render dim and inert until the engine's parameter surface
// grows (arch-design §13.3); they keep the page taxonomy honest.

constexpr ColumnSpec kColsPart[] = {
    {ColumnKind::kPending, {}},  // chan
    {ColumnKind::kPending, {}},  // voices
    {ColumnKind::kPending, {}},  // transpose
    {ColumnKind::kPending, {}},  // glide
    {ColumnKind::kPending, {}},  // mono/poly
    {ColumnKind::kPending, {}},  // bend range
};

constexpr ColumnSpec kColsOsc[] = {
    {ColumnKind::kPending, {}},                              // wave
    {ColumnKind::kParam, {engine::ParamId::kPitchCoarse}},   // coarse
    {ColumnKind::kPending, {}},                              // fine
    {ColumnKind::kPending, {}},                              // level
    {ColumnKind::kPending, {}},                              // shape
    {ColumnKind::kPending, {}},                              // pan
    {ColumnKind::kPending, {}},                              // sync
};

constexpr ColumnSpec kColsFilt[] = {
    {ColumnKind::kParam, {engine::ParamId::kCutoff}},        // cutoff
    {ColumnKind::kParam, {engine::ParamId::kResonance}},     // resonance
    {ColumnKind::kPending, {}},                              // env amt
    {ColumnKind::kParam, {engine::ParamId::kDrive}},         // drive
    {ColumnKind::kParam, {engine::ParamId::kKeyFollowDepth}},  // keytrack
    {ColumnKind::kPending, {}},                              // mode
};

constexpr ColumnSpec kColsAmp[] = {
    {ColumnKind::kParam, {engine::ParamId::kAmp}},  // level
    {ColumnKind::kPending, {}},                     // pan
    {ColumnKind::kPending, {}},                     // velo sens
    {ColumnKind::kPending, {}},                     // send A
    {ColumnKind::kPending, {}},                     // send B
};

constexpr ColumnSpec kColsEnv[] = {
    {ColumnKind::kParam, {engine::ParamId::kAttack}},   // A
    {ColumnKind::kParam, {engine::ParamId::kDecay}},    // D
    {ColumnKind::kParam, {engine::ParamId::kSustain}},  // S
    {ColumnKind::kParam, {engine::ParamId::kRelease}},  // R
    {ColumnKind::kPending, {}},                         // curve
    {ColumnKind::kPending, {}},                         // velo sens
};

constexpr ColumnSpec kColsLfo[] = {
    {ColumnKind::kPending, {}},  // rate
    {ColumnKind::kPending, {}},  // shape
    {ColumnKind::kPending, {}},  // depth
    {ColumnKind::kPending, {}},  // sync
    {ColumnKind::kPending, {}},  // fade
    {ColumnKind::kPending, {}},  // phase
    {ColumnKind::kPending, {}},  // retrig
};

// The MOD page's source/dest/amount are the three fields ModRoute has; curve
// and enable are declared kPending until ModRoute grows (Design Decisions).
constexpr ColumnSpec kColsMod[] = {
    {ColumnKind::kRouteField, {.field = RouteField::kSource}},  // source
    {ColumnKind::kRouteField, {.field = RouteField::kDest}},    // dest
    {ColumnKind::kRouteField, {.field = RouteField::kAmount}},  // amount
    {ColumnKind::kPending, {}},                                 // curve
    {ColumnKind::kPending, {}},                                 // enable
};

constexpr ColumnSpec kColsOutScope[] = {
    {ColumnKind::kPending, {}},  // source
    {ColumnKind::kPending, {}},  // timebase
    {ColumnKind::kPending, {}},  // scale
    {ColumnKind::kPending, {}},  // trigger
    {ColumnKind::kPending, {}},  // hold
};

constexpr ColumnSpec kColsOutCycle[] = {
    {ColumnKind::kPending, {}},  // source
    {ColumnKind::kPending, {}},  // cycles
    {ColumnKind::kPending, {}},  // scale
    {ColumnKind::kPending, {}},  // align
    {ColumnKind::kPending, {}},  // hold
};

constexpr ColumnSpec kColsOutSpec[] = {
    {ColumnKind::kPending, {}},  // source
    {ColumnKind::kPending, {}},  // range
    {ColumnKind::kPending, {}},  // scale
    {ColumnKind::kPending, {}},  // average
    {ColumnKind::kPending, {}},  // window
};

constexpr ColumnSpec kColsPatch[] = {
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kCategory}},   // category
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kSort}},       // sort
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kFavourite}},  // favourite
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kAction}},     // action
};

constexpr ColumnSpec kColsConf[] = {
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kDetents}},      // detents/rev
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kAccelMax}},     // accel max
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kAccelThresh}},  // accel thresh
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kLongPress}},    // long press
    {ColumnKind::kViewCtl, {.ctl = ViewCtl::kFineDiv}},      // fine div
};

}  // namespace

const PageDesc g_pages[static_cast<int>(SubjectId::kCount)] = {
    [static_cast<int>(SubjectId::kPart)] =
        {SubjectId::kPart, "PART", kColsPart, ColCount(kColsPart),
         ItemAxis::kNone, -1},
    [static_cast<int>(SubjectId::kOsc1)] =
        {SubjectId::kOsc1, "OSC1", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kOsc2)] =
        {SubjectId::kOsc2, "OSC2", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kOsc3)] =
        {SubjectId::kOsc3, "OSC3", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kOsc4)] =
        {SubjectId::kOsc4, "OSC4", kColsOsc, ColCount(kColsOsc),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOsc)},
    [static_cast<int>(SubjectId::kFilt)] =
        {SubjectId::kFilt, "FILT", kColsFilt, ColCount(kColsFilt),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotFilter)},
    [static_cast<int>(SubjectId::kAmp)] =
        {SubjectId::kAmp, "AMP", kColsAmp, ColCount(kColsAmp),
         ItemAxis::kNone, -1},
    [static_cast<int>(SubjectId::kEnv1)] =
        {SubjectId::kEnv1, "ENV1", kColsEnv, ColCount(kColsEnv),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kEnv2)] =
        {SubjectId::kEnv2, "ENV2", kColsEnv, ColCount(kColsEnv),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kEnv3)] =
        {SubjectId::kEnv3, "ENV3", kColsEnv, ColCount(kColsEnv),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kLfo1)] =
        {SubjectId::kLfo1, "LFO1", kColsLfo, ColCount(kColsLfo),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kLfo2)] =
        {SubjectId::kLfo2, "LFO2", kColsLfo, ColCount(kColsLfo),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kLfo3)] =
        {SubjectId::kLfo3, "LFO3", kColsLfo, ColCount(kColsLfo),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotEnv)},
    [static_cast<int>(SubjectId::kMod)] =
        {SubjectId::kMod, "MOD", kColsMod, ColCount(kColsMod),
         ItemAxis::kSlots, -1},
    [static_cast<int>(SubjectId::kOutScope)] =
        {SubjectId::kOutScope, "SCOPE", kColsOutScope, ColCount(kColsOutScope),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOut)},
    [static_cast<int>(SubjectId::kOutCycle)] =
        {SubjectId::kOutCycle, "CYCLE", kColsOutCycle, ColCount(kColsOutCycle),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOut)},
    [static_cast<int>(SubjectId::kOutSpec)] =
        {SubjectId::kOutSpec, "SPECTRUM", kColsOutSpec, ColCount(kColsOutSpec),
         ItemAxis::kNone, static_cast<std::int8_t>(kSlotOut)},
    [static_cast<int>(SubjectId::kFx)] =
        {SubjectId::kFx, "FX", nullptr, 0, ItemAxis::kNone, -1},
    [static_cast<int>(SubjectId::kPatch)] =
        {SubjectId::kPatch, "PATCH", kColsPatch, ColCount(kColsPatch),
         ItemAxis::kPatches, -1},
    [static_cast<int>(SubjectId::kConf)] =
        {SubjectId::kConf, "CONF", kColsConf, ColCount(kColsConf),
         ItemAxis::kNone, -1},
};

}  // namespace nostromo
