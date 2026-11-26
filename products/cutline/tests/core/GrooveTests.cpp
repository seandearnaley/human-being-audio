// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove.h"

#include <catch2/catch_test_macros.hpp>

using namespace cl;

TEST_CASE("A new groove bank holds the default beat in scene A and empty patterns in B-D", "[groove]") {
    const GrooveBank bank;
    const Pattern& a = bank.scenes[0];
    CHECK(patternBars(a) == 1);
    for (int step = 0; step < kStepsPerBar; ++step) {
        const auto si = static_cast<std::size_t>(step);
        CHECK(a.lanes[0].steps[si].on() == (step == 0 || step == 8));
        CHECK(a.lanes[1].steps[si].on() == (step == 4 || step == 12));
        CHECK(a.lanes[4].steps[si].on() == (step % 2 == 0));
    }
    CHECK(a.lanes[0].steps[0].velocity == 96);
    CHECK(a.lanes[1].steps[4].layer == Layer::core);
    for (std::size_t scene = 1; scene < bank.scenes.size(); ++scene)
        CHECK(bank.scenes[scene] == Pattern{});
    CHECK(bank.slicerLength == 16);
}

TEST_CASE("Pattern length helpers clamp bars and loop short lanes", "[groove]") {
    Pattern p{};
    CHECK(patternSteps(p) == 16);
    p.bars = 2;
    CHECK(patternSteps(p) == 32);
    p.bars = 3;
    CHECK(patternBars(p) == 2);
    p.bars = 4;
    CHECK(patternSteps(p) == 64);

    CHECK(laneLoopSteps(p, 6) == 64);
    p.lanes[6].length = 7;
    CHECK(laneLoopSteps(p, 6) == 7);
    CHECK(storedStep(p, 6, 0) == 0);
    CHECK(storedStep(p, 6, 7) == 0);
    CHECK(storedStep(p, 6, 23) == 2);
    p.lanes[6].length = 64;
    CHECK(laneLoopSteps(p, 6) == 64);
}

TEST_CASE("The fill window covers the last 1, 2 or 4 beats of the bar", "[groove]") {
    Fill fill{};
    fill.beats = 1;
    CHECK(fillWindowStart(fill) == 12);
    fill.beats = 2;
    CHECK(fillWindowStart(fill) == 8);
    fill.beats = 4;
    CHECK(fillWindowStart(fill) == 0);
    fill.beats = 3;
    CHECK(fillWindowStart(fill) == 8);
}

TEST_CASE("The groove bank stays small enough to copy on the audio thread", "[groove]") {
    CHECK(sizeof(GrooveBank) < 16384);
}
