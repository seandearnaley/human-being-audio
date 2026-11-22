// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic tempo-locked modulation lane math shared across studio engines.
#pragma once

#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hba::dsp {

inline constexpr int kLaneStepsPerCycle = 8;

enum class LaneWaveform : std::uint8_t {
    sine = 0,
    triangle = 1,
    rampUp = 2,
    rampDown = 3,
    square = 4,
    steps = 5,
    custom = 6,
};

// Integer hash (Chris Wellons' lowbias32): fixed, so deterministic patterns never drift.
constexpr std::uint32_t laneHash(std::uint32_t x) noexcept HBA_NONBLOCKING {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// The value of step `step` (0..kLaneStepsPerCycle-1) of `lane`, in -1..1.
constexpr double laneStepValue(int lane, int step) noexcept HBA_NONBLOCKING {
    const std::uint32_t h =
        laneHash(static_cast<std::uint32_t>(lane) * 0x9e3779b9U + static_cast<std::uint32_t>(step) + 1U);
    return static_cast<double>(h) / 2147483647.5 - 1.0;
}

// Lane phase in [0, 1) for a host position in quarter notes.
inline double lanePhase(double ppq, double lengthInQuarters, double phaseDegrees) noexcept HBA_NONBLOCKING {
    const double position = ppq / lengthInQuarters + phaseDegrees / 360.0;
    return position - std::floor(position);
}

// Built-in waveform value in [-1, 1] at `phase` in [0, 1).
inline double laneWaveform(LaneWaveform shape, double phase, int lane) noexcept HBA_NONBLOCKING {
    switch (shape) {
    case LaneWaveform::sine:
        return std::sin(6.283185307179586 * phase);
    case LaneWaveform::triangle:
        if (phase < 0.25)
            return 4.0 * phase;
        if (phase < 0.75)
            return 2.0 - 4.0 * phase;
        return 4.0 * phase - 4.0;
    case LaneWaveform::rampUp:
        return 2.0 * phase - 1.0;
    case LaneWaveform::rampDown:
        return 1.0 - 2.0 * phase;
    case LaneWaveform::square:
        return phase < 0.5 ? 1.0 : -1.0;
    case LaneWaveform::steps: {
        const int step = std::clamp(static_cast<int>(phase * kLaneStepsPerCycle), 0, kLaneStepsPerCycle - 1);
        return laneStepValue(lane, step);
    }
    case LaneWaveform::custom:
        break;
    }
    return 0.0;
}

} // namespace hba::dsp
