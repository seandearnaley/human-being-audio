// The movement panel's editing logic, without a window: picking, snapping,
// bending and the press-drag-release gestures on a curve.
#include "movement_edit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tf;
using namespace tf::ui;
using Catch::Approx;

namespace {
// 160 x 80 pixels from (20, 10): x = 20..180, y = 10 (value 1) .. 90 (value -1).
constexpr edit::Area kArea { 20.0f, 10.0f, 160.0f, 80.0f };
constexpr edit::Reach kReach { 8.0f, 5.0f, 3.0f };

edit::Pos at(float cycle, float value) { return { edit::toX(kArea, cycle), edit::toY(kArea, value) }; }

Curve twoLines() { return curves::make({ { 0.0f, -1.0f }, { 0.5f, 1.0f } }); }
}

TEST_CASE("Area maps a cycle and a value to pixels and back") {
    CHECK(edit::toX(kArea, 0.0f) == 20.0f);
    CHECK(edit::toX(kArea, 1.0f) == 180.0f);
    CHECK(edit::toY(kArea, 1.0f) == 10.0f);
    CHECK(edit::toY(kArea, -1.0f) == 90.0f);
    CHECK(edit::cycleAt(kArea, 100.0f) == Approx(0.5f));
    CHECK(edit::valueAt(kArea, 30.0f) == Approx(0.5f));
    // Outside the area clamps to it.
    CHECK(edit::cycleAt(kArea, 0.0f) == 0.0f);
    CHECK(edit::valueAt(kArea, 200.0f) == -1.0f);
}

TEST_CASE("Snapping goes to sixteenths unless free") {
    CHECK(edit::snap(0.26f, false) == Approx(0.25f));
    CHECK(edit::snap(0.04f, false) == Approx(0.0625f));
    CHECK(edit::snap(0.03f, false) == 0.0f);
    CHECK(edit::snap(0.03f, true) == Approx(0.03f));
}

TEST_CASE("A handle is picked within its reach, nearest first") {
    const Curve curve = twoLines();
    CHECK(edit::pickPoint(curve, kArea, at(0.5f, 1.0f), kReach.handle) == 1);
    CHECK(edit::pickPoint(curve, kArea, { edit::toX(kArea, 0.5f) + 7.0f, edit::toY(kArea, 1.0f) }, kReach.handle) == 1);
    CHECK(edit::pickPoint(curve, kArea, { edit::toX(kArea, 0.5f) + 9.0f, edit::toY(kArea, 1.0f) }, kReach.handle) == -1);
    CHECK(edit::pickPoint(curve, kArea, at(0.02f, -1.0f), kReach.handle) == 0);
}

TEST_CASE("A segment is the one evaluate plays, wrapping before the first point") {
    const Curve curve = curves::make({ { 0.25f, 0.0f }, { 0.75f, 0.0f } });
    CHECK(edit::segmentAt(curve, 0.5f) == 0);
    CHECK(edit::segmentAt(curve, 0.8f) == 1);
    CHECK(edit::segmentAt(curve, 0.1f) == 1);
}

TEST_CASE("Only a line segment near the pointer is picked for bending") {
    Curve curve = twoLines();
    const float value = evaluate(curve, 0.25);
    CHECK(edit::pickSegment(curve, kArea, at(0.25f, value), kReach.line) == 0);
    CHECK(edit::pickSegment(curve, kArea, { edit::toX(kArea, 0.25f), edit::toY(kArea, value) + 6.0f }, kReach.line) == -1);
    curve.points[0].segment = Segment::smooth;
    CHECK(edit::pickSegment(curve, kArea, at(0.25f, value), kReach.line) == -1);
}

TEST_CASE("Bending puts the segment's middle under the pointer") {
    // A rising segment from -1 to 1: dragging its middle down by 0.5 lowers it there.
    const float tension = edit::bend(0.0f, -1.0f, 1.0f, -0.5f);
    CHECK(tension > 0.0f);
    CurvePoint from { 0.0f, -1.0f, tension, Segment::line };
    Curve curve = curves::make({ from, { 0.5f, 1.0f } });
    CHECK(evaluate(curve, 0.25) == Approx(-0.5f).margin(1.0e-4));
    // A falling segment bends the other way for the same drag.
    CHECK(edit::bend(0.0f, 1.0f, -1.0f, -0.5f) < 0.0f);
    // A flat segment cannot bend; the range is limited.
    CHECK(edit::bend(0.3f, 0.2f, 0.2f, 1.0f) == 0.3f);
    CHECK(edit::bend(0.0f, -1.0f, 1.0f, -10.0f) == Approx(1.0f));
    CHECK(edit::bend(0.0f, -1.0f, 1.0f, 10.0f) == Approx(-1.0f));
}

