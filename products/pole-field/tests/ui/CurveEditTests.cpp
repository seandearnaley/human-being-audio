// The movement panel's editing logic, without a window: picking, snapping,
// bending and press-drag-release gestures on a curve.
#include "movement_edit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pf;
using namespace pf::ui;
using Catch::Approx;

namespace {
constexpr edit::Area kArea { 20.0f, 10.0f, 160.0f, 80.0f };
constexpr edit::Reach kReach { 8.0f, 5.0f, 3.0f };

edit::Pos at(float cycle, float value) { return { edit::toX(kArea, cycle), edit::toY(kArea, value) }; }

Curve twoLines() { return curves::make({ { 0.0f, -1.0f }, { 0.5f, 1.0f } }); }
}

TEST_CASE("Area maps a cycle and a value to pixels and back", "[ui]") {
    CHECK(edit::toX(kArea, 0.0f) == 20.0f);
    CHECK(edit::toX(kArea, 1.0f) == 180.0f);
    CHECK(edit::toY(kArea, 1.0f) == 10.0f);
    CHECK(edit::toY(kArea, -1.0f) == 90.0f);
    CHECK(edit::cycleAt(kArea, 100.0f) == Approx(0.5f));
    CHECK(edit::valueAt(kArea, 30.0f) == Approx(0.5f));
}

TEST_CASE("Clicking, dragging, bending, and double-clicking edit curves cleanly", "[ui]") {
    Curve curve = twoLines();
    edit::Gesture gesture;
    CHECK(gesture.press(curve, kArea, at(0.74f, -0.8f), 1, false, kReach));
    REQUIRE(curve.count == 3);
    CHECK(curve.points[2].x == Approx(0.75f));
    CHECK(gesture.drag(curve, kArea, at(0.9f, 0.0f), false));
    CHECK(curve.points[2].x == Approx(0.875f));
    CHECK_FALSE(gesture.release(curve, kArea, false));

    CHECK(gesture.press(curve, kArea, at(0.875f, 0.0f), 2, false, kReach));
    REQUIRE(curve.count == 2);

    const float value = evaluate(curve, 0.25);
    CHECK_FALSE(gesture.press(curve, kArea, at(0.25f, value), 1, false, kReach));
    CHECK(gesture.drag(curve, kArea, at(0.25f, value - 0.5f), false));
    CHECK(curve.points[0].tension > 0.0f);
}
