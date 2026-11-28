// SPDX-License-Identifier: GPL-3.0-or-later
// Canonical modulation lane routing, clock domains (Sync, Retrig, 1-Shot, Free Hz,
// Free Retrig), and deterministic waveforms shared across studio engines.
#pragma once

#include <hba/dsp/curve.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hba::dsp {

inline constexpr int kLaneStepsPerCycle = 8;

inline constexpr std::array<std::string_view, 12> kLaneRateLabels{
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32",
};
inline constexpr std::array<double, 12> kLaneRateQuarters{
    32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125,
};
inline constexpr std::array<std::string_view, 5> kLaneFollowLabels{"Own", "Lane 1", "Lane 2", "Lane 3", "Lane 4"};
inline constexpr std::array<std::string_view, 7> kLaneRatioLabels{
    "\u00f74", "\u00f73", "\u00f72", "\u00d71", "\u00d72", "\u00d73", "\u00d74",
};
inline constexpr std::array<double, 7> kLaneRatios{0.25, 1.0 / 3.0, 0.5, 1.0, 2.0, 3.0, 4.0};
inline constexpr std::array<std::string_view, 5> kLaneModeLabels{"Sync", "Retrig", "1-Shot", "Free", "Free R"};

inline constexpr float kLaneMinHz = 0.05f;
inline constexpr float kLaneMaxHz = 20.0f;
inline constexpr float kLaneDefaultHz = 0.5f;

enum class LaneWaveform : std::uint8_t {
    sine = 0,
    triangle = 1,
    rampUp = 2,
    rampDown = 3,
    square = 4,
    steps = 5,
    custom = 6,
};

enum class LaneMode : std::uint8_t {
    sync = 0,
    retrig = 1,
    oneShot = 2,
    freeRun = 3,
    freeRetrig = 4,
};

struct LaneClock {
    double hostPpq = 0.0;
    double notePpq = 0.0;
    double freeSec = 0.0;
    double noteSec = 0.0;
};

struct LaneConfig {
    bool on = false;
    int rateIndex = 0;
    double phaseDeg = 0.0;
    double depth = 0.0;
    int shape = 0;
    int follow = 0;     // 0 = Own, 1..N = follow lane (follow - 1)
    int ratioIndex = 3; // index into kLaneRatios (3 = x1)
    LaneMode mode = LaneMode::sync;
    double hz = static_cast<double>(kLaneDefaultHz);
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
    if (!std::isfinite(ppq) || !std::isfinite(lengthInQuarters) || !(lengthInQuarters > 0.0) ||
        !std::isfinite(phaseDegrees))
        return 0.0;
    const double position = ppq / lengthInQuarters + phaseDegrees / 360.0;
    return position - std::floor(position);
}

// Free-running lane phase in [0, 1) for elapsed time in seconds at `hz` cycles/sec.
inline double lanePhaseHz(double seconds, double hz, double phaseDegrees) noexcept HBA_NONBLOCKING {
    if (!std::isfinite(seconds) || !std::isfinite(hz) || !std::isfinite(phaseDegrees))
        return 0.0;
    const double position = seconds * std::max(0.0, hz) + phaseDegrees / 360.0;
    return position - std::floor(position);
}

// Built-in waveform value in [-1, 1] at `phase` in [0, 1).
inline double laneWaveform(LaneWaveform shape, double phase, int lane) noexcept HBA_NONBLOCKING {
    if (!std::isfinite(phase))
        return 0.0;
    const double p = phase - std::floor(phase);
    switch (shape) {
    case LaneWaveform::sine:
        return std::sin(6.283185307179586 * p);
    case LaneWaveform::triangle:
        if (p < 0.25)
            return 4.0 * p;
        if (p < 0.75)
            return 2.0 - 4.0 * p;
        return 4.0 * p - 4.0;
    case LaneWaveform::rampUp:
        return 2.0 * p - 1.0;
    case LaneWaveform::rampDown:
        return 1.0 - 2.0 * p;
    case LaneWaveform::square:
        return p < 0.5 ? 1.0 : -1.0;
    case LaneWaveform::steps: {
        const int step = std::clamp(static_cast<int>(p * kLaneStepsPerCycle), 0, kLaneStepsPerCycle - 1);
        return laneStepValue(lane, step);
    }
    case LaneWaveform::custom:
        break;
    }
    return 0.0;
}

