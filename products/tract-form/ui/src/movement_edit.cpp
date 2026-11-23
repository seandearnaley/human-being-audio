#include "movement_edit.h"

namespace tf::ui {

Params paramsOf(const Model& model) {
    Params params;
    for (int i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        params[id] = model.get(id);
    }
    return params;
}

LaneCurves curvesOf(const Model& model) {
    LaneCurves curves;
    for (int lane = 0; lane < kLaneCount; ++lane) curves[static_cast<std::size_t>(lane)] = model.laneCurve(lane);
    return curves;
}

Curve startingCurve(LaneShape shape, int lane) {
    switch (shape) {
        case LaneShape::sine: return curves::sine();
        case LaneShape::triangle: return curves::triangle();
        case LaneShape::rampUp: return curves::rampUp();
        case LaneShape::rampDown: return curves::rampDown();
        case LaneShape::square: return curves::square();
        case LaneShape::steps: {
            Curve curve;
            for (int step = 0; step < kLaneStepsPerCycle; ++step)
                addPoint(curve, { static_cast<float>(step) / kLaneStepsPerCycle,
                                  static_cast<float>(laneStepValue(lane, step)), 0.0f, Segment::hold });
            return curve;
        }
        case LaneShape::custom: break;
    }
    return curves::sine();
}
}
