// Four tempo-locked movement lanes shared between the engine and the interface.
#pragma once

#include "pf/curve.h"
#include "pf/params.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pf {

using hba::dsp::kLaneStepsPerCycle;
using hba::dsp::laneHash;
using hba::dsp::lanePhase;
using hba::dsp::laneStepValue;

inline double laneShape(LaneShape shape, double phase, int lane) noexcept HBA_NONBLOCKING {
    return hba::dsp::laneWaveform(static_cast<hba::dsp::LaneWaveform>(shape), phase, lane);
}

using LaneCurves = std::array<Curve, kLaneCount>;

inline LaneCurves defaultLaneCurves() noexcept {
    LaneCurves curves;
    curves.fill(curves::sine());
    return curves;
}

inline int laneSource(const Params& params, int lane) noexcept {
    int source = lane;
    for (int hop = 0; hop < kLaneCount; ++hop) {
        const int follow = params.choice(laneParam(source, LaneField::follow)) - 1;
        if (follow < 0 || follow >= kLaneCount || follow == source) return source;
        source = follow;
    }
    return lane;
}

inline double laneLengthQuarters(const Params& params, int lane) noexcept {
    const int source = laneSource(params, lane);
    const auto rate = static_cast<std::size_t>(
        std::clamp(params.choice(laneParam(source, LaneField::rate)), 0, static_cast<int>(kLaneRateQuarters.size()) - 1));
    if (source == lane) return kLaneRateQuarters[rate];
    const auto ratio = static_cast<std::size_t>(
        std::clamp(params.choice(laneParam(lane, LaneField::ratio)), 0, static_cast<int>(kLaneRatios.size()) - 1));
    return kLaneRateQuarters[rate] / kLaneRatios[ratio];
}

inline double laneOutput(const Params& params, const LaneCurves& curves, int lane, double ppq) noexcept {
    const int source = laneSource(params, lane);
    const double phase = lanePhase(ppq, laneLengthQuarters(params, lane), params[laneParam(lane, LaneField::phase)]);
    const auto shape = static_cast<LaneShape>(
        std::clamp(params.choice(laneParam(source, LaneField::shape)), 0, static_cast<int>(LaneShape::custom)));
    if (shape == LaneShape::custom) return evaluate(curves[static_cast<std::size_t>(source)], phase);
    return laneShape(shape, phase, source);
}

inline double laneSend(const Params& params, const LaneCurves& curves, int lane, double ppq) noexcept {
    if (!params.on(laneParam(lane, LaneField::on))) return 0.0;
    return laneOutput(params, curves, lane, ppq) * params[laneParam(lane, LaneField::depth)] * params[ParamId::motion];
}

inline LaneTarget laneTargetOf(const Params& params, int lane) noexcept {
    return static_cast<LaneTarget>(std::clamp(params.choice(laneParam(lane, LaneField::target)), 0, kLaneTargetCount - 1));
}

// How far a full-depth lane moves each target in the target's own units.
inline constexpr std::array<float, kLaneTargetCount> kLaneSpans { {
    0.5f,    // morphX
    0.5f,    // morphY
    0.5f,    // morphZ
    24.0f,   // freq: ±2 octaves
    0.5f,    // res
    1.0f,    // transform
    0.5f,    // drive
    0.5f,    // spread
    0.5f,    // mix
    180.0f,  // fit: degrees, wraps
} };

inline float laneSum(const Params& params, const std::array<float, kLaneCount>& sends, LaneTarget target) noexcept {
    float sum = 0.0f;
    for (int lane = 0; lane < kLaneCount; ++lane)
        if (laneTargetOf(params, lane) == target) sum += sends[static_cast<std::size_t>(lane)];
    return sum;
}

inline float laneApply(LaneTarget target, float base, float sum) noexcept {
    return base + kLaneSpans[static_cast<std::size_t>(target)] * sum;
}

}
