// SPDX-License-Identifier: GPL-3.0-or-later
#include <hba/dsp/curve.h>
#include <hba/dsp/lane_math.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using namespace hba::dsp;

TEST_CASE("hba::dsp::Curve evaluates periodic shapes and tensions cleanly", "[dsp][curve]") {
    const Curve sine = curves::sine();
    REQUIRE(sine.count == 2);
    CHECK(evaluate(sine, 0.0) == Approx(0.0f).margin(1e-5f));
    CHECK(evaluate(sine, 0.25) == Approx(1.0f).margin(1e-5f));
    CHECK(evaluate(sine, 0.5) == Approx(0.0f).margin(1e-5f));
    CHECK(evaluate(sine, 0.75) == Approx(-1.0f).margin(1e-5f));
    CHECK(evaluate(sine, 1.25) == Approx(1.0f).margin(1e-5f));
    CHECK(evaluate(sine, -0.25) == Approx(-1.0f).margin(1e-5f));

    const Curve sq = curves::square();
    CHECK(evaluate(sq, 0.0) == Approx(1.0f));
    CHECK(evaluate(sq, 0.49) == Approx(1.0f));
    CHECK(evaluate(sq, 0.5) == Approx(-1.0f));
    CHECK(evaluate(sq, 0.99) == Approx(-1.0f));

    // Line tension: negative rises above linear midpoint, positive sags below it.
    Curve bent = curves::make({{0.0f, -1.0f, -0.5f, Segment::line}, {0.5f, 1.0f, 0.5f, Segment::line}});
    CHECK(evaluate(bent, 0.25) > 0.0f);
    CHECK(evaluate(bent, 0.75) > 0.0f);
}

TEST_CASE("hba::dsp::Curve edit helpers preserve ordering, bounds and capacity", "[dsp][curve]") {
    Curve c;
    CHECK(evaluate(c, 0.3) == Approx(0.0f));

    REQUIRE(addPoint(c, {0.5f, 0.25f}) == 0);
    REQUIRE(addPoint(c, {0.1f, -0.5f}) == 0);
    REQUIRE(addPoint(c, {0.9f, 0.75f}) == 2);
    REQUIRE(c.count == 3);
    CHECK(c.points[0].x == Approx(0.1f));
    CHECK(c.points[1].x == Approx(0.5f));
    CHECK(c.points[2].x == Approx(0.9f));

    // Moving point 1 cannot cross neighbours.
    const CurvePoint moved = movePoint(c, 1, 0.99f, 2.0f);
    CHECK(moved.x < c.points[2].x);
    CHECK(moved.y == Approx(1.0f));

    // Removing down to 1 point stops at 1.
    removePoint(c, 1);
    removePoint(c, 0);
    REQUIRE(c.count == 1);
    removePoint(c, 0);
    REQUIRE(c.count == 1);

    // Capacity cap at kMaxCurvePoints.
    Curve full;
    for (int i = 0; i < kMaxCurvePoints; ++i) {
        CHECK(addPoint(full, {static_cast<float>(i) / static_cast<float>(kMaxCurvePoints), 0.0f}) >= 0);
    }
    CHECK(addPoint(full, {0.5f, 0.0f}) == -1);
}

TEST_CASE("hba::dsp::lane_math is deterministic and bounded", "[dsp][curve]") {
    // Lowbias32 determinism check.
    CHECK(laneHash(1U) == laneHash(1U));
    CHECK(laneHash(1U) != laneHash(2U));

    for (int lane = 0; lane < 4; ++lane) {
        for (int step = 0; step < kLaneStepsPerCycle; ++step) {
            const double v = laneStepValue(lane, step);
            CHECK(v >= -1.0);
            CHECK(v <= 1.0);
        }
    }

    CHECK(lanePhase(0.0, 4.0, 0.0) == Approx(0.0));
    CHECK(lanePhase(1.0, 4.0, 90.0) == Approx(0.5));
    CHECK(lanePhase(-1.0, 4.0, 0.0) == Approx(0.75));

    CHECK(laneWaveform(LaneWaveform::sine, 0.25, 0) == Approx(1.0));
    CHECK(laneWaveform(LaneWaveform::triangle, 0.25, 0) == Approx(1.0));
    CHECK(laneWaveform(LaneWaveform::triangle, 0.75, 0) == Approx(-1.0));
    CHECK(laneWaveform(LaneWaveform::rampUp, 0.5, 0) == Approx(0.0));
    CHECK(laneWaveform(LaneWaveform::rampDown, 0.5, 0) == Approx(0.0));
    CHECK(laneWaveform(LaneWaveform::square, 0.25, 0) == Approx(1.0));
    CHECK(laneWaveform(LaneWaveform::square, 0.75, 0) == Approx(-1.0));
}