constexpr bool laneModeIsFree(LaneMode mode) noexcept HBA_NONBLOCKING {
    return mode == LaneMode::freeRun || mode == LaneMode::freeRetrig;
}

// Cycle-safe follow-chain resolution: returns `lane` itself or the ultimate leader it follows.
template <std::size_t N>
inline int resolveLaneSource(const std::array<LaneConfig, N>& lanes, int lane) noexcept HBA_NONBLOCKING {
    if constexpr (N == 0)
        return 0;
    if (lane < 0 || lane >= static_cast<int>(N))
        return 0;
    int source = lane;
    for (std::size_t hop = 0; hop < N; ++hop) {
        const int follow = lanes[static_cast<std::size_t>(source)].follow - 1;
        if (follow < 0 || follow >= static_cast<int>(N) || follow == source)
            return source;
        source = follow;
    }
    return lane;
}

// Effective timing mode for `lane`: followers automatically inherit their leader's clock domain
// (Free / Free R Hz vs tempo-synced PPQ) so linked lanes stay phase-locked and ratio-accurate
// across BPMs instead of reading a tempo-synced leader's hidden Hz rate.
template <std::size_t N>
inline LaneMode resolveLaneMode(const std::array<LaneConfig, N>& lanes, int lane) noexcept HBA_NONBLOCKING {
    if constexpr (N == 0)
        return LaneMode::sync;
    if (lane < 0 || lane >= static_cast<int>(N))
        return LaneMode::sync;
    const int source = resolveLaneSource(lanes, lane);
    const LaneMode own = lanes[static_cast<std::size_t>(lane)].mode;
    if (source == lane)
        return own;
    const LaneMode leader = lanes[static_cast<std::size_t>(source)].mode;
    if (laneModeIsFree(leader))
        return (own == LaneMode::freeRetrig || own == LaneMode::retrig) ? LaneMode::freeRetrig : leader;
    if (laneModeIsFree(own))
        return (own == LaneMode::freeRetrig) ? LaneMode::retrig : leader;
    return own;
}

template <std::size_t N>
inline bool resolveLaneUsesFreeClock(const std::array<LaneConfig, N>& lanes, int lane) noexcept HBA_NONBLOCKING {
    return laneModeIsFree(resolveLaneMode(lanes, lane));
}

// Cycle length in quarter notes for tempo-synced modes (leader rate divided by follower ratio).
template <std::size_t N>
inline double resolveLaneLengthQuarters(const std::array<LaneConfig, N>& lanes, int lane) noexcept HBA_NONBLOCKING {
    if constexpr (N == 0)
        return kLaneRateQuarters[0];
    if (lane < 0 || lane >= static_cast<int>(N))
        return kLaneRateQuarters[0];
    const int source = resolveLaneSource(lanes, lane);
    const auto rate = static_cast<std::size_t>(std::clamp(lanes[static_cast<std::size_t>(source)].rateIndex, 0,
                                                          static_cast<int>(kLaneRateQuarters.size()) - 1));
    if (source == lane)
        return kLaneRateQuarters[rate];
    const auto ratio = static_cast<std::size_t>(
        std::clamp(lanes[static_cast<std::size_t>(lane)].ratioIndex, 0, static_cast<int>(kLaneRatios.size()) - 1));
    return kLaneRateQuarters[rate] / kLaneRatios[ratio];
}

