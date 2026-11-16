#include "tf/curve.h"
#include "tf/lanes.h"
#include "tf/params.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <random>

using Catch::Approx;
using namespace tf;

TEST_CASE("an empty curve is zero and a single point is constant") {
    Curve curve;
    CHECK(evaluate(curve, 0.3) == 0.0f);
    addPoint(curve, { 0.4f, 0.7f });
    CHECK(evaluate(curve, 0.0) == Approx(0.7f));
    CHECK(evaluate(curve, 0.9) == Approx(0.7f));
}

TEST_CASE("a curve passes through its points and wraps") {
    const Curve curve = curves::make({ { 0.1f, -0.5f }, { 0.5f, 1.0f }, { 0.8f, 0.2f } });
    CHECK(evaluate(curve, 0.1) == Approx(-0.5f));
    CHECK(evaluate(curve, 0.5) == Approx(1.0f));
    CHECK(evaluate(curve, 0.8) == Approx(0.2f));
    CHECK(evaluate(curve, 0.3) == Approx(0.25f));

    // The last segment runs from 0.8 round to 0.1 of the next cycle.
    CHECK(evaluate(curve, 0.95) == Approx(0.2f + (-0.5f - 0.2f) * 0.5f));
    CHECK(evaluate(curve, 0.0) == Approx(0.2f + (-0.5f - 0.2f) * (0.2f / 0.3f)).margin(1e-5));
    CHECK(evaluate(curve, 1.3) == Approx(evaluate(curve, 0.3)));
    CHECK(evaluate(curve, -0.7) == Approx(evaluate(curve, 0.3)));
}

TEST_CASE("hold segments step and smooth segments are flat at their ends") {
    const Curve steps = curves::square();
    CHECK(evaluate(steps, 0.0) == 1.0f);
    CHECK(evaluate(steps, 0.49) == 1.0f);
    CHECK(evaluate(steps, 0.5) == -1.0f);
    CHECK(evaluate(steps, 0.99) == -1.0f);

    const Curve smooth = curves::make({ { 0.0f, -1.0f, 0.0f, Segment::smooth }, { 0.5f, 1.0f, 0.0f, Segment::smooth } });
    CHECK(evaluate(smooth, 0.25) == Approx(0.0f).margin(1e-6));
    CHECK(evaluate(smooth, 0.01) - evaluate(smooth, 0.0) < 0.01f);
    CHECK(evaluate(smooth, 0.5) - evaluate(smooth, 0.49) < 0.01f);
}

TEST_CASE("tension bends a line either way and never leaves its end values") {
    const auto tension = GENERATE(-1.0f, -0.5f, 0.5f, 1.0f);
    const Curve curve = curves::make({ { 0.0f, 0.0f, tension }, { 0.5f, 1.0f, 0.0f, Segment::hold } });
    const float middle = evaluate(curve, 0.25);
    if (tension < 0.0f) CHECK(middle > 0.5f); else CHECK(middle < 0.5f);
    for (int i = 0; i <= 50; ++i) {
        const float y = evaluate(curve, 0.5 * i / 50.0);
        CHECK(y >= 0.0f);
        CHECK(y <= 1.0f);
    }
}

TEST_CASE("the starting curves match the built-in lane shapes") {
    struct Pair {
        LaneShape shape;
        Curve curve;
        float tolerance;
    };
    const auto pair = GENERATE(Pair { LaneShape::sine, curves::sine(), 1e-5f },
                               Pair { LaneShape::triangle, curves::triangle(), 1e-4f },
                               Pair { LaneShape::rampUp, curves::rampUp(), 1e-3f },
                               Pair { LaneShape::rampDown, curves::rampDown(), 1e-3f },
                               Pair { LaneShape::square, curves::square(), 1e-6f });
    // Stay off the discontinuities, where the two definitions may jump a sample apart.
    for (int i = 1; i < 200; ++i) {
        const double phase = i / 200.0;
        if (std::abs(phase - 0.5) < 0.006) continue;
        CAPTURE(static_cast<int>(pair.shape), phase);
        CHECK(evaluate(pair.curve, phase) == Approx(laneShape(pair.shape, phase, 0)).margin(pair.tolerance));
    }
}

