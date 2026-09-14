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
    const char *name;       ///< Parameter name.
    const char *unit;       ///< Display unit, e.g. "Hz", "%", "s".
    float disp_min;         ///< Display value at normalized 0.
    float disp_max;         ///< Display value at normalized 1.
    float def;              ///< Default normalized value.
    ParamCurve curve;       ///< Display mapping curve.
    std::uint16_t base;     ///< Byte offset of instance 0 within the Part.
    std::uint16_t stride;   ///< Byte distance between instances; 0 = single-instance.
    bool modulatable;       ///< Whether a route may target this parameter.
    const char *const *labels;  ///< Discrete value labels; nullptr = continuous.
    std::uint8_t n_labels;      ///< Number of labels; 0 = continuous.
    CombinationClass comb;  ///< How routes combine; meaningful only when modulatable.
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
/// @param ref Parameter address ({instance, id}).
/// @return Value in [0, 1].
float ParamGet(const Part *p, ParamRef ref);

/// @brief Write a parameter's normalized value (clamped to [0, 1]).
/// @param p Part to write to.
/// @param ref Parameter address ({instance, id}).
/// @param norm Value in [0, 1].
void ParamSet(Part *p, ParamRef ref, float norm);

/// @brief Read a parameter in display units.
/// @param p Part to read from.
/// @param ref Parameter address ({instance, id}).
/// @return Display value.
float ParamGetDisp(const Part *p, ParamRef ref);

/// @brief Write a parameter from display units.
/// @param p Part to write to.
/// @param ref Parameter address ({instance, id}).
/// @param disp Display value.
void ParamSetDisp(Part *p, ParamRef ref, float disp);

/// @brief Format a parameter's display value into `buf`.
/// @param p Part to read from.
/// @param ref Parameter address ({instance, id}).
/// @param buf Destination buffer.
/// @param n Buffer size in bytes.
/// @return Characters written (excluding NUL), as snprintf.
int ParamFormat(const Part *p, ParamRef ref, char *buf, std::size_t n);

/// @brief Format a normalized value into `buf` against a descriptor.
///
/// Discrete parameters (n_labels > 0) render their nearest label ("SAW");
/// continuous parameters render "value unit" ("440 Hz").
/// @param desc Parameter descriptor.
/// @param norm Normalized value in [0, 1].
/// @param buf Destination buffer.
/// @param n Buffer size in bytes.
/// @return Characters written (excluding NUL), as snprintf.
int ParamFormatValue(const ParamDesc *desc, float norm, char *buf,
                     std::size_t n);

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
