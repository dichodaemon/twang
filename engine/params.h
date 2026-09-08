#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"

// Parameter model: named parameters in one flat table, stored normalized
// (0..1) in the Voice. UI, MIDI CC mapping, and patch save/load all walk this
// table instead of knowing about individual parameters.

namespace engine {

enum class ParamId : std::uint8_t {
    kCutoff = 0,
    kResonance,
    kFilterEnvAmount,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kCount,
};

enum class ParamCurve : std::uint8_t {
    kLinear = 0,
    kExponential,
};

struct ParamDesc {
    const char *name;
    const char *unit;
    float disp_min;   // display value at normalized 0
    float disp_max;   // display value at normalized 1
    float def;        // default normalized
    ParamCurve curve;
    std::size_t offset;  // offsetof(Voice, field)
};

// The single source of truth for the parameter surface.
extern const ParamDesc g_params[static_cast<std::size_t>(ParamId::kCount)];

int param_count();
const char *param_name(ParamId id);
const char *param_unit(ParamId id);

float param_get(const Voice *v, ParamId id);             // normalized 0..1
void  param_set(Voice *v, ParamId id, float norm);       // normalized 0..1
float param_get_disp(const Voice *v, ParamId id);        // display units
void  param_set_disp(Voice *v, ParamId id, float disp);  // display units
int   param_format(const Voice *v, ParamId id, char *buf, std::size_t n);

// curve mappings (shared by the engine and UI)
float param_norm_to_disp(const ParamDesc *p, float norm);
float param_disp_to_norm(const ParamDesc *p, float disp);

}  // namespace engine
