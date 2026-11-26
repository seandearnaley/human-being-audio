// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/params.h"

#include <hba/core/param_math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cl {

std::optional<ParamId> findParam(std::string_view key) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (kParams[i].key == key)
            return static_cast<ParamId>(i);
    }
    return std::nullopt;
}

float clampToRange(const ParamSpec& spec, float real) {
    if (spec.kind == ParamKind::toggle && std::isfinite(real))
        return real >= 0.5f ? 1.0f : 0.0f;
    const bool isCont = spec.kind == ParamKind::continuous && spec.unit != Unit::integer;
    return hba::core::clampParam(real, spec.min, spec.max, spec.def, isCont);
}

float toNormalised(const ParamSpec& spec, float real) {
    const float clamped = clampToRange(spec, real);
    return hba::core::toNormalisedLogSkew(clamped, spec.min, spec.max, spec.def, spec.skew,
                                          spec.kind == ParamKind::continuous);
}

float fromNormalised(const ParamSpec& spec, float normalised) {
    const float n = std::clamp(std::isfinite(normalised) ? normalised : 0.0f, 0.0f, 1.0f);
    if (spec.kind == ParamKind::toggle)
        return n >= 0.5f ? 1.0f : 0.0f;
    const bool isCont = spec.kind == ParamKind::continuous;
    const float raw = hba::core::fromNormalisedLogSkew(n, spec.min, spec.max, spec.def, spec.skew, isCont);
    return spec.unit == Unit::integer ? std::clamp(std::round(raw), spec.min, spec.max) : raw;
}

std::string formatValue(const ParamSpec& spec, float real) {
    const float v = clampToRange(spec, real);
    if (spec.kind != ParamKind::continuous && !spec.choices.empty()) {
        const auto idx = static_cast<std::size_t>(
            std::clamp(static_cast<int>(v + 0.5f), 0, static_cast<int>(spec.choices.size()) - 1));
        return std::string(spec.choices[idx]);
    }
    char buf[32]{};
    switch (spec.unit) {
    case Unit::percent:
        if (spec.min < 0.0f)
            std::snprintf(buf, sizeof(buf), "%+.0f%%", static_cast<double>(v * 100.0f));
        else
            std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(v * 100.0f));
        return buf;
    case Unit::ms:
        if (spec.min < 0.0f)
            std::snprintf(buf, sizeof(buf), "%+.1f ms", static_cast<double>(v));
        else
            std::snprintf(buf, sizeof(buf), "%.0f ms", static_cast<double>(v));
        return buf;
    case Unit::degrees:
        std::snprintf(buf, sizeof(buf), "%.0f\u00b0", static_cast<double>(v));
        return buf;
    case Unit::dB:
        std::snprintf(buf, sizeof(buf), "%.1f dB", static_cast<double>(v));
        return buf;
    case Unit::integer:
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
        return buf;
    case Unit::none:
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
        return buf;
    }
    return {};
}

} // namespace cl
