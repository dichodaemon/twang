// feel.cc — the runtime-tunable feel defaults.
//
// These are the starting values the CONF page edits (§7.10); they are not
// constants. They are tuned by hand on the prototype, so they live here, once,
// rather than scattered as literals in the recognizer and dispatcher.

#include "feel.h"

namespace nostromo {

FeelProfile g_feel = {
    24,   // detents_per_rev
    4,    // accel_max_default
    8,    // accel_threshold_dps
    500,  // long_press_ms
    10,   // fine_divisor
};

}  // namespace nostromo
