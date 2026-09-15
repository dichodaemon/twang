#include "params.h"

#include <cmath>
#include <cstdio>

namespace engine {

constexpr ParamDesc k_params[static_cast<std::size_t>(ParamId::kCount)] = {
    [static_cast<std::size_t>(ParamId::kCutoff)] =
        { "cutoff", "Hz", 20.0f, 20000.0f, 1.0f, ParamCurve::kExponential,
          static_cast<std::uint16_t>(offsetof(Part, params) + 0 * sizeof(float)), 0, true,
          nullptr, 0,
          CombinationClass::kAdditive, 4, false },
    [static_cast<std::size_t>(ParamId::kResonance)] =
        { "resonance", "%", 0.0f, 100.0f, 0.0f, ParamCurve::kLinear,
          static_cast<std::uint16_t>(offsetof(Part, params) + 1 * sizeof(float)), 0, false,
          nullptr, 0,
          CombinationClass::kAdditive, 4, false },
    [static_cast<std::size_t>(ParamId::kAttack)] =
        { "attack", "s", 0.0f, 10.0f, 0.25f, ParamCurve::kExponential,
          static_cast<std::uint16_t>(offsetof(Part, params) + 2 * sizeof(float)), 0, false,
          nullptr, 0,
          CombinationClass::kExponential, 4, false },
    [static_cast<std::size_t>(ParamId::kDecay)] =
        { "decay", "s", 0.0f, 10.0f, 0.6f, ParamCurve::kExponential,
          static_cast<std::uint16_t>(offsetof(Part, params) + 3 * sizeof(float)), 0, false,
          nullptr, 0,
          CombinationClass::kExponential, 4, false },
    [static_cast<std::size_t>(ParamId::kSustain)] =
        { "sustain", "%", 0.0f, 100.0f, 0.7f, ParamCurve::kLinear,
          static_cast<std::uint16_t>(offsetof(Part, params) + 4 * sizeof(float)), 0, false,
          nullptr, 0,
          CombinationClass::kMultiplicative, 4, false },
    [static_cast<std::size_t>(ParamId::kRelease)] =
        { "release", "s", 0.0f, 10.0f, 0.6f, ParamCurve::kExponential,
          static_cast<std::uint16_t>(offsetof(Part, params) + 5 * sizeof(float)), 0, false,
          nullptr, 0,
          CombinationClass::kExponential, 4, false },
    [static_cast<std::size_t>(ParamId::kAmp)] =
        { "amp", "%", 0.0f, 100.0f, 1.0f, ParamCurve::kLinear,
          static_cast<std::uint16_t>(offsetof(Part, params) + 6 * sizeof(float)), 0, true,
          nullptr, 0,
          CombinationClass::kMultiplicative, 4, false },
    [static_cast<std::size_t>(ParamId::kPitchCoarse)] =
        { "pitch_coarse", "semi", -24.0f, 24.0f, 0.5f, ParamCurve::kLinear,
          static_cast<std::uint16_t>(offsetof(Part, params) + 7 * sizeof(float)), 0, true,
          nullptr, 0,
          CombinationClass::kExponential, 4, true },
    [static_cast<std::size_t>(ParamId::kPitchBend)] =
        { "pitchbend", "%", 0.0f, 100.0f, 0.5f, ParamCurve::kLinear,
          static_cast<std::uint16_t>(offsetof(Part, params) + 8 * sizeof(float)), 0, false,
          nullptr, 0,
          CombinationClass::kAdditive, 4, true },
    [static_cast<std::size_t>(ParamId::kDrive)] =
        { "drive", "dB", 0.0f, 20.0f, 0.0f, ParamCurve::kExponential,
          static_cast<std::uint16_t>(offsetof(Part, params) + 9 * sizeof(float)), 0, true,
          nullptr, 0,
          CombinationClass::kAdditive, 4, false },
    [static_cast<std::size_t>(ParamId::kKeyFollowDepth)] =
        { "key_follow", "%", 0.0f, 100.0f, 0.5f, ParamCurve::kLinear,
          static_cast<std::uint16_t>(offsetof(Part, key_follow_depth)), 0, false,
          nullptr, 0,
          CombinationClass::kAdditive, 4, false },
};

int ParamCount() { return static_cast<int>(ParamId::kCount); }

const char *ParamName(ParamId id) {
    return k_params[static_cast<std::size_t>(id)].name;
}

const char *ParamUnit(ParamId id) {
    return k_params[static_cast<std::size_t>(id)].unit;
}

// Exponential curve from zero (disp_min == 0): 0 at norm 0, disp_max at
// norm 1, with a 10000x ratio so short times get fine resolution.
constexpr float kExpRefRatio = 10000.0f;

float ParamNormToDisp(const ParamDesc *p, float norm) {
    if (p->curve == ParamCurve::kExponential) {
        if (p->disp_min <= 0.0f)
            return p->disp_max * (std::pow(kExpRefRatio, norm) - 1.0f) /
                   (kExpRefRatio - 1.0f);
        return p->disp_min * std::pow(p->disp_max / p->disp_min, norm);
    }
    return p->disp_min + (p->disp_max - p->disp_min) * norm;
}

float ParamDispToNorm(const ParamDesc *p, float disp) {
    if (p->curve == ParamCurve::kExponential) {
        if (p->disp_min <= 0.0f)
            return std::log(disp / p->disp_max * (kExpRefRatio - 1.0f) + 1.0f) /
                   std::log(kExpRefRatio);
        return std::log(disp / p->disp_min) / std::log(p->disp_max / p->disp_min);
    }
    return (disp - p->disp_min) / (p->disp_max - p->disp_min);
}

float ParamGet(const Part *p, ParamRef ref) {
    const ParamDesc &desc = k_params[static_cast<std::size_t>(ref.id)];
    const auto *bytes = reinterpret_cast<const std::byte *>(p);
    return *reinterpret_cast<const float *>(
        bytes + desc.base + ref.instance * desc.stride);
}

void ParamSet(Part *p, ParamRef ref, float norm) {
    // `!(norm >= 0)` is true for negative AND NaN; both map to 0 so a NaN
    // parameter can never poison the voice (e.g. filter state).
    if (!(norm >= 0.0f)) norm = 0.0f;
    else if (norm > 1.0f) norm = 1.0f;
    const ParamDesc &desc = k_params[static_cast<std::size_t>(ref.id)];
    auto *bytes = reinterpret_cast<std::byte *>(p);
    *reinterpret_cast<float *>(bytes + desc.base + ref.instance * desc.stride) =
        norm;
}

float ParamGetDisp(const Part *p, ParamRef ref) {
    return ParamNormToDisp(&k_params[static_cast<std::size_t>(ref.id)],
                           ParamGet(p, ref));
}

void ParamSetDisp(Part *p, ParamRef ref, float disp) {
    ParamSet(p, ref,
             ParamDispToNorm(&k_params[static_cast<std::size_t>(ref.id)], disp));
}

int ParamFormatValue(const ParamDesc *desc, float norm, char *buf,
                     std::size_t n) {
    if (desc->n_labels > 0) {
        // Equal-width buckets: floor(norm * n) gives each label an equal slice
        // of [0, 1]; the round(norm*(n-1)) form gives the end labels half width.
        int idx = static_cast<int>(norm * desc->n_labels);
        if (idx < 0) idx = 0;
        else if (idx >= desc->n_labels) idx = desc->n_labels - 1;
        return std::snprintf(buf, n, "%s", desc->labels[idx]);
    }
    const float disp = ParamNormToDisp(desc, norm);
    const float a = disp < 0.0f ? -disp : disp;
    // No scientific notation: magnitudes >= 1000 get a "k" suffix (2.40k), and
    // tiny magnitudes keep enough decimals to stay readable rather than "e-05".
    if (a >= 1000.0f)
        return std::snprintf(buf, n, "%.3gk %s", disp * 0.001f, desc->unit);
    if (a != 0.0f && a < 0.0001f)
        return std::snprintf(buf, n, "%.6f %s", disp, desc->unit);
    return std::snprintf(buf, n, "%.3g %s", disp, desc->unit);
}

int ParamFormat(const Part *p, ParamRef ref, char *buf, std::size_t n) {
    const ParamDesc &desc = k_params[static_cast<std::size_t>(ref.id)];
    return ParamFormatValue(&desc, ParamGet(p, ref), buf, n);
}

}  // namespace engine
