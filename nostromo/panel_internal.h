/// @file panel_internal.h
/// @brief Internal panel wiring used only by Interaction::Init.
///
/// These bind the panel's back-pointers and are deliberately NOT part of the
/// public panel API. Hosts (the desktop sim in host/main.cc and the target
/// smoke in target/zephyr/cm33/src/main.cc) must bind the panel through
/// Interaction::Init — the single wiring point that sets both the interaction
/// and the engine. Keeping the setters out of panel.h makes piecemeal wiring
/// impossible, so the two hosts cannot drift: a host that binds the engine
/// directly and skips the interaction layer leaves the panel's navigation
/// back-pointer null and faults on the first draw.
#pragma once

#include "panel.h"

namespace nostromo {

struct Interaction;

void PanelSetInteraction(Panel *p, const Interaction *it);
void PanelSetEngine(Panel *p, engine::EngineControl *control);

}  // namespace nostromo
