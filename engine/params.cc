#include "params.h"

#include <cmath>
#include <cstdio>

constexpr ParamDesc g_params[PARAM_COUNT] = {
    [PARAM_CUTOFF] =
        { "cutoff", "Hz", 20.0f, 20000.0f, 1.0f, PARAM_CURVE_EXP,
          offsetof(Voice, cutoff) },
    [PARAM_RESONANCE] =
        { "resonance", "%", 0.0f, 100.0f, 0.0f, PARAM_CURVE_LIN,
          offsetof(Voice, resonance) },
    [PARAM_FILTER_ENV_AMOUNT] =
        { "filter_env", "%", 0.0f, 100.0f, 0.0f, PARAM_CURVE_LIN,
          offsetof(Voice, filter_env_amount) },
    [PARAM_ATTACK] =
        { "attack", "s", 0.001f, 10.0f, 0.25f, PARAM_CURVE_EXP,
          offsetof(Voice, attack) },
    [PARAM_DECAY] =
        { "decay", "s", 0.001f, 10.0f, 0.6f, PARAM_CURVE_EXP,
          offsetof(Voice, decay) },
    [PARAM_SUSTAIN] =
        { "sustain", "%", 0.0f, 100.0f, 0.7f, PARAM_CURVE_LIN,
          offsetof(Voice, sustain) },
    [PARAM_RELEASE] =
        { "release", "s", 0.001f, 10.0f, 0.6f, PARAM_CURVE_EXP,
          offsetof(Voice, release) },
};

int param_count() { return PARAM_COUNT; }
const char *param_name(ParamId id) { return g_params[id].name; }
const char *param_unit(ParamId id) { return g_params[id].unit; }

float param_norm_to_disp(const ParamDesc *p, float norm) {
    if (p->curve == PARAM_CURVE_EXP)
        return p->disp_min * std::pow(p->disp_max / p->disp_min, norm);
    return p->disp_min + (p->disp_max - p->disp_min) * norm;
}

float param_disp_to_norm(const ParamDesc *p, float disp) {
    if (p->curve == PARAM_CURVE_EXP)
        return std::log(disp / p->disp_min) / std::log(p->disp_max / p->disp_min);
    return (disp - p->disp_min) / (p->disp_max - p->disp_min);
}

float param_get(const Voice *v, ParamId id) {
    return *(const float *)((const char *)v + g_params[id].offset);
}

void param_set(Voice *v, ParamId id, float norm) {
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    *(float *)((char *)v + g_params[id].offset) = norm;
}

float param_get_disp(const Voice *v, ParamId id) {
    return param_norm_to_disp(&g_params[id], param_get(v, id));
}

void param_set_disp(Voice *v, ParamId id, float disp) {
    param_set(v, id, param_disp_to_norm(&g_params[id], disp));
}

int param_format(const Voice *v, ParamId id, char *buf, std::size_t n) {
    return std::snprintf(buf, n, "%.3g %s", param_get_disp(v, id),
                         param_unit(id));
}
