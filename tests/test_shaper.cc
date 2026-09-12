// test_shaper.cc — unit tests for the per-voice drive shaper (CurveEval,
// AntiderivativeEval, ShaperProcess). Drives the public API directly, so the
// shaper is exercised without the envelope/filter/noise of the full render.

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

// float64 closed-form antiderivative reference: F(x) = log(cosh(x)).
static double FRef(double x) { return std::log(std::cosh(x)); }

// float64 closed-form first-order ADAA reference, with a configurable epsilon
// (so the epsilon precision-guard property can be checked independently).
static std::vector<double> AdaaRef(const std::vector<double> &x, double eps) {
    std::vector<double> y(x.size());
    double xp = 0.0, Fp = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double dx = x[n] - xp;
        const double fx = FRef(x[n]);
        if (std::fabs(dx) < eps) {
            y[n] = std::tanh((x[n] + xp) * 0.5);
        } else {
            y[n] = (fx - Fp) / dx;
        }
        xp = x[n];
        Fp = fx;
    }
    return y;
}

// Total out-of-band energy (harmonics + fold-back) relative to the fundamental,
// in dB. Assumes an integer number of input cycles at f0 so the fundamental
// DFT bin has no spectral leakage.
static double OutOfBandDb(const std::vector<double> &y, double f0, int n) {
    double total = 0.0;
    for (double s : y) total += s * s;
    double re = 0.0, im = 0.0;
    for (int k = 0; k < n; ++k) {
        const double ph = 2.0 * kPi * f0 * k / kFs;
        re += y[k] * std::cos(ph);
        im -= y[k] * std::sin(ph);
    }
    const double fund = 2.0 * (re * re + im * im) / n;
    const double oob = total - fund;
    if (oob <= 0.0 || fund <= 0.0) return -200.0;
    return 10.0 * std::log10(oob / fund);
}

int main() {
    /* 1. AntiderivativeEval matches float64 log(cosh) within the linear
     *    interpolation bound (~5e-4 absolute over the 256-entry table), and
     *    the |x| > 8 asymptote is exact. */
    {
        const double pts[] = {0.0, 0.5, 1.0, 2.0, 4.0, 7.9, -3.0};
        for (double x : pts) {
            const float got = AntiderivativeEval(CurveShape::kSoftSat, static_cast<float>(x));
            Check(std::fabs(static_cast<double>(got) - FRef(x)) < 6e-4,
                  "AntiderivativeEval within interpolation bound");
        }
        const double asy[] = {10.0, -12.0};
        for (double x : asy) {
            const float got = AntiderivativeEval(CurveShape::kSoftSat, static_cast<float>(x));
            Check(std::fabs(static_cast<double>(got) - (std::fabs(x) - std::log(2.0))) < 1e-5,
                  "AntiderivativeEval asymptote |x|-log2");
        }
    }

    /* 2. DC input: the |dx| < eps fallback fires every sample (no divide by
     *    zero), output stays bounded and NaN-free. */
    {
        Voice v{};
        bool ok = true;
        for (int i = 0; i < 1000; ++i) {
            const float y = ShaperProcess(&v, 0.5f);
            if (!std::isfinite(y) || std::fabs(y) > 1.0f) { ok = false; break; }
        }
        Check(ok, "DC input bounded and NaN-free");
        Voice v2{};
        Check(ShaperProcess(&v2, 0.0f) == 0.0f, "zero input -> zero output");
    }

    /* 3. Small-signal DC gain is unity (tanh'(0) = 1): after the first-sample
     *    transient, a small constant input passes through at unity. */
    {
        Voice v{};
        float y = 0.0f;
        for (int i = 0; i < 100; ++i) y = ShaperProcess(&v, 1e-3f);
        Check(std::fabs(y - 1e-3f) < 1e-6f, "small-signal DC gain unity");
    }

    /* 4. Group delay is ~half a sample: for a small sine the fallback path
     *    evaluates f at the midpoint, so y[n] ~= (x[n] + x[n-1]) / 2. */
    {
        Voice v{};
        const double A = 0.01, f = 100.0;
        double maxerr = 0.0, prev = 0.0;
        for (int n = 0; n < 2000; ++n) {
            const double x = A * std::sin(2.0 * kPi * f * n / kFs);
            const float y = ShaperProcess(&v, static_cast<float>(x));
            if (n > 0) {
                const double expect = (x + prev) * 0.5;
                const double err = std::fabs(static_cast<double>(y) - expect);
                if (err > maxerr) maxerr = err;
            }
            prev = x;
        }
        Check(maxerr < 1e-6, "group delay ~ half sample (midpoint fallback)");
    }

    /* 5. Anti-aliasing: the tabulated ADAA matches the float64 closed-form
     *    ADAA reference on a 7 kHz sine at x3 and x10 drive (the table's
     *    interpolation error is a smooth low-order distortion, not fold-back,
     *    so out-of-band energy is preserved to well under 1.5 dB). */
    {
        const int n = 4800;
        for (double drive : {3.0, 10.0}) {
            std::vector<double> x(n), ytab(n);
            for (int k = 0; k < n; ++k) x[k] = drive * std::sin(2.0 * kPi * 7000.0 * k / kFs);
            Voice v{};
            for (int k = 0; k < n; ++k) ytab[k] = static_cast<double>(ShaperProcess(&v, static_cast<float>(x[k])));
            const std::vector<double> yref = AdaaRef(x, 1e-3);
            const double a = OutOfBandDb(ytab, 7000.0, n);
            const double b = OutOfBandDb(yref, 7000.0, n);
            Check(std::fabs(a - b) < 1.5, "ADAA anti-aliasing matches closed-form reference");
        }
    }

    /* 6. State advance: ShaperProcess leaves the voice at {x, F(x)}. */
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
