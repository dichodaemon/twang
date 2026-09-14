/// @file interaction.h
/// @brief Navigation and input vocabulary for the interaction layer, plus its
/// public entry points.
///
/// The interaction layer (nostromo-interaction_arch-design.md) sits between
/// the input driver and the engine's parameter API: it resolves what each
/// physical control currently drives, applies gestures to that binding, and
/// marks the affected DYN slots dirty through the panel's MarkDirty. This
/// header owns the vocabulary shared by pages.h / surface.h / feel.h /
/// interaction.cc, and the four entry points. It performs no drawing and
/// touches no invalidation state.
#pragma once

#include <cstdint>

#include "engine.h"
#include "geom.h"

namespace nostromo {

/// Logical controls, independent of the physical surface (see SurfaceProfile
/// in surface.h for the physical→logical map). Encoders address columns
/// 0..kColumns−1; the rest are buttons.
enum class Control : std::uint8_t {
  kNav1 = 0,       ///< subject pane cursor
  kNav2,           ///< item axis cursor
  kEnc0,           ///< parameter column 0 .. kColumns-1
  kEncLast = kEnc0 + geom::kColumns - 1,
  kPart0, kPart1, kPart2, kPart3,
  kMod,            ///< momentary (arm) and tap (view)
  kPerf,           ///< latching, reserved
  kGroup,          ///< momentary, column-group cycle
  kOut,            ///< momentary, jump to kOutScope and back
  kCount,
};

/// The n-th parameter encoder (0 <= n < geom::kColumns). kEnc0..kEncLast are
/// contiguous but only the endpoints are named, so encoders are addressed by
/// index through this helper.
constexpr Control Enc(int n) {
  return static_cast<Control>(static_cast<int>(Control::kEnc0) + n);
}

/// Button edge for a pure press/release event.
enum class Edge : std::uint8_t { kNone, kDown, kUp };

/// One physical event, already mapped to a logical control.
struct InputEvent {
  Control       control;
  std::int8_t   detents;   ///< signed; 0 for a pure button event
  Edge          edge;      ///< kNone for a pure turn
  std::uint32_t t_ms;      ///< monotonic
};

/// A recognised input pattern (see GestureRecognizer in interaction.cc).
enum class Gesture : std::uint8_t {
  kNone = 0,     ///< no gesture (a press start, or an absorbed release)
  kTurn,         ///< detents, no press held
  kHoldTurn,     ///< detents while pressed — fine adjust
  kPressShort,   ///< press and release under g_feel.long_press_ms, no detent
  kPressLong,    ///< press held past g_feel.long_press_ms, no detent
};

/// Per-control press state for gesture recognition. One per Control, held by
/// the interaction layer; the recognizer mutates it and reads g_feel.
struct PressState {
  bool          pressed = false;   ///< a press is in progress
  std::uint32_t press_t_ms = 0;    ///< kDown timestamp
  bool          detent = false;    ///< a detent arrived during this press
};

/// Convert one InputEvent to a Gesture, mutating the per-control `st`. The
/// hold-versus-press disambiguation lives here (arch-design §5): a detent
/// during a press is kHoldTurn and the release is absorbed (emits kNone).
Gesture Recognize(const InputEvent &ev, PressState &st);

/// A subject is an entry in the navigation pane. Enum order is pane order —
/// NAV1 walks index order — which is arch-design §7.6's order, not §7.3's
/// (the two disagree; §7.6 wins because g_pages is indexed by SubjectId).
enum class SubjectId : std::uint8_t {
  kPart = 0,     ///< part-level settings
  kOsc1, kOsc2, kOsc3, kOsc4,
  kFilt, kAmp,
  kEnv1, kEnv2, kEnv3,
  kLfo1, kLfo2, kLfo3,
  kMod,
  kOutScope, kOutCycle, kOutSpec,  ///< globals, below the pane rule
  kFx,
  kPatch, kConf,
  kCount,        ///< 20; asserted == geom::kSubjectCount in pages.h
};

/// Latched / momentary navigation modes.
enum class ViewMode : std::uint8_t {
  kEdit = 0,     ///< default
  kModArm,       ///< MOD held — momentary
  kModView,      ///< MOD tapped — latched, LED lit
  kPerform,      ///< PERF — latched, LED lit
};

/// A navigation position: everything the OUT button must restore.
struct NavPos {
  SubjectId    subject;
  std::uint8_t group;
  std::int8_t  focus_col;
};

/// The complete navigation state (arch-design §7.3). Plain value type,
/// serialisable, no pointers.
struct NavState {
  std::uint8_t part;                  ///< [0, engine::kNumParts)
  SubjectId    subject;               ///< pane cursor; global across parts
  std::uint8_t group;                 ///< active column group
  std::uint8_t item[static_cast<int>(SubjectId::kCount)];  ///< per-page item cursor
  std::int8_t  focus_col;             ///< focused column, -1 = none
  ViewMode     mode;
  engine::ModSourceId armed_source;  ///< persists between kModArm entries
  NavPos       prev;                  ///< return position for the OUT button
};

struct SurfaceProfile;  ///< defined in surface.h
struct Panel;           ///< defined in panel.h

/// @brief Initialise the layer (control thread, once).
/// Binds the panel (the MarkDirty target) and surface; loads g_feel defaults;
/// zeroes NavState; marks every plot slot dirty. Reports once if
/// surface.n_encoders < geom::kColumns.
void InteractionInit(Panel *panel, const SurfaceProfile &surface);

/// @brief Feed one logical input event.
/// Precondition: ev.control < Control::kCount, ev.t_ms monotonic.
void InteractionOnInput(const InputEvent &ev);

/// @brief Create or update a modulation route (control thread).
/// Finds the matching (src, dst) route or the lowest free slot; returns false
/// if the table is full and no slot matched. A zero amount keeps the slot.
/// `dst` is a full ParamRef — the instance comes from ResolveBinding and is
/// never re-derived here (Design Decisions).
bool InteractionCreateRoute(std::uint8_t part, engine::ModSourceId src,
                            engine::ParamRef dst, float amount);

/// @brief Read-only view of the navigation state for the renderer (pane/header
/// chrome and DYN hooks). Returns a const reference so the screen cannot write
/// back — the layer is the sole writer of NavState.
const NavState &InteractionNavState();

}  // namespace nostromo
