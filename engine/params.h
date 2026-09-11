/// @file params.h
/// @brief Parameter descriptor table and accessors.
///
/// The parameter model: named parameters in one flat table, stored
/// normalized (0..1) in the Part. The UI, MIDI CC mapping, and patch
/// save/load all walk this table instead of knowing individual parameters.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"

namespace engine {

/// How a normalized value maps to its display value.
enum class ParamCurve : std::uint8_t {
    kLinear = 0,   ///< Linear mapping.
    kExponential,  ///< Exponential mapping (frequency, time).
};

/// Static description of one parameter.
struct ParamDesc {
    const char *name;    ///< Parameter name.
    const char *unit;    ///< Display unit, e.g. "Hz", "%", "s".
    float disp_min;      ///< Display value at normalized 0.
    float disp_max;      ///< Display value at normalized 1.
    float def;           ///< Default normalized value.
    ParamCurve curve;    ///< Display mapping curve.
    std::size_t offset;  ///< offsetof(Part, field) — the target slot.
};

/// The single source of truth for the parameter surface.
extern const ParamDesc g_params[static_cast<std::size_t>(ParamId::kCount)];

/// @brief Number of parameters.
/// @return The parameter count.
int ParamCount();

/// @brief Name of a parameter.
/// @param id Parameter identifier.
/// @return Parameter name.
const char *ParamName(ParamId id);

/// @brief Display unit of a parameter.
/// @param id Parameter identifier.
/// @return Display unit string.
const char *ParamUnit(ParamId id);

/// @brief Read a parameter's normalized value.
/// @param p Part to read from.
/// @param id Parameter identifier.
/// @return Value in [0, 1].
float ParamGet(const Part *p, ParamId id);

/// @brief Write a parameter's normalized value (clamped to [0, 1]).
/// @param p Part to write to.
/// @param id Parameter identifier.
/// @param norm Value in [0, 1].
void ParamSet(Part *p, ParamId id, float norm);

/// @brief Read a parameter in display units.
/// @param p Part to read from.
/// @param id Parameter identifier.
/// @return Display value.
float ParamGetDisp(const Part *p, ParamId id);

/// @brief Write a parameter from display units.
/// @param p Part to write to.
/// @param id Parameter identifier.
/// @param disp Display value.
void ParamSetDisp(Part *p, ParamId id, float disp);

/// @brief Format a parameter's display value into `buf`.
/// @param p Part to read from.
/// @param id Parameter identifier.
/// @param buf Destination buffer.
/// @param n Buffer size in bytes.
/// @return Characters written (excluding NUL), as snprintf.
int ParamFormat(const Part *p, ParamId id, char *buf, std::size_t n);

/// @brief Map a normalized value to display units.
/// @param p Parameter descriptor.
/// @param norm Normalized value in [0, 1].
/// @return Display value.
float ParamNormToDisp(const ParamDesc *p, float norm);

/// @brief Map a display value to normalized units.
/// @param p Parameter descriptor.
/// @param disp Display value.
/// @return Normalized value in [0, 1].
float ParamDispToNorm(const ParamDesc *p, float disp);

}  // namespace engine