// Cycle frequency in Hz for free-running modes (leader Hz multiplied by follower ratio).
template <std::size_t N>
inline double resolveLaneRateHz(const std::array<LaneConfig, N>& lanes, int lane) noexcept HBA_NONBLOCKING {
    if constexpr (N == 0)
        return static_cast<double>(kLaneDefaultHz);
    if (lane < 0 || lane >= static_cast<int>(N))
        return static_cast<double>(kLaneDefaultHz);
    const int source = resolveLaneSource(lanes, lane);
    const double rawHz = lanes[static_cast<std::size_t>(source)].hz;
    const double baseHz = std::isfinite(rawHz)
                              ? std::clamp(rawHz, static_cast<double>(kLaneMinHz), static_cast<double>(kLaneMaxHz))
                              : static_cast<double>(kLaneDefaultHz);
    if (source == lane)
        return baseHz;
    const auto ratio = static_cast<std::size_t>(
        std::clamp(lanes[static_cast<std::size_t>(lane)].ratioIndex, 0, static_cast<int>(kLaneRatios.size()) - 1));
    return baseHz * kLaneRatios[ratio];
}

// Resolved phase in [0, 1) across all 5 studio lane modes.
template <std::size_t N>
inline double resolveLanePhase(const std::array<LaneConfig, N>& lanes, int lane,
                               const LaneClock& clock) noexcept HBA_NONBLOCKING {
    if constexpr (N == 0)
        return 0.0;
    if (lane < 0 || lane >= static_cast<int>(N))
        return 0.0;
    const double phaseDeg = lanes[static_cast<std::size_t>(lane)].phaseDeg;
    const LaneMode mode = resolveLaneMode(lanes, lane);
    if (laneModeIsFree(mode)) {
        const double clockSec = (mode == LaneMode::freeRetrig) ? std::max(0.0, clock.noteSec) : clock.freeSec;
        return lanePhaseHz(clockSec, resolveLaneRateHz(lanes, lane), phaseDeg);
    }
    const double length = resolveLaneLengthQuarters(lanes, lane);
    if (mode == LaneMode::oneShot) {
        const double offset = std::isfinite(phaseDeg) ? std::clamp(phaseDeg / 360.0, 0.0, 0.999999) : 0.0;
        const double notePpq = std::isfinite(clock.notePpq) ? std::max(0.0, clock.notePpq) : 0.0;
        const double progress = notePpq / std::max(length, 1.0e-6);
        return std::clamp(offset + progress, 0.0, 1.0 - 1.0e-6);
    }
    const double clockPpq = (mode == LaneMode::retrig) ? std::max(0.0, clock.notePpq) : clock.hostPpq;
    return lanePhase(clockPpq, length, phaseDeg);
}

// Built-in or drawn-curve waveform value in [-1, 1] at `clock`.
template <std::size_t N>
inline double evaluateLaneOutput(const std::array<LaneConfig, N>& lanes, const std::array<Curve, N>& curves, int lane,
                                 const LaneClock& clock) noexcept HBA_NONBLOCKING {
    if constexpr (N == 0)
        return 0.0;
    if (lane < 0 || lane >= static_cast<int>(N))
        return 0.0;
    const int source = resolveLaneSource(lanes, lane);
    const double phase = resolveLanePhase(lanes, lane, clock);
    const auto shape = static_cast<LaneWaveform>(
        std::clamp(lanes[static_cast<std::size_t>(source)].shape, 0, static_cast<int>(LaneWaveform::custom)));
    if (shape == LaneWaveform::custom)
        return evaluate(curves[static_cast<std::size_t>(source)], phase);
    return laneWaveform(shape, phase, source);
}

// Lane send in [-1, 1] (scaled by depth and global motion, or 0 when lane is off).
template <std::size_t N>
inline double evaluateLaneSend(const std::array<LaneConfig, N>& lanes, const std::array<Curve, N>& curves, int lane,
                               const LaneClock& clock, double motion) noexcept HBA_NONBLOCKING {
    if constexpr (N == 0)
        return 0.0;
    if (lane < 0 || lane >= static_cast<int>(N))
        return 0.0;
    const auto& cfg = lanes[static_cast<std::size_t>(lane)];
    if (!cfg.on)
        return 0.0;
    return evaluateLaneOutput(lanes, curves, lane, clock) * cfg.depth * motion;
}

} // namespace hba::dsp
