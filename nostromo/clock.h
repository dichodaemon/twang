/// @file clock.h
/// @brief Monotonic millisecond clock, build-selected per platform.
///
/// Shared code needs a monotonic millisecond clock; the source differs per
/// platform (Zephyr's k_uptime_get_32, FreeRTOS ticks, bare-metal SysTick,
/// hosted std::chrono). One declaration, one backend per platform, selected
/// by the build — a new target adds a clock_<target>.cc and one build line,
/// never a preprocessor branch (mirrors the audio/ output abstraction).

#pragma once

#include <cstdint>

namespace nostromo {

/// @brief Monotonic milliseconds since an arbitrary epoch (e.g. boot).
///
/// Backends: clock_host.cc (std::chrono::steady_clock, desktop) and
/// clock_zephyr.cc (k_uptime_get_32, cm33). 32-bit, so it wraps after ~49
/// days; callers use only deltas.
std::uint32_t NowMs();

}  // namespace nostromo
