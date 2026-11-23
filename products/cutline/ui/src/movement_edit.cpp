// SPDX-License-Identifier: AGPL-3.0-or-later
#include "movement_edit.h"

#include <algorithm>
#include <cmath>

namespace cl::ui {

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
    for (int lane = 0; lane < kLaneCount; ++lane)
        curves[static_cast<std::size_t>(lane)] = model.laneCurve(lane);
    return curves;
}

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
        for (int step = 0; step < kLaneStepsPerCycle; ++step)
            addPoint(curve, {static_cast<float>(step) / kLaneStepsPerCycle,
                             static_cast<float>(laneStepValue(lane, step)), 0.0f, Segment::hold});
        return curve;
    }
    case LaneShape::custom:
        break;
    }
    return curves::sine();
}

namespace edit {

namespace {
constexpr float kLowestTension = -1.0f;
constexpr float kHighestTension = 1.0f;

float distance(Pos a, Pos b) { return std::hypot(a.x - b.x, a.y - b.y); }
float middleOf(float tension) { return std::pow(0.5f, std::pow(8.0f, tension)); }
} // namespace

float toX(const Area& area, float cycle) { return area.x + area.width * cycle; }
float toY(const Area& area, float value) { return area.y + area.height * (1.0f - value) * 0.5f; }
float cycleAt(const Area& area, float x) { return std::clamp((x - area.x) / area.width, 0.0f, 1.0f); }
float valueAt(const Area& area, float y) { return std::clamp(1.0f - 2.0f * (y - area.y) / area.height, -1.0f, 1.0f); }
Pos pointPos(const Area& area, const CurvePoint& point) { return {toX(area, point.x), toY(area, point.y)}; }

float snap(float cycle, bool free) {
    if (free)
        return cycle;
    return std::round(cycle * kSnapDivisions) / kSnapDivisions;
}

int pickPoint(const Curve& curve, const Area& area, Pos at, float radius) {
    int best = -1;
    float nearest = radius;
    for (int i = 0; i < curve.count; ++i) {
        const float d = distance(pointPos(area, curve.points[static_cast<std::size_t>(i)]), at);
        if (d <= nearest) {
            nearest = d;
            best = i;
        }
    }
    return best;
}

int segmentAt(const Curve& curve, float cycle) {
    if (curve.count <= 0)
        return -1;
    int from = curve.count - 1;
    for (int i = 0; i < curve.count; ++i) {
        if (curve.points[static_cast<std::size_t>(i)].x > cycle)
            break;
        from = i;
    }
    return from;
}

int pickSegment(const Curve& curve, const Area& area, Pos at, float tolerance) {
    if (curve.count < 2)
        return -1;
    const float cycle = cycleAt(area, at.x);
    const int index = segmentAt(curve, cycle);
    if (curve.points[static_cast<std::size_t>(index)].segment != Segment::line)
        return -1;
    const float y = toY(area, evaluate(curve, cycle));
    return std::abs(y - at.y) <= tolerance ? index : -1;
}

float bend(float tensionAtPress, float fromY, float toY, float dragValue) {
    const float rise = toY - fromY;
    if (std::abs(rise) < 1.0e-3f)
        return tensionAtPress;
    const float middle =
        std::clamp(middleOf(tensionAtPress) + dragValue / rise, middleOf(kHighestTension), middleOf(kLowestTension));
    const float exponent = std::log(middle) / std::log(0.5f);
    return std::clamp(std::log(exponent) / std::log(8.0f), kLowestTension, kHighestTension);
}

bool Gesture::press(Curve& curve, const Area& area, Pos at, int clicks, bool free, const Reach& reach) {
    mode_ = Mode::none;
    index_ = -1;
    moved_ = false;
    press_ = at;
    slop_ = reach.slop;

    const int point = pickPoint(curve, area, at, reach.handle);
    if (clicks >= 2) {
        if (point < 0 || curve.count <= 1)
            return false;
        removePoint(curve, point);
        return true;
    }
    if (point >= 0) {
        const Pos centre = pointPos(area, curve.points[static_cast<std::size_t>(point)]);
        mode_ = Mode::point;
        index_ = point;
        offset_ = {centre.x - at.x, centre.y - at.y};
        return false;
    }
    const int segment = pickSegment(curve, area, at, reach.line);
    if (segment >= 0) {
        mode_ = Mode::segment;
        index_ = segment;
        tension_ = curve.points[static_cast<std::size_t>(segment)].tension;
        return false;
    }
    const int added = addPoint(curve, {snap(cycleAt(area, at.x), free), valueAt(area, at.y)});
    if (added < 0)
        return false;
    mode_ = Mode::point;
    index_ = added;
    offset_ = {};
    return true;
}

bool Gesture::drag(Curve& curve, const Area& area, Pos at, bool free) {
    if (mode_ == Mode::none || index_ < 0 || index_ >= curve.count)
        return false;
    const auto i = static_cast<std::size_t>(index_);
    const CurvePoint before = curve.points[i];

    if (mode_ == Mode::point) {
        movePoint(curve, index_, snap(cycleAt(area, at.x + offset_.x), free), valueAt(area, at.y + offset_.y));
        return !(curve.points[i] == before);
    }

    if (!moved_ && distance(at, press_) < slop_)
        return false;
    moved_ = true;
    const CurvePoint& next = curve.points[index_ + 1 < curve.count ? i + 1 : 0];
    const float dragValue = valueAt(area, at.y) - valueAt(area, press_.y);
    curve.points[i].tension = bend(tension_, before.y, next.y, dragValue);
    return curve.points[i].tension != before.tension;
}

bool Gesture::release(Curve& curve, const Area& area, bool free) {
    const bool clickedSegment = mode_ == Mode::segment && !moved_;
    mode_ = Mode::none;
    index_ = -1;
    if (clickedSegment)
        return addPoint(curve, {snap(cycleAt(area, press_.x), free), valueAt(area, press_.y)}) >= 0;
    return false;
}

} // namespace edit
} // namespace cl::ui
