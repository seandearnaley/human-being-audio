// SPDX-License-Identifier: AGPL-3.0-or-later
// Four tempo-locked MIDI/MPE expression lanes shared between engine and UI.
#pragma once

#include "cl/curve.h"
#include "cl/nonblocking.h"
#include "cl/params.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

using hba::dsp::kLaneStepsPerCycle;
using hba::dsp::laneHash;
using hba::dsp::lanePhase;
using hba::dsp::laneStepValue;

inline double laneShape(LaneShape shape, double phase, int lane) noexcept CL_NONBLOCKING {
    return hba::dsp::laneWaveform(static_cast<hba::dsp::LaneWaveform>(shape), phase, lane);
}

using LaneCurves = std::array<Curve, kLaneCount>;

inline LaneCurves defaultLaneCurves() noexcept CL_NONBLOCKING {
    LaneCurves curves;
    curves[0] = curves::sine();
    curves[1] = curves::make({{0.0f, -0.6f, -0.35f, Segment::smooth},
                              {0.3f, 0.8f, 0.2f, Segment::smooth},
                              {0.6f, -0.2f, -0.2f, Segment::line},
                              {0.82f, 0.95f, 0.4f, Segment::smooth}});
    curves[2] = curves::triangle();
    curves[3] = curves::rampUp();
    return curves;
}

inline int laneSource(const Params& params, int lane) noexcept CL_NONBLOCKING {
    int source = lane;
    for (int hop = 0; hop < kLaneCount; ++hop) {
        const int follow = params.choice(laneParam(source, LaneField::follow)) - 1;
        if (follow < 0 || follow >= kLaneCount || follow == source)
            return source;
        source = follow;
    }
    return lane;
}

inline double laneLengthQuarters(const Params& params, int lane) noexcept CL_NONBLOCKING {
    const int source = laneSource(params, lane);
    const auto rate = static_cast<std::size_t>(std::clamp(params.choice(laneParam(source, LaneField::rate)), 0,
                                                          static_cast<int>(kLaneRateQuarters.size()) - 1));
    if (source == lane)
        return kLaneRateQuarters[rate];
    const auto ratio = static_cast<std::size_t>(
        std::clamp(params.choice(laneParam(lane, LaneField::ratio)), 0, static_cast<int>(kLaneRatios.size()) - 1));
    return kLaneRateQuarters[rate] / kLaneRatios[ratio];
}

inline double laneOutput(const Params& params, const LaneCurves& curves, int lane, double ppq) noexcept CL_NONBLOCKING {
    const int source = laneSource(params, lane);
    const double phase = lanePhase(ppq, laneLengthQuarters(params, lane), params[laneParam(lane, LaneField::phase)]);
    const auto shape = static_cast<LaneShape>(
        std::clamp(params.choice(laneParam(source, LaneField::shape)), 0, static_cast<int>(LaneShape::custom)));
    if (shape == LaneShape::custom)
        return evaluate(curves[static_cast<std::size_t>(source)], phase);
    return laneShape(shape, phase, source);
}

inline double laneSend(const Params& params, const LaneCurves& curves, int lane, double ppq) noexcept CL_NONBLOCKING {
    if (!params.on(laneParam(lane, LaneField::on)))
        return 0.0;
    return laneOutput(params, curves, lane, ppq) * params[laneParam(lane, LaneField::depth)] * params[ParamId::motion];
}

inline LaneTarget laneTargetOf(const Params& params, int lane) noexcept CL_NONBLOCKING {
    return static_cast<LaneTarget>(
        std::clamp(params.choice(laneParam(lane, LaneField::target)), 0, kLaneTargetCount - 1));
}

inline float laneSum(const Params& params, const std::array<float, kLaneCount>& sends,
                     LaneTarget target) noexcept CL_NONBLOCKING {
    float sum = 0.0f;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        if (laneTargetOf(params, lane) == target)
            sum += sends[static_cast<std::size_t>(lane)];
    }
    return sum;
}

} // namespace cl
