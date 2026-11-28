// SPDX-License-Identifier: GPL-3.0-or-later
// Shared real-time-safe parameter clamping and normalisation math.
#pragma once

#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <cmath>

namespace hba::core {

inline float clampParam(float real, float low, float high, float def, bool isContinuous) noexcept HBA_NONBLOCKING {
    if (!std::isfinite(real))
        return def;
    if (!(low <= high))
        return std::isfinite(def) ? def : low;
    const float clamped = std::clamp(real, low, high);
    return isContinuous ? clamped : std::round(clamped);
}

// Log/power curve used by Tract Form, Pole Field, and Cutline:
// skew == 0.0f selects logarithmic mapping; otherwise real = low + span * norm^skew.
inline float toNormalisedLogSkew(float real, float low, float high, float def, float skew,
                                 bool isContinuous) noexcept HBA_NONBLOCKING {
    const float value = clampParam(real, low, high, def, isContinuous);
    const float span = high - low;
    if (span <= 0.0f)
        return 0.0f;
    if (isContinuous && skew == 0.0f && low > 0.0f && high > low)
        return std::clamp(std::log(value / low) / std::log(high / low), 0.0f, 1.0f);
    const float linear = std::clamp((value - low) / span, 0.0f, 1.0f);
    if (!isContinuous || !std::isfinite(skew) || skew <= 0.0f || skew == 1.0f)
        return linear;
    return std::pow(linear, 1.0f / skew);
}

inline float fromNormalisedLogSkew(float norm, float low, float high, float def, float skew,
                                   bool isContinuous) noexcept HBA_NONBLOCKING {
    const float n = std::clamp(std::isfinite(norm) ? norm : 0.0f, 0.0f, 1.0f);
    float real = low;
    if (isContinuous && skew == 0.0f && low > 0.0f && high > low)
        real = low * std::pow(high / low, n);
    else {
        const bool usePow = isContinuous && std::isfinite(skew) && skew > 0.0f && skew != 1.0f;
        real = low + (high - low) * (usePow ? std::pow(n, skew) : n);
    }
    return clampParam(real, low, high, def, isContinuous);
}

// JUCE NormalisableRange skew convention used by Torus Glide and Strike Body:
// norm = lin^skew, lin = norm^(1/skew).
inline float toNormalisedJuceSkew(float real, float low, float high, float def, float skew,
                                  bool isContinuous) noexcept HBA_NONBLOCKING {
    const float value = clampParam(real, low, high, def, isContinuous);
    const float span = high - low;
    if (span <= 0.0f)
        return 0.0f;
    const float linear = std::clamp((value - low) / span, 0.0f, 1.0f);
    if (!isContinuous || !std::isfinite(skew) || skew <= 0.0f || skew == 1.0f)
        return linear;
    return std::pow(linear, skew);
}

inline float fromNormalisedJuceSkew(float norm, float low, float high, float def, float skew,
                                    bool isContinuous) noexcept HBA_NONBLOCKING {
    const float n = std::clamp(std::isfinite(norm) ? norm : 0.0f, 0.0f, 1.0f);
    const bool usePow = isContinuous && std::isfinite(skew) && skew > 0.0f && skew != 1.0f;
    const float linear = usePow ? std::pow(n, 1.0f / skew) : n;
    return clampParam(low + linear * (high - low), low, high, def, isContinuous);
}

} // namespace hba::core
