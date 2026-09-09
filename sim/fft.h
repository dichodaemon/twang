/// @file fft.h
/// @brief Radix-2 complex FFT for the display spectrum.
///
/// Header-only and side-effect-free, like engine/dsp.h, so it can be timed
/// and verified in isolation. Control (UI) side only — never in the audio
/// path.

#pragma once

#include <cmath>

namespace sim {

inline constexpr float kPi = 3.14159265358979323846f;

/// In-place iterative radix-2 complex FFT. `n` must be a power of two.
/// `re`/`im` hold the input and are overwritten with the transform.
inline void Fft(float *re, float *im, int n) {
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            const float tr = re[i]; re[i] = re[j]; re[j] = tr;
            const float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    // Butterflies.
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * kPi / static_cast<float>(len);
        const float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float vr = re[b] * cr - im[b] * ci;
                const float vi = re[b] * ci + im[b] * cr;
                const float ur = re[a], ui = im[a];
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
                const float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

}  // namespace sim
