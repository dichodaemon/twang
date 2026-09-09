/// @file fft.h
/// @brief Radix-2 complex FFT for the display spectrum.
///
/// Control (UI) side only — never in the audio path.

#pragma once

namespace sim {

inline constexpr float kPi = 3.14159265358979323846f;

/// In-place iterative radix-2 complex FFT. `n` must be a power of two.
/// `re`/`im` hold the input and are overwritten with the transform.
void Fft(float *re, float *im, int n);

}  // namespace sim
