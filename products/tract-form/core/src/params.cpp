#include "tf/params.h"

#include <hba/core/param_math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf {

std::optional<ParamId> findParam(std::string_view key) {
    for (std::size_t i = 0; i < kParams.size(); ++i)
        if (kParams[i].key == key) return static_cast<ParamId>(i);
    return std::nullopt;
}

float clampToRange(const ParamSpec& spec, float real) {
    return hba::core::clampParam(real, spec.min, spec.max, spec.def, spec.kind == ParamKind::continuous);
}

float toNormalised(const ParamSpec& spec, float real) {
    return hba::core::toNormalisedLogSkew(real, spec.min, spec.max, spec.def, spec.skew,
                                          spec.kind == ParamKind::continuous);
}

float fromNormalised(const ParamSpec& spec, float normalised) {
    return hba::core::fromNormalisedLogSkew(normalised, spec.min, spec.max, spec.def, spec.skew,
                                            spec.kind == ParamKind::continuous);
}

namespace {
std::string print(const char* format, double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, format, value);
    return buffer;
}
}

std::string formatValue(const ParamSpec& spec, float real) {
    if (spec.kind != ParamKind::continuous) {
        const auto index = static_cast<std::size_t>(clampToRange(spec, real));
        return std::string(spec.choices[index]);
    }
    const bool isSigned = spec.min < 0.0f;
    switch (spec.unit) {
        case Unit::percent: return print(isSigned ? "%+.0f %%" : "%.0f %%", real * 100.0);
        case Unit::ms:      return print(real < 100.0f ? "%.1f ms" : "%.0f ms", real);
        case Unit::cents:   return print("%+.1f ct", real);
        case Unit::degrees: return print("%.0f°", real);
        case Unit::dB:      return print(isSigned ? "%+.1f dB" : "%.1f dB", real);
        case Unit::hz:      return print("%.0f Hz", real);
        case Unit::ratio:
            return print("%.3f", real);
        case Unit::harmonic:
            return print(real < 10.0f ? "\u00d7%.1f" : "\u00d7%.0f", real);
        case Unit::semitones:
            return print(isSigned ? "%+.1f st" : "%.1f st", real);
        case Unit::none:
            break;
        }
    return print("%.2f", real);
}

}