TEST_CASE("Clicking empty space adds a snapped line point that can be dragged") {
    Curve curve = twoLines();
    edit::Gesture gesture;
    CHECK(gesture.press(curve, kArea, at(0.74f, -0.8f), 1, false, kReach));
    REQUIRE(curve.count == 3);
    CHECK(curve.points[2].x == Approx(0.75f));
    CHECK(curve.points[2].y == Approx(-0.8f));
    CHECK(curve.points[2].segment == Segment::line);
    CHECK(gesture.movingPoint() == 2);

    CHECK(gesture.drag(curve, kArea, at(0.9f, 0.0f), false));
    CHECK(curve.points[2].x == Approx(0.875f));
    CHECK(curve.points[2].y == Approx(0.0f));
    CHECK_FALSE(gesture.release(curve, kArea, false));
    CHECK_FALSE(gesture.active());
}

TEST_CASE("Dragging a point keeps it between its neighbours; Alt drags freely") {
    Curve curve = curves::make({ { 0.0f, 0.0f }, { 0.25f, 0.5f }, { 0.5f, 0.0f } });
    edit::Gesture gesture;
    CHECK_FALSE(gesture.press(curve, kArea, at(0.25f, 0.5f), 1, false, kReach));
    CHECK(gesture.movingPoint() == 1);
    gesture.drag(curve, kArea, at(0.9f, 0.2f), false);
    CHECK(curve.points[1].x < 0.5f);
    CHECK(curve.points[1].x > 0.49f);
    gesture.drag(curve, kArea, at(0.3f, 0.2f), true);
    CHECK(curve.points[1].x == Approx(0.3f).margin(1.0e-4));
    // Grabbing off-centre does not make the point jump to the pointer.
    gesture.release(curve, kArea, true);
    const edit::Pos centre = edit::pointPos(kArea, curve.points[1]);
    gesture.press(curve, kArea, { centre.x + 4.0f, centre.y }, 1, true, kReach);
    gesture.drag(curve, kArea, { centre.x + 4.0f, centre.y }, true);
    CHECK(curve.points[1].x == Approx(0.3f).margin(1.0e-4));
}

TEST_CASE("Double-clicking a point removes it, but never the last") {
    Curve curve = twoLines();
    edit::Gesture gesture;
    CHECK(gesture.press(curve, kArea, at(0.5f, 1.0f), 2, false, kReach));
    REQUIRE(curve.count == 1);
    CHECK_FALSE(gesture.press(curve, kArea, at(0.0f, -1.0f), 2, false, kReach));
    CHECK(curve.count == 1);
    // A double-click away from any point does nothing.
    CHECK_FALSE(gesture.press(curve, kArea, at(0.6f, 0.6f), 2, false, kReach));
    CHECK(curve.count == 1);
}

TEST_CASE("Dragging a line segment bends it; a click on it adds a point") {
    Curve curve = twoLines();
    const float value = evaluate(curve, 0.25);
    edit::Gesture gesture;
    CHECK_FALSE(gesture.press(curve, kArea, at(0.25f, value), 1, false, kReach));
    CHECK(gesture.movingPoint() == -1);
    // Within the slop nothing changes.
    CHECK_FALSE(gesture.drag(curve, kArea, { edit::toX(kArea, 0.25f), edit::toY(kArea, value) + 2.0f }, false));
    CHECK(gesture.drag(curve, kArea, at(0.25f, value - 0.5f), false));
    CHECK(curve.points[0].tension > 0.0f);
    CHECK(curve.count == 2);
    CHECK_FALSE(gesture.release(curve, kArea, false));

    Curve clicked = twoLines();
    gesture.press(clicked, kArea, at(0.25f, value), 1, false, kReach);
    CHECK(gesture.release(clicked, kArea, false));
    CHECK(clicked.count == 3);
    CHECK(clicked.points[1].x == Approx(0.25f));
}

TEST_CASE("A full curve takes no more points") {
    Curve curve;
    for (int i = 0; i < kMaxCurvePoints; ++i) addPoint(curve, { static_cast<float>(i) / kMaxCurvePoints, 0.9f });
    edit::Gesture gesture;
    CHECK_FALSE(gesture.press(curve, kArea, at(0.51f, -0.9f), 1, true, kReach));
    CHECK(curve.count == kMaxCurvePoints);
    CHECK_FALSE(gesture.active());
}

TEST_CASE("Each starting curve plays exactly its built-in shape") {
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (const LaneShape shape : { LaneShape::sine, LaneShape::triangle, LaneShape::rampUp, LaneShape::rampDown,
                                       LaneShape::square, LaneShape::steps }) {
            const Curve curve = startingCurve(shape, lane);
            for (int i = 0; i < 200; ++i) {
                const double phase = (i + 0.5) / 200.0;
                INFO("lane " << lane << " shape " << static_cast<int>(shape) << " phase " << phase);
                CHECK(evaluate(curve, phase) == Approx(laneShape(shape, phase, lane)).margin(2.0e-3));
            }
        }
    }
    CHECK(startingCurve(LaneShape::steps, 2).count == kLaneStepsPerCycle);
}
