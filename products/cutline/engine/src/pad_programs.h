// SPDX-License-Identifier: AGPL-3.0-or-later
// X/Y mappings of the four interim pad programs. The PLAY spec replaces these.
#pragma once

#include "cl/nonblocking.h"

#include <algorithm>
#include <cmath>

namespace cl::pads {

inline constexpr double kPadQuantizeBeats = 0.25; // pads engage on the next 1/16, whatever the scene quantize is

// Repeat X: a window of 1, 1/2, 1/4 or 1/8 beat.
constexpr double repeatWindowBeats(float x) noexcept CL_NONBLOCKING {
    return x < 0.25f ? 1.0 : (x < 0.5f ? 0.5 : (x < 0.75f ? 0.25 : 0.125));
}

// Repeat Y: gate length, from 20% to 105% of each note's own length.
constexpr double repeatGate(float y) noexcept CL_NONBLOCKING {
    return 0.20 + 0.85 * static_cast<double>(std::clamp(y, 0.0f, 1.0f));
}

// Roll X: 1/8, 1/16, 1/16 triplet or 1/32, in beats.
constexpr double rollStepBeats(float x) noexcept CL_NONBLOCKING {
    return x < 0.25f ? 0.5 : (x < 0.5f ? 0.25 : (x < 0.75f ? 1.0 / 6.0 : 0.125));
}

// Roll Y: velocity 1..127.
inline int rollVelocity(float y) noexcept CL_NONBLOCKING {
    return std::clamp(static_cast<int>(std::lround(1.0f + 126.0f * std::clamp(y, 0.0f, 1.0f))), 1, 127);
}

// Rate X: 0.5x, 1x or 2x, on top of the Rate parameter.
constexpr double rateMultiplier(float x) noexcept CL_NONBLOCKING {
    return x < 1.0f / 3.0f ? 0.5 : (x > 2.0f / 3.0f ? 2.0 : 1.0);
}

// Rate Y: below 30% kick and snare only; above 70% no kick.
constexpr bool rateKeepsLane(float y, int lane) noexcept CL_NONBLOCKING {
    if (y < 0.30f)
        return lane <= 1;
    if (y > 0.70f)
        return lane != 0;
    return true;
}

// Drop X: the left half is silence, the right half plays the fill (the same as fillNow).
constexpr bool dropIsSilence(float x) noexcept CL_NONBLOCKING { return x < 0.5f; }

} // namespace cl::pads
