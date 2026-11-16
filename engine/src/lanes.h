// Movement lanes: a phase locked to the host position and a shape. The shapes
// themselves live in core (tf/lanes.h) so the interface draws what plays here.
#pragma once

#include "control.h"
#include "tf/lanes.h"
#include "tf/params.h"

#include <algorithm>

namespace tf::dsp {

inline constexpr int kStepsPerCycle = kLaneStepsPerCycle;

inline constexpr double stepValue(int lane, int step) noexcept { return laneStepValue(lane, step); }

inline double lanePhase(double ppq, int rateIndex, double phaseDegrees) noexcept TF_NONBLOCKING {
    const auto rate = static_cast<std::size_t>(std::clamp(rateIndex, 0, static_cast<int>(kLaneRateQuarters.size()) - 1));
    return tf::lanePhase(ppq, kLaneRateQuarters[rate], phaseDegrees);
}

using tf::laneShape;

}