TEST_CASE("editing keeps points sorted, in range and within capacity") {
    std::mt19937 random(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    Curve curve;
    for (int i = 0; i < 100; ++i) {
        const int action = static_cast<int>(unit(random) * 3.0f);
        if (action == 0)
            addPoint(curve, { unit(random) * 1.4f - 0.2f, unit(random) * 3.0f - 1.5f });
        else if (action == 1 && curve.count > 0)
            removePoint(curve, static_cast<int>(unit(random) * static_cast<float>(curve.count)));
        else if (curve.count > 0)
            movePoint(curve, static_cast<int>(unit(random) * static_cast<float>(curve.count)), unit(random) * 1.4f - 0.2f,
                      unit(random) * 3.0f - 1.5f);

        REQUIRE(curve.count >= 0);
        REQUIRE(curve.count <= kMaxCurvePoints);
        for (int p = 0; p < curve.count; ++p) {
            const auto& point = curve.points[static_cast<std::size_t>(p)];
            REQUIRE(point.x >= 0.0f);
            REQUIRE(point.x < 1.0f);
            REQUIRE(point.y >= -1.0f);
            REQUIRE(point.y <= 1.0f);
            if (p > 0) REQUIRE(point.x >= curve.points[static_cast<std::size_t>(p - 1)].x);
        }
        const float value = evaluate(curve, unit(random));
        REQUIRE(std::isfinite(value));
        REQUIRE(std::abs(value) <= 1.0f);
    }

    // A full curve refuses more points, and the last point cannot be removed.
    Curve full;
    for (int i = 0; i < kMaxCurvePoints; ++i) CHECK(addPoint(full, { static_cast<float>(i) / kMaxCurvePoints, 0.0f }) >= 0);
    CHECK(addPoint(full, { 0.5f, 0.5f }) == -1);
    Curve one = curves::make({ { 0.2f, 0.3f } });
    removePoint(one, 0);
    CHECK(one.count == 1);
}

TEST_CASE("a lane follows another's shape at a ratio, and loops fall back") {
    Params params = Params::defaults();
    const LaneCurves curves = defaultLaneCurves();

    // Lane 2 follows lane 1 at twice its rate.
    params[laneParam(0, LaneField::rate)] = 3.0f;    // 1 bar
    params[laneParam(0, LaneField::shape)] = static_cast<float>(LaneShape::rampUp);
    params[laneParam(0, LaneField::phase)] = 0.0f;
    params[laneParam(1, LaneField::follow)] = 1.0f;  // Lane 1
    params[laneParam(1, LaneField::ratio)] = 4.0f;   // x2
    params[laneParam(1, LaneField::phase)] = 0.0f;
    params[laneParam(1, LaneField::shape)] = static_cast<float>(LaneShape::square);  // ignored while following

    CHECK(laneSource(params, 1) == 0);
    CHECK(laneLengthQuarters(params, 0) == 4.0);
    CHECK(laneLengthQuarters(params, 1) == 2.0);
    CHECK(laneOutput(params, curves, 0, 1.0) == Approx(-0.5));
    CHECK(laneOutput(params, curves, 1, 1.0) == Approx(0.0));   // halfway through its own, shorter cycle
    CHECK(laneOutput(params, curves, 1, 0.5) == Approx(laneOutput(params, curves, 0, 1.0)));

    // A chain resolves to its end; a loop falls back to the lane itself.
    params[laneParam(2, LaneField::follow)] = 2.0f;  // Lane 2, which follows Lane 1
    CHECK(laneSource(params, 2) == 0);
    params[laneParam(0, LaneField::follow)] = 3.0f;  // Lane 1 follows Lane 3: a loop
    CHECK(laneSource(params, 0) == 0);
    CHECK(laneSource(params, 2) == 2);
}

TEST_CASE("a Custom lane plays its drawn curve and sends depth times Motion") {
    Params params = Params::defaults();
    LaneCurves curves = defaultLaneCurves();
    curves[2] = curves::make({ { 0.0f, 0.25f, 0.0f, Segment::hold }, { 0.5f, -0.75f, 0.0f, Segment::hold } });

    params[laneParam(2, LaneField::shape)] = static_cast<float>(LaneShape::custom);
    params[laneParam(2, LaneField::rate)] = 5.0f;   // 1/4
    params[laneParam(2, LaneField::phase)] = 0.0f;
    params[laneParam(2, LaneField::depth)] = -0.5f;
    params[ParamId::motion] = 0.8f;

    CHECK(laneOutput(params, curves, 2, 0.25) == Approx(0.25));
    CHECK(laneOutput(params, curves, 2, 0.75) == Approx(-0.75));
    CHECK(laneSend(params, curves, 2, 0.75) == Approx(-0.75 * -0.5 * 0.8));
    params[laneParam(2, LaneField::on)] = 0.0f;
    CHECK(laneSend(params, curves, 2, 0.75) == 0.0);
}

TEST_CASE("lanes aimed at one target add up, and Reach moves by ratio") {
    Params params = Params::defaults();
    params[laneParam(0, LaneField::target)] = static_cast<float>(LaneTarget::reach);
    params[laneParam(1, LaneField::target)] = static_cast<float>(LaneTarget::reach);
    params[laneParam(2, LaneField::target)] = static_cast<float>(LaneTarget::morph);
    const std::array<float, kLaneCount> sends { 0.25f, 0.25f, -1.0f, 0.5f };

    CHECK(laneSum(params, sends, LaneTarget::reach) == Approx(0.5f));
    CHECK(laneSum(params, sends, LaneTarget::morph) == Approx(-1.0f));
    CHECK(laneSum(params, sends, LaneTarget::fit) == 0.0f);

    CHECK(laneApply(LaneTarget::reach, 16.0f, 0.5f) == Approx(32.0f));   // half of two octaves up
    CHECK(laneApply(LaneTarget::reach, 16.0f, -1.0f) == Approx(4.0f));
    CHECK(laneApply(LaneTarget::morph, 0.5f, -1.0f) == Approx(0.0f));
    CHECK(laneApply(LaneTarget::fit, 90.0f, 0.5f) == Approx(180.0f));
}
