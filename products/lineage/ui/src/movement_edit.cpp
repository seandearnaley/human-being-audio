// SPDX-License-Identifier: AGPL-3.0-or-later
#include "movement_edit.h"

namespace ln::ui {

Curve startingCurve(LaneShape shape, int lane) {
    switch (shape) {
    case LaneShape::sine:
        return curves::sine();
    case LaneShape::triangle:
        return curves::triangle();
    case LaneShape::rampUp:
        return curves::rampUp();
    case LaneShape::rampDown:
        return curves::rampDown();
    case LaneShape::square:
        return curves::square();
    case LaneShape::steps: {
        Curve curve;
        for (int step = 0; step < hba::dsp::kLaneStepsPerCycle; ++step) {
            addPoint(curve, {static_cast<float>(step) / static_cast<float>(hba::dsp::kLaneStepsPerCycle),
                             static_cast<float>(hba::dsp::laneStepValue(lane, step)), 0.0f, Segment::hold});
        }
        return curve;
    }
    case LaneShape::custom:
        break;
    }
    return curves::sine();
}

} // namespace ln::ui