TEST_CASE("hba::dsp shared lane router handles follow chains, loops, Free Hz inheritance, and all 5 modes",
          "[dsp][curve]") {
    std::array<LaneConfig, 4> lanes{};
    std::array<Curve, 4> curves{};
    curves.fill(curves::sine());

    // Lane 0: Free 0.5 Hz, RampUp. Lane 1 follows Lane 0 at x2 (1.0 Hz) with default Sync mode -> inherits Free!
    lanes[0] = {true, 3, 0.0, 1.0, static_cast<int>(LaneWaveform::rampUp), 0, 3, LaneMode::freeRun, 0.5};
    lanes[1] = {true, 5, 90.0, 0.5, static_cast<int>(LaneWaveform::square), 1, 4, LaneMode::sync, 0.2};
    // Lane 2 follows Lane 3 and Lane 3 follows Lane 2 -> cycle guard falls back to self.
    lanes[2] = {true, 4, 0.0, 1.0, static_cast<int>(LaneWaveform::sine), 4, 3, LaneMode::oneShot, 0.5};
    lanes[3] = {true, 4, 0.0, 1.0, static_cast<int>(LaneWaveform::sine), 3, 3, LaneMode::retrig, 0.5};

    CHECK(resolveLaneSource(lanes, 0) == 0);
    CHECK(resolveLaneSource(lanes, 1) == 0);
    CHECK(resolveLaneSource(lanes, 2) == 2);
    CHECK(resolveLaneSource(lanes, 3) == 3);

    CHECK(resolveLaneUsesFreeClock(lanes, 0));
    CHECK(resolveLaneUsesFreeClock(lanes, 1));
    CHECK_FALSE(resolveLaneUsesFreeClock(lanes, 2));
    CHECK(resolveLaneRateHz(lanes, 0) == Approx(0.5));
    CHECK(resolveLaneRateHz(lanes, 1) == Approx(1.0));

    // At freeSec = 0.5: lane 0 phase = 0.25 (rampUp -> -0.5); lane 1 phase = (0.5*1.0 + 0.25) = 0.75 (rampUp -> +0.5).
    const LaneClock clk{10.0, 3.0, 0.5, 0.25};
    CHECK(resolveLanePhase(lanes, 0, clk) == Approx(0.25));
    CHECK(resolveLanePhase(lanes, 1, clk) == Approx(0.75));
    CHECK(evaluateLaneOutput(lanes, curves, 0, clk) == Approx(-0.5));
    CHECK(evaluateLaneSend(lanes, curves, 1, clk, 0.8) == Approx(0.5 * 0.5 * 0.8));

    // One-shot clamps at end of cycle without wrapping.
    CHECK(resolveLanePhase(lanes, 2, clk) == Approx(1.0 - 1.0e-6));
    // Retrig uses notePpq (3.0 / 2.0 = 1.5 -> 0.5).
    CHECK(resolveLanePhase(lanes, 3, clk) == Approx(0.5));

    // Free R leader: follower inherits freeRetrig and tracks noteSec.
    lanes[0].mode = LaneMode::freeRetrig;
    CHECK(resolveLaneMode(lanes, 1) == LaneMode::freeRetrig);
    CHECK(resolveLanePhase(lanes, 1, clk) == Approx(0.5));
}
