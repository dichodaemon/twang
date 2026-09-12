// test_shaper.cc — unit tests for the per-voice drive shaper: the clamped
// Padé curve (CurveEval), its shifted antiderivative (AntiderivativeEval), and
// first-order ADAA (ShaperProcess). Drives the public API directly.

#include <cmath>
#include <cstdio>
#include <vector>

#include "engine.h"

using namespace engine;

static int g_failures = 0;

static void Check(bool ok, const char *msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_failures;
    }
}

static const double kPi = 3.14159265358979323846;
static const double kFs = static_cast<double>(kSampleRate);
static const double kLim = static_cast<double>(kFTableMax);         // 3.0
static const double kAsym = static_cast<double>(kFTableAsym);       // −0.651607519
static const double kRefEps = 1e-12;  // reference uses the pure quotient (ideal ADAA)

// float64 reference of the shipped curve and its antiderivative (independent
// of the implementation's table/float path).
static double CurveRef(double x) {
    if (x >= kLim) return 1.0;
    if (x <= -kLim) return -1.0;
    return x * (27.0 + x * x) / (27.0 + 9.0 * x * x);
}
static double FRef(double x) {
    const double ax = std::fabs(x);
    if (ax >= kLim) return ax + kAsym;
    return x * x / 18.0 + (4.0 / 3.0) * std::log((x * x + 3.0) / 3.0);
}

// float64 closed-form first-order ADAA reference (same curve, exact F, same ε).
static std::vector<double> AdaaRef(const std::vector<double> &x) {
    std::vector<double> y(x.size());
    double xp = 0.0, Fp = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double dx = x[n] - xp;
        const double fx = FRef(x[n]);
        if (std::fabs(dx) < kRefEps) {
            y[n] = CurveRef((x[n] + xp) * 0.5);
        } else {
            y[n] = (fx - Fp) / dx;
        }
        xp = x[n];
        Fp = fx;
    }
    return y;
}

