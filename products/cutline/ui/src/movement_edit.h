// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/ui/model.h"

namespace cl::ui {

Params paramsOf(const Model& model);
LaneCurves curvesOf(const Model& model);
Curve startingCurve(LaneShape shape, int lane);

namespace edit {

inline constexpr int kSnapDivisions = 16;

struct Pos {
    float x = 0.0f;
    float y = 0.0f;
};

struct Area {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct Reach {
    float handle = 8.0f;
    float line = 5.0f;
    float slop = 3.0f;
};

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
    bool active() const { return mode_ != Mode::none; }
    int movingPoint() const { return mode_ == Mode::point ? index_ : -1; }

  private:
    enum class Mode { none, point, segment };
    Mode mode_ = Mode::none;
    int index_ = -1;
    Pos offset_{};
    Pos press_{};
    float tension_ = 0.0f;
    float slop_ = 3.0f;
    bool moved_ = false;
};

} // namespace edit
} // namespace cl::ui
