// Movement lane shapes. The engine plays them and the interface draws them, so
// both read this one definition.
#pragma once

#include "tf/params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tf {

// `Steps` holds this many values per lane cycle.
inline constexpr int kLaneStepsPerCycle = 8;

// Integer hash (Chris Wellons' lowbias32): fixed, so a pattern never changes.
constexpr std::uint32_t laneHash(std::uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// The value of step `step` (0..kLaneStepsPerCycle-1) of `lane`, in -1..1. Each
// lane has its own pattern, and it repeats identically every cycle.
constexpr double laneStepValue(int lane, int step) noexcept {
    const std::uint32_t h = laneHash(static_cast<std::uint32_t>(lane) * 0x9e3779b9U + static_cast<std::uint32_t>(step) + 1U);
    return static_cast<double>(h) / 2147483647.5 - 1.0;
}

// Lane phase in 0..1 for a host position in quarter notes.
inline double lanePhase(double ppq, double lengthInQuarters, double phaseDegrees) noexcept {
    const double position = ppq / lengthInQuarters + phaseDegrees / 360.0;
    return position - std::floor(position);
}

// Shape value in -1..1 at `phase` (0..1). Sine and triangle start at 0 going
// up; the square is high for the first half.
inline double laneShape(LaneShape shape, double phase, int lane) noexcept {
    switch (shape) {
        case LaneShape::sine: return std::sin(6.283185307179586 * phase);
        case LaneShape::triangle:
            if (phase < 0.25) return 4.0 * phase;
            if (phase < 0.75) return 2.0 - 4.0 * phase;
            return 4.0 * phase - 4.0;
        case LaneShape::rampUp: return 2.0 * phase - 1.0;
        case LaneShape::rampDown: return 1.0 - 2.0 * phase;
        case LaneShape::square: return phase < 0.5 ? 1.0 : -1.0;
        case LaneShape::steps: {
            const int step = std::clamp(static_cast<int>(phase * kLaneStepsPerCycle), 0, kLaneStepsPerCycle - 1);
            return laneStepValue(lane, step);
        }
    }
    return 0.0;
}

}
