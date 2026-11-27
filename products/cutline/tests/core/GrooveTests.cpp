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

TEST_CASE("A new bank has the default slicer gate", "[groove]") {
    const GrooveBank bank;
    for (int step = 0; step < kMaxSlicerSteps; ++step) {
        const bool expected = step == 0 || step == 3 || step == 6 || step == 10 || step == 14;
        CHECK(bank.slicerSteps[static_cast<std::size_t>(step)].on() == expected);
    }
    CHECK(bank.slicerSteps[0].velocity == 96);
}

TEST_CASE("Sanitise blanks off steps, clamps fields and clears what the lengths hide", "[groove]") {
    GrooveBank bank;
    Pattern& p = bank.scenes[1];
    p.bars = 3;                      // rounds down to 2
    p.lanes[0].steps[5].ratchet = 3; // off step carrying junk
    p.lanes[0].steps[6].velocity = 200;
    p.lanes[0].steps[6].ratchet = 9;
    p.lanes[0].steps[6].nudge = 90;
    p.lanes[0].steps[40].velocity = 96; // beyond 2 bars
    p.lanes[0].offsetMs = 77;
    p.lanes[0].midiNote = 200;
    p.lanes[0].length = 40; // not shorter than the pattern: follow it
    p.lanes[1].length = 7;
    p.fill.beats = 3;                  // rounds to 2
    p.fill.steps[1][4].velocity = 96;  // before the 2-beat window
    p.fill.steps[1][12].velocity = 96; // inside it
    p.fill.steps[1][12].layer = Layer::core;
    bank.slicerLength = 60;
    bank.slicerSteps[31].velocity = 96;
    bank.slicerSteps[31].layer = Layer::detail;

    sanitise(bank);

    CHECK(p.bars == 2);
    CHECK(p.lanes[0].steps[5] == Step{});
    CHECK(p.lanes[0].steps[6].velocity == 127);
    CHECK(p.lanes[0].steps[6].ratchet == 4);
    CHECK(p.lanes[0].steps[6].nudge == 24);
    CHECK_FALSE(p.lanes[0].steps[40].on());
    CHECK(p.lanes[0].offsetMs == 20);
    CHECK(p.lanes[0].midiNote == 127);
    CHECK(p.lanes[0].length == 0);
    CHECK(p.lanes[1].length == 7);
    CHECK(p.fill.beats == 2);
    CHECK_FALSE(p.fill.steps[1][4].on());
    CHECK(p.fill.steps[1][12].on());
    CHECK(p.fill.steps[1][12].layer == Layer::groove); // layer means nothing on a fill step
    CHECK(bank.slicerLength == 32);
    CHECK(bank.slicerSteps[31].layer == Layer::groove);

    bank.slicerLength = 8;
    sanitise(bank);
    CHECK_FALSE(bank.slicerSteps[10].on()); // the default gate's step 10 is now hidden

    const GrooveBank once = bank;
    sanitise(bank);
    CHECK(bank == once); // sanitising twice changes nothing
}

TEST_CASE("Growing a pattern repeats it and shrinking drops the hidden bars", "[groove]") {
    Pattern p = defaultBeat();
    p.lanes[6].length = 12;
    setPatternBars(p, 2);
    CHECK(patternSteps(p) == 32);
    CHECK(p.lanes[0].steps[16].on());
    CHECK(p.lanes[1].steps[20].on());
    CHECK(p.lanes[4].steps[30].on());
    CHECK(p.lanes[6].length == 12);

    p.lanes[0].steps[20].velocity = 96;
    setPatternBars(p, 1);
    CHECK(patternSteps(p) == 16);
    CHECK_FALSE(p.lanes[0].steps[16].on());
    CHECK_FALSE(p.lanes[0].steps[20].on());

    p.lanes[6].length = 16;
    setPatternBars(p, 1);
    CHECK(p.lanes[6].length == 0);
}

TEST_CASE("Growing a pattern keeps a looped lane's length, so its loop runs on across the new bars", "[groove]") {
    Pattern p = defaultBeat();
    p.lanes[6].length = 12;
    CHECK(storedStep(p, 6, 15) == 3);
    setPatternBars(p, 2);
    CHECK(p.lanes[6].length == 12);
    CHECK(storedStep(p, 6, 16) == 4); // bar 2 continues the loop, which played stored steps 0..3 on steps 12..15
    CHECK(storedStep(p, 6, 23) == 11);
    CHECK(storedStep(p, 6, 24) == 0);
    CHECK(storedStep(p, 6, 31) == 7);
}
