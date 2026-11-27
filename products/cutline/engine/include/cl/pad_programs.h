// SPDX-License-Identifier: AGPL-3.0-or-later
// X/Y mappings of the six PLAY programs: Repeat, Roll, Density, Rate, Drop, Fill. The engine plays from these and
// the PLAY surface draws its zones from them, so the two cannot disagree.
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

// Roll X: 1/8, 1/16, 1/16 triplet or 1/32, in pattern beats.
constexpr double rollStepBeats(float x) noexcept CL_NONBLOCKING {
    return x < 0.25f ? 0.5 : (x < 0.5f ? 0.25 : (x < 0.75f ? 1.0 / 6.0 : 0.125));
}

// Roll Y: the lane it rolls. Bottom third kick, middle third snare, top third closed hat.
constexpr int rollLane(float y) noexcept CL_NONBLOCKING { return y < 1.0f / 3.0f ? 0 : (y < 2.0f / 3.0f ? 1 : 4); }

inline constexpr int kRollVelocityFrom = 72;
inline constexpr int kRollVelocityTo = 120;

// Roll velocity rises across each bar: 72 on the bar's first roll note, 120 on its last. `beatInBar` is the note's
// place in its bar on the pattern's clock; `stepBeats` the roll's grid, so the last note of a bar is 4 - stepBeats.
inline int rollVelocity(double beatInBar, double stepBeats) noexcept CL_NONBLOCKING {
    const double span = std::max(1.0e-9, 4.0 - stepBeats);
    const double t = std::clamp(beatInBar / span, 0.0, 1.0);
    return static_cast<int>(std::lround(static_cast<double>(kRollVelocityFrom) +
                                        static_cast<double>(kRollVelocityTo - kRollVelocityFrom) * t));
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

// Drop X: four zones, left to right.
enum class DropZone { silence, kickOnly, kickAndSnare, noKick };

constexpr DropZone dropZone(float x) noexcept CL_NONBLOCKING {
    return x < 0.25f ? DropZone::silence
                     : (x < 0.5f ? DropZone::kickOnly : (x < 0.75f ? DropZone::kickAndSnare : DropZone::noKick));
}

constexpr bool dropIsSilence(float x) noexcept CL_NONBLOCKING { return dropZone(x) == DropZone::silence; }

// The lanes a Drop zone lets through: none; kick; kick and snare; everything except kick.
constexpr bool dropKeepsLane(float x, int lane) noexcept CL_NONBLOCKING {
    switch (dropZone(x)) {
    case DropZone::silence:
        return false;
    case DropZone::kickOnly:
        return lane == 0;
    case DropZone::kickAndSnare:
        return lane <= 1;
    case DropZone::noKick:
        return lane != 0;
    }
    return true;
}

} // namespace cl::pads
