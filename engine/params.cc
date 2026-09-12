#include "params.h"

#include <cmath>
#include <cstdio>

namespace engine {

constexpr ParamDesc g_params[static_cast<std::size_t>(ParamId::kCount)] = {
    [static_cast<std::size_t>(ParamId::kCutoff)] =
        { "cutoff", "Hz", 20.0f, 20000.0f, 1.0f, ParamCurve::kExponential,
          offsetof(Part, params) + 0 * sizeof(float),
          CombinationClass::kAdditive },
    [static_cast<std::size_t>(ParamId::kResonance)] =
        { "resonance", "%", 0.0f, 100.0f, 0.0f, ParamCurve::kLinear,
          offsetof(Part, params) + 1 * sizeof(float),
          CombinationClass::kAdditive },
    [static_cast<std::size_t>(ParamId::kAttack)] =
        { "attack", "s", 0.0f, 10.0f, 0.25f, ParamCurve::kExponential,
          offsetof(Part, params) + 2 * sizeof(float),
          CombinationClass::kExponential },
    [static_cast<std::size_t>(ParamId::kDecay)] =
        { "decay", "s", 0.0f, 10.0f, 0.6f, ParamCurve::kExponential,
          offsetof(Part, params) + 3 * sizeof(float),
          CombinationClass::kExponential },
    [static_cast<std::size_t>(ParamId::kSustain)] =
        { "sustain", "%", 0.0f, 100.0f, 0.7f, ParamCurve::kLinear,
          offsetof(Part, params) + 4 * sizeof(float),
          CombinationClass::kMultiplicative },
    [static_cast<std::size_t>(ParamId::kRelease)] =
        { "release", "s", 0.0f, 10.0f, 0.6f, ParamCurve::kExponential,
          offsetof(Part, params) + 5 * sizeof(float),
          CombinationClass::kExponential },
    [static_cast<std::size_t>(ParamId::kAmp)] =
        { "amp", "%", 0.0f, 100.0f, 0.25f, ParamCurve::kLinear,
          offsetof(Part, params) + 6 * sizeof(float),
          CombinationClass::kMultiplicative },
    [static_cast<std::size_t>(ParamId::kPitchCoarse)] =
        { "pitch_coarse", "semi", -24.0f, 24.0f, 0.5f, ParamCurve::kLinear,
          offsetof(Part, params) + 7 * sizeof(float),
          CombinationClass::kExponential },
    [static_cast<std::size_t>(ParamId::kPitchBend)] =
        { "pitchbend", "%", 0.0f, 100.0f, 0.5f, ParamCurve::kLinear,
          offsetof(Part, params) + 8 * sizeof(float),
          CombinationClass::kAdditive },
    [static_cast<std::size_t>(ParamId::kDrive)] =
        { "drive", "dB", 0.0f, 20.0f, 0.0f, ParamCurve::kExponential,
          offsetof(Part, params) + 9 * sizeof(float),
          CombinationClass::kAdditive },
    [static_cast<std::size_t>(ParamId::kKeyFollowDepth)] =
        { "key_follow", "%", 0.0f, 100.0f, 0.5f, ParamCurve::kLinear,
          offsetof(Part, key_follow_depth), CombinationClass::kAdditive },
};

int ParamCount() { return static_cast<int>(ParamId::kCount); }

const char *ParamName(ParamId id) {
    return g_params[static_cast<std::size_t>(id)].name;
}

const char *ParamUnit(ParamId id) {
    return g_params[static_cast<std::size_t>(id)].unit;
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

float ParamGet(const Part *p, ParamId id) {
    const ParamDesc &desc = g_params[static_cast<std::size_t>(id)];
    const auto *base = reinterpret_cast<const std::byte *>(p);
    return *reinterpret_cast<const float *>(base + desc.offset);
}

void ParamSet(Part *p, ParamId id, float norm) {
    // `!(norm >= 0)` is true for negative AND NaN; both map to 0 so a NaN
    // parameter can never poison the voice (e.g. filter state).
    if (!(norm >= 0.0f)) norm = 0.0f;
    else if (norm > 1.0f) norm = 1.0f;
    const ParamDesc &desc = g_params[static_cast<std::size_t>(id)];
    auto *base = reinterpret_cast<std::byte *>(p);
    *reinterpret_cast<float *>(base + desc.offset) = norm;
}

float ParamGetDisp(const Part *p, ParamId id) {
    return ParamNormToDisp(&g_params[static_cast<std::size_t>(id)],
                           ParamGet(p, id));
}

void ParamSetDisp(Part *p, ParamId id, float disp) {
    ParamSet(p, id,
             ParamDispToNorm(&g_params[static_cast<std::size_t>(id)], disp));
}

int ParamFormat(const Part *p, ParamId id, char *buf, std::size_t n) {
    return std::snprintf(buf, n, "%.3g %s", ParamGetDisp(p, id),
                         ParamUnit(id));
}

}  // namespace engine
