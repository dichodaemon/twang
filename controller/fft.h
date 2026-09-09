/// @file fft.h
/// @brief Radix-2 complex FFT for the display spectrum.
///
/// Control (UI) side only — never in the audio path.

#pragma once

namespace controller {

/// The constant pi, as a float.
inline constexpr float kPi = 3.14159265358979323846f;

/// @brief In-place iterative radix-2 complex FFT.
/// @param re Real part; holds the input, overwritten with the transformed
/// real part.
/// @param im Imaginary part; holds the input, overwritten with the
/// transformed imaginary part.
/// @param n Transform length; a power of two.
void Fft(float *re, float *im, int n);

}  // namespace controller