int main() {
    /* 1. CurveEval: f(0)=0, f'(0)=1, f(±3)=±1, clamp, monotone. */
    {
        Check(CurveEval(CurveShape::kSoftSat, 0.0f) == 0.0f, "f(0)=0");
        Check(std::fabs(CurveEval(CurveShape::kSoftSat, 1e-3f) - 1e-3f) < 1e-6f,
              "f'(0)=1 (small-signal)");
        Check(CurveEval(CurveShape::kSoftSat, 3.0f) == 1.0f, "f(3)=1");
        Check(CurveEval(CurveShape::kSoftSat, -3.0f) == -1.0f, "f(-3)=-1");
        Check(CurveEval(CurveShape::kSoftSat, 4.0f) == 1.0f, "clamp x=4 -> 1");
        Check(CurveEval(CurveShape::kSoftSat, -10.0f) == -1.0f, "clamp x=-10 -> -1");
        const float f1 = CurveEval(CurveShape::kSoftSat, 1.0f);
        const float f2 = CurveEval(CurveShape::kSoftSat, 2.0f);
        const float f3 = CurveEval(CurveShape::kSoftSat, 2.9f);
        Check(f1 < f2 && f2 < f3 && f3 < 1.0f, "monotone on [0,3]");
    }

    /* 2. AntiderivativeEval: within the linear-interpolation bound (~7e-5). */
    {
        const double pts[] = {0.0, 0.5, 1.0, 2.0, 2.9, -1.0, -2.5};
        for (double x : pts) {
            const float got = AntiderivativeEval(CurveShape::kSoftSat, static_cast<float>(x));
            Check(std::fabs(static_cast<double>(got) - FRef(x)) < 1e-4,
                  "AntiderivativeEval within interpolation bound");
        }
        const double asy[] = {3.5, -4.0, 20.0};
        for (double x : asy) {
            const float got = AntiderivativeEval(CurveShape::kSoftSat, static_cast<float>(x));
            Check(std::fabs(static_cast<double>(got) - (std::fabs(x) + kAsym)) < 1e-5,
                  "AntiderivativeEval asymptote |x|+kFTableAsym");
        }
    }

    /* 3. DC input: bounded and NaN-free, in both the fallback and asymptote. */
    {
        Voice v{};
        bool ok = true;
        for (int i = 0; i < 1000; ++i) {
            const float y = ShaperProcess(&v, 0.5f);
            if (!std::isfinite(y) || std::fabs(y) > 1.0f) { ok = false; break; }
        }
        Check(ok, "DC 0.5 bounded and NaN-free (fallback)");
        Voice v2{};
        ok = true;
        for (int i = 0; i < 1000; ++i) {
            const float y = ShaperProcess(&v2, 5.0f);  // |x|>3 -> asymptote branch
            if (!std::isfinite(y) || std::fabs(y) > 1.0f) { ok = false; break; }
        }
        Check(ok, "DC 5.0 bounded and NaN-free (asymptote)");
        Voice v3{};
        Check(ShaperProcess(&v3, 0.0f) == 0.0f, "zero input -> zero output");
    }

    /* 4. Small-signal DC gain is unity (f'(0) = 1). */
    {
        Voice v{};
        float y = 0.0f;
        for (int i = 0; i < 100; ++i) y = ShaperProcess(&v, 1e-3f);
        Check(std::fabs(y - 1e-3f) < 1e-6f, "small-signal DC gain unity");
    }

    /* 5. Group delay is ~half a sample (midpoint fallback). */
    {
        Voice v{};
        const double A = 0.01, f = 100.0;
        double maxerr = 0.0, prev = 0.0;
        for (int n = 0; n < 2000; ++n) {
            const double x = A * std::sin(2.0 * kPi * f * n / kFs);
            const float y = ShaperProcess(&v, static_cast<float>(x));
            if (n > 0) {
                const double err = std::fabs(static_cast<double>(y) - (x + prev) * 0.5);
                if (err > maxerr) maxerr = err;
            }
            prev = x;
        }
        Check(maxerr < 1e-6, "group delay ~ half sample (midpoint fallback)");
    }

    /* 6. Fresh-voice first sample: F(0)=0, so no additive-constant spike.
     *    x=0.05 must give ~F(0.05)/0.05 = 0.025, not ~1.46/0.05 = 29. */
    {
        Voice v{};  // xp = 0, Fp = 0
        const float y = ShaperProcess(&v, 0.05f);
        Check(std::fabs(y - 0.025f) < 0.005f,
              "fresh voice first sample ~ F(x)/x (no additive-constant spike)");
    }

    /* 7. Numerical fidelity (relative to distortion): across the drive × freq
     *    grid, the shaper's RMS error is ≥ 40 dB below the curve's own harmonic
     *    content, or the absolute error is ≤ −80 dBFS. Drive is the shaper input
     *    amplitude (matching the review's final.cc grid). */
    {
        const double drives[] = {1.0, 3.0, 10.0};
        const double freqs[] = {110.0, 220.0, 440.0, 880.0, 2000.0};
        const int n = 16384;
        bool all_ok = true;
        for (double drive : drives) {
            for (double f : freqs) {
                std::vector<double> x(n);
                for (int k = 0; k < n; ++k)
                    x[k] = drive * std::sin(2.0 * kPi * f * k / kFs);
                Voice v{};
                std::vector<double> y(n);
                for (int k = 0; k < n; ++k)
                    y[k] = static_cast<double>(ShaperProcess(&v, static_cast<float>(x[k])));
                const std::vector<double> yref = AdaaRef(x);
                double num = 0.0, den = 0.0;
                for (int k = 0; k < n; ++k) {
                    const double e = y[k] - yref[k];
                    const double h = yref[k] - x[k];  // curve's nonlinearity
                    num += e * e;
                    den += h * h;
                }
                const double rms_err = std::sqrt(num / n);
                const double rms_harm = std::sqrt(den / n);
                const double ratio_db = 20.0 * std::log10(rms_err / rms_harm);
                const double abs_db = 20.0 * std::log10(rms_err);
                if (!(ratio_db <= -40.0 || abs_db <= -80.0)) {
                    std::printf("  grid fail: drive=%.0f f=%.0f "
                                "ratio=%.1f dB abs=%.1f dBFS\n",
                                drive, f, ratio_db, abs_db);
                    all_ok = false;
                }
            }
        }
        Check(all_ok, "relative bar: error >= 40 dB below harmonic content (or abs <= -80 dBFS)");
    }

    /* 8. State advance: ShaperProcess leaves the voice at {x, F(x)}. */
    {
        Voice v{};
        const float x = 0.7f;
        ShaperProcess(&v, x);
        Check(v.shaper.xp == x, "shaper.xp advanced to x");
        Check(std::fabs(v.shaper.Fp - AntiderivativeEval(CurveShape::kSoftSat, x)) < 1e-6f,
              "shaper.Fp advanced to F(x)");
    }

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("PASS: shaper\n");
    return 0;
}
