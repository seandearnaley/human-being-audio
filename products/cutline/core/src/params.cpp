// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/params.h"

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
    if (spec.kind == ParamKind::toggle)
        return real >= 0.5f ? 1.0f : 0.0f;
    if (spec.kind == ParamKind::choice)
        return std::clamp(std::round(real), spec.min, spec.max);
    if (spec.unit == Unit::integer)
        return std::clamp(std::round(real), spec.min, spec.max);
    return std::clamp(real, spec.min, spec.max);
}

float toNormalised(const ParamSpec& spec, float real) {
    const float clamped = clampToRange(spec, real);
    if (spec.max <= spec.min)
        return 0.0f;
    if (spec.kind != ParamKind::continuous)
        return (clamped - spec.min) / (spec.max - spec.min);
    if (spec.skew == 0.0f)
        return std::log(clamped / spec.min) / std::log(spec.max / spec.min);
    const float linear = (clamped - spec.min) / (spec.max - spec.min);
    return spec.skew == 1.0f ? linear : std::pow(linear, 1.0f / spec.skew);
}

float fromNormalised(const ParamSpec& spec, float normalised) {
    const float n = std::clamp(normalised, 0.0f, 1.0f);
    if (spec.kind == ParamKind::toggle)
        return n >= 0.5f ? 1.0f : 0.0f;
    if (spec.kind == ParamKind::choice)
        return std::clamp(std::round(spec.min + (spec.max - spec.min) * n), spec.min, spec.max);
    float raw = 0.0f;
    if (spec.skew == 0.0f)
        raw = spec.min * std::pow(spec.max / spec.min, n);
    else if (spec.skew == 1.0f)
        raw = spec.min + (spec.max - spec.min) * n;
    else
        raw = spec.min + (spec.max - spec.min) * std::pow(n, spec.skew);
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
        std::snprintf(buf, sizeof(buf), "%+.1f ms", static_cast<double>(v));
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
