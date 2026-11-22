#pragma once

#include "pf/lanes.h"
#include "pf/ui/model.h"

namespace pf::ui {

Params paramsOf(const Model& model);
LaneCurves curvesOf(const Model& model);
Curve startingCurve(LaneShape shape, int lane);

namespace edit {

struct Area {
    float x = 0.0f, y = 0.0f, width = 1.0f, height = 1.0f;
};

struct Pos {
    float x = 0.0f, y = 0.0f;
};

struct Reach {
    float handle = 8.0f;
    float line = 5.0f;
    float slop = 3.0f;
};

inline constexpr int kSnapDivisions = 16;

float toX(const Area& area, float cycle);
float toY(const Area& area, float value);
float cycleAt(const Area& area, float x);
float valueAt(const Area& area, float y);
Pos pointPos(const Area& area, const CurvePoint& point);
float snap(float cycle, bool free);
int pickPoint(const Curve& curve, const Area& area, Pos at, float radius);
int segmentAt(const Curve& curve, float cycle);
int pickSegment(const Curve& curve, const Area& area, Pos at, float tolerance);
float bend(float tensionAtPress, float fromY, float toY, float dragValue);

class Gesture {
public:
    bool press(Curve& curve, const Area& area, Pos at, int clicks, bool free, const Reach& reach);
    bool drag(Curve& curve, const Area& area, Pos at, bool free);
    bool release(Curve& curve, const Area& area, bool free);

    int movingPoint() const { return mode_ == Mode::point ? index_ : -1; }
    bool active() const { return mode_ != Mode::none; }

private:
    enum class Mode { none, point, segment };

    Mode mode_ = Mode::none;
    int index_ = -1;
    Pos press_ {};
    Pos offset_ {};
    float tension_ = 0.0f;
    float slop_ = 0.0f;
    bool moved_ = false;
};

}

}
