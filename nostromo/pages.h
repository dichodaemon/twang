/// @file pages.h
/// @brief Page table and binding-resolution types for the interaction layer.
///
/// A page describes one subject's ordered columns, its item axis, and its plot
/// slot. Column grouping is computed from geom::kColumns, never authored
/// (arch-design §7.4). A binding resolves a physical control to what it drives
/// given the navigation state. Everything here is pure data or a pure function;
/// no engine reads, no drawing.
#pragma once

#include <cstdint>

#include "engine.h"
#include "geom.h"
#include "interaction.h"

namespace nostromo {

/// What a column drives.
enum class ColumnKind : std::uint8_t {
  kNone = 0,     ///< past the end of a partial final group
  kParam,        ///< an engine parameter of the current subject
  kPending,      ///< declared, but ParamId does not define it yet
  kRouteField,   ///< a field of the item the page's item axis selects
  kViewCtl,      ///< a browser or settings control
};

/// The three fields a ModRoute actually has. The MOD page's `curve`/`enable`
/// columns are ColumnKind::kPending until ModRoute grows (Design Decisions).
enum class RouteField : std::uint8_t { kSource, kDest, kAmount };

/// Browser / settings controls (PATCH and CONF pages).
enum class ViewCtl : std::uint8_t {
  kCategory, kSort, kFavourite, kAction,                    // PATCH
  kDetents, kAccelMax, kAccelThresh, kLongPress, kFineDiv,  // CONF
  kScopeRefresh,                                            // CONF (output refresh)
  kTimebase, kCycles, kRange,                               // kOutView col 1
  kScale,                                                   // kOutView col 2 (shared)
  kTrigger, kAlign, kAverage,                               // kOutView col 3
  kHold, kWindow,                                           // kOutView col 4
};

/// A tagged column reference. A column *declares a kind*: `param` names a
/// ParamId, not a ParamRef — the instance is resolved from SubjectId later.
/// `label` is the column-header text for kPending/kRouteField/kViewCtl columns
/// (entities with no ParamId). A kParam column derives its header from
/// ParamDesc::long_name, so its label is nullptr.
struct ColumnSpec {
  ColumnKind    kind;
  const char   *label;   ///< header text; nullptr for kParam (derived)
  union {
    engine::ParamId param;
    RouteField      field;
    ViewCtl         ctl;
  };
};

/// What NAV2 walks on a page.
enum class ItemAxis : std::uint8_t {
  kNone = 0,   ///< NAV2 idle
  kSlots,      ///< modulation slots
  kPatches,    ///< patch list within the selected category
  kRoutes,     ///< route list of the focused column
};

/// One subject's page: an ordered column list, its item axis, and its plot.
struct PageDesc {
  SubjectId         subject;
  const char       *label;     ///< pane text (short); see the length invariant (§9)
  const char       *long_name; ///< title text (full); the pane's expansion
  const ColumnSpec *cols;      ///< ORDERED. Head of the list is the hot set.
  std::uint8_t      n_cols;   ///< grouping is derived, not authored
  ItemAxis          item_axis;
  std::int8_t       dyn_slot; ///< plot slot, or -1
};

/// Groups are ceil(n/E) slices of `cols`. Changing E re-groups every page.
/// Templated so test_bindings.cc can sweep E ∈ {4,5,6,7} without editing
/// geom::kColumns.
template <int E = geom::kColumns>
constexpr int GroupCount(const PageDesc &p) {
  return (p.n_cols + E - 1) / E;
}

/// The column at (group, col); kNone past the end of a partial final group.
template <int E = geom::kColumns>
constexpr ColumnSpec Column(const PageDesc &p, int group, int col) {
  const int i = group * E + col;
  return i < p.n_cols ? p.cols[i] : ColumnSpec{ColumnKind::kNone, "", {}};
}

/// What a resolved control drives.
enum class BindKind : std::uint8_t {
  kNone = 0,
  kParam,        ///< ColumnKind::kParam — a parameter of the current subject
  kRouteField,   ///< ColumnKind::kRouteField — a field of the item NAV2 selects
  kViewCtl,      ///< ColumnKind::kViewCtl — a browser or settings control
  kRouteAmount,  ///< a route amount reached through kModArm / kModView
  kNavSubject, kNavItem,
  kPartSelect, kModeToggle, kGroupCycle, kOutToggle,
  kPending,      ///< ColumnKind::kPending — declared, unimplemented
};

/// The resolution of a physical control to what it drives. A binding names a
/// *kind plus an instance*: `param` is a full ParamRef, filled once by
/// ResolveBinding, so the Dispatcher never re-derives subject → instance.
struct Binding {
  BindKind kind;
  union {
    engine::ParamRef param;   ///< kParam, kRouteAmount
    RouteField       field;   ///< kRouteField
    ViewCtl          ctl;     ///< kViewCtl
  };
  std::int8_t slot;     ///< kRouteAmount / kRouteField: engine route slot, -1 to allocate
  std::int8_t column;   ///< originating column, -1 if not column-derived
};

/// The page table, indexed by SubjectId, in pane order.
extern const PageDesc k_pages[static_cast<int>(SubjectId::kCount)];

/// Pure (NavState, Control) → Binding. No side effects; reads only const
/// tables (k_pages and the engine parameter-descriptor table).
Binding ResolveBinding(const NavState &nav, Control c);

/// SubjectId must enumerate exactly the subjects the pane renders.
static_assert(static_cast<int>(SubjectId::kCount) == geom::kSubjectCount,
              "SubjectId::kCount must equal geom::kSubjectCount");

}  // namespace nostromo
