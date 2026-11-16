// Movement lane shapes. The engine plays them and the interface draws them, so
// both read this one definition.
#pragma once

#include "tf/params.h"

#include <cmath>
#include <cstdint>

namespace tf {

inline constexpr int kLaneStepsPerCycle = 8;

// A repeatable pseudo-random value in -1..1 for a step index.
constexpr float laneStepValue(std::uint32_t step) {
    std::uint32_t h = step * 0x9e3779b9u + 0x7f4a7c15u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffu) / 32767.5f - 1.0f;
}

// Lane output in -1..1 for a phase in 0..1. `cycle` counts completed cycles and
// only matters to Steps, whose pattern repeats every cycle.
inline float laneShape(LaneShape shape, float phase, std::uint32_t cycle = 0) {
    (void) cycle;
    switch (shape) {
        case LaneShape::sine:     return std::sin(6.2831853f * phase);
        case LaneShape::triangle: return phase < 0.25f ? 4.0f * phase : phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f;
        case LaneShape::rampUp:   return 2.0f * phase - 1.0f;
        case LaneShape::rampDown: return 1.0f - 2.0f * phase;
        case LaneShape::square:   return phase < 0.5f ? 1.0f : -1.0f;
        case LaneShape::steps:    return laneStepValue(static_cast<std::uint32_t>(phase * kLaneStepsPerCycle));
    }
    return 0.0f;
}

// Lane phase in 0..1 for a host position in quarter notes.
inline float lanePhase(double ppq, double lengthInQuarters, float phaseDegrees) {
    const double position = ppq / lengthInQuarters + static_cast<double>(phaseDegrees) / 360.0;
    return static_cast<float>(position - std::floor(position));
}

}
