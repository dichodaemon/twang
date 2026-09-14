/// @file feel.h
/// @brief Runtime-tunable feel parameters for the interaction layer.
///
/// Anything judged by hand — detent scaling, acceleration threshold, long-press
/// duration, the fine divisor — is runtime state, not a constant, and is edited
/// from the CONF page (arch-design §7.10). A feel experiment must not cost a
/// rebuild and a reflash.
#pragma once

#include <cstdint>

namespace nostromo {

/// The values judged by hand, mutable at runtime.
struct FeelProfile {
  std::uint8_t  detents_per_rev;      ///< the encoder's own detent count
  std::uint8_t  accel_max_default;    ///< ordinary parameters; 1 = none
  std::uint16_t accel_threshold_dps;  ///< detents/second above which accel engages
  std::uint32_t long_press_ms;        ///< press-and-hold threshold
  std::uint8_t  fine_divisor;         ///< hold-and-turn divisor (10 = x1/10)
};

/// Mutable; edited from the CONF page.
extern FeelProfile g_feel;

}  // namespace nostromo
