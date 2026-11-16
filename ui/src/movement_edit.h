// What the movement panel needs that does not touch a window: reading the lanes
// from the model, the curve a built-in shape starts from, and the geometry of
// editing a curve with the mouse. Pure functions, so they can be unit tested.
#pragma once

#include "tf/lanes.h"
#include "tf/ui/model.h"

namespace tf::ui {

// The model's parameters and curves as the core's lane functions take them.
Params paramsOf(const Model& model);
LaneCurves curvesOf(const Model& model);

// The curve a built-in shape becomes when it is first edited. It plays exactly
// as the shape did: Steps becomes eight held points with the lane's own values.
Curve startingCurve(LaneShape shape, int lane);

namespace edit {

// The area one cycle is drawn in, in pixels: x is 0..1 of the cycle left to
// right, y is 1 at the top and -1 at the bottom.
struct Area {
    float x = 0.0f, y = 0.0f, width = 1.0f, height = 1.0f;
};

struct Pos {
    float x = 0.0f, y = 0.0f;
};

// Sizes in pixels, already scaled.
struct Reach {
    float handle = 8.0f;  // how far from a point's centre a press still picks it
    float line = 5.0f;    // how far from a line segment a press bends it
    float slop = 3.0f;    // movement before a press on a segment counts as a drag
};

inline constexpr int kSnapDivisions = 16;

float toX(const Area& area, float cycle);
float toY(const Area& area, float value);
float cycleAt(const Area& area, float x);
float valueAt(const Area& area, float y);
Pos pointPos(const Area& area, const CurvePoint& point);

// To the nearest sixteenth of the cycle, unless `free`.
float snap(float cycle, bool free);

// The point whose handle is under `at` (the nearest one), or -1.
int pickPoint(const Curve& curve, const Area& area, Pos at, float radius);

// The point whose segment contains `cycle`, as `evaluate` reads it: before the
// first point, that is the last point's segment wrapping round.
int segmentAt(const Curve& curve, float cycle);

// The `line` segment whose drawn curve passes within `tolerance` of `at`, or -1.
int pickSegment(const Curve& curve, const Area& area, Pos at, float tolerance);

// A segment's tension after the pointer has moved `dragValue` (value units, up
// positive) since the press: the segment's middle follows the pointer.
float bend(float tensionAtPress, float fromY, float toY, float dragValue);

// One press-drag-release on the curve. Each call returns true when it changed
// the curve.
class Gesture {
public:
    // `clicks` is the press's repeat count: 2 removes a point.
    bool press(Curve& curve, const Area& area, Pos at, int clicks, bool free, const Reach& reach);
    bool drag(Curve& curve, const Area& area, Pos at, bool free);
    bool release(Curve& curve, const Area& area, bool free);

    // The point being moved, for drawing it highlighted; -1 otherwise.
    int movingPoint() const { return mode_ == Mode::point ? index_ : -1; }
    bool active() const { return mode_ != Mode::none; }

private:
    enum class Mode { none, point, segment };

    Mode mode_ = Mode::none;
    int index_ = -1;
    Pos press_ {};
    Pos offset_ {};  // the handle's centre minus the press
    float tension_ = 0.0f;
    float slop_ = 0.0f;
    bool moved_ = false;
};

}

}
