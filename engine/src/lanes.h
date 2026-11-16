// Movement lanes: a phase locked to the host position and a shape.
// Lane phase is frac(ppq / lengthInQuarters + phase / 360).
#pragma once

#include "control.h"
#include "tf/params.h"

#include <algorithm>
#include <cstdint>

namespace tf::dsp {

// `Steps` holds this many values per lane cycle.
inline constexpr int kStepsPerCycle = 8;

// Integer hash (Chris Wellons' lowbias32): fixed, so a pattern never changes.
inline constexpr std::uint32_t hash32(std::uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// The value of step `step` (0..kStepsPerCycle-1) of `lane`, in -1..1.
inline constexpr double stepValue(int lane, int step) noexcept {
    const std::uint32_t h = hash32(static_cast<std::uint32_t>(lane) * 0x9e3779b9U + static_cast<std::uint32_t>(step) + 1U);
    return static_cast<double>(h) / 2147483647.5 - 1.0;
}

inline double lanePhase(double ppq, int rateIndex, double phaseDegrees) noexcept TF_NONBLOCKING {
    const auto rate = static_cast<std::size_t>(std::clamp(rateIndex, 0, static_cast<int>(kLaneRateQuarters.size()) - 1));
    return frac(ppq / kLaneRateQuarters[rate] + phaseDegrees / 360.0);
}

// Shape value in -1..1 at phase `theta` (0..1). Sine and triangle start at 0
// going up; the square is high for the first half.
inline double laneShape(LaneShape shape, double theta, int lane) noexcept TF_NONBLOCKING {
    switch (shape) {
        case LaneShape::sine: return std::sin(kTwoPi * theta);
        case LaneShape::triangle:
            if (theta < 0.25) return 4.0 * theta;
            if (theta < 0.75) return 2.0 - 4.0 * theta;
            return 4.0 * theta - 4.0;
        case LaneShape::rampUp: return 2.0 * theta - 1.0;
        case LaneShape::rampDown: return 1.0 - 2.0 * theta;
        case LaneShape::square: return theta < 0.5 ? 1.0 : -1.0;
        case LaneShape::steps: {
            const int step = std::clamp(static_cast<int>(theta * kStepsPerCycle), 0, kStepsPerCycle - 1);
            return stepValue(lane, step);
        }
    }
    return 0.0;
}

}
