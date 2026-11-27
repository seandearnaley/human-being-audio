// SPDX-License-Identifier: AGPL-3.0-or-later
// The grid's logic without a window: what each cell looks like, and what each edit does.
#include "grid_edit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cl;
using namespace cl::ui;
using Catch::Approx;

namespace {

Step hit(int velocity, Layer layer) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
    return s;
}

// Scene B: two bars, one step in each layer on the hat lane, and a one-beat snare fill.
GrooveState layered() {
    GrooveBank bank;
    Pattern& p = bank.scenes[1];
    p.bars = 2;
    p.lanes[0].steps[0] = hit(118, Layer::core);
    p.lanes[4].steps[2] = hit(96, Layer::groove);
    p.lanes[4].steps[3] = hit(56, Layer::detail);
    p.lanes[4].steps[18] = hit(96, Layer::core);
    p.lanes[4].steps[18].ratchet = 3;
    p.lanes[4].steps[18].flam = true;
    p.fill.beats = 1;
    p.fill.steps[1][14] = hit(118, Layer::groove);
    return GrooveState(bank);
}

} // namespace

TEST_CASE("The view resolves every bar of the scene with the engine's maps", "[grid]") {
    const GrooveState groove = layered();
    const grid::View view = grid::resolveView(groove, 1, 0.5f, 0.5f, -1);
    REQUIRE(view.bars == 2);
    CHECK(grid::cellOf(view.bar[0].lanes[0][0]).look == grid::CellLook::core);
    CHECK(grid::cellOf(view.bar[0].lanes[0][0]).velocity == 118);
    CHECK(grid::cellOf(view.bar[0].lanes[4][2]).look == grid::CellLook::groove);
    CHECK(grid::cellOf(view.bar[0].lanes[4][3]).look == grid::CellLook::outline); // Detail is silent at 50%
    CHECK(grid::cellOf(view.bar[0].lanes[4][5]).look == grid::CellLook::empty);
    const grid::Cell ratcheted = grid::cellOf(view.bar[1].lanes[4][2]);
    CHECK(ratcheted.look == grid::CellLook::core);
    CHECK(ratcheted.ratchet == 3);
    CHECK(ratcheted.flam);

    const grid::View full = grid::resolveView(groove, 1, 1.0f, 0.5f, -1);
    CHECK(grid::cellOf(full.bar[0].lanes[4][3]).look == grid::CellLook::detail);

    const grid::View defaults = grid::resolveView(GrooveState(), 0, 0.5f, 0.5f, -1);
    CHECK(defaults.bars == 1);
    CHECK(grid::cellOf(defaults.bar[0].lanes[1][4]).look == grid::CellLook::core);
}

TEST_CASE("A live fill shows its steps in the fill look, in its own bar only", "[grid]") {
    const GrooveState groove = layered();
    const grid::View idle = grid::resolveView(groove, 1, 0.5f, 0.5f, -1);
    CHECK(grid::cellOf(idle.bar[1].lanes[1][14]).look == grid::CellLook::empty);

    const grid::View live = grid::resolveView(groove, 1, 0.5f, 0.5f, 1);
    CHECK(grid::cellOf(live.bar[1].lanes[1][14]).look == grid::CellLook::fill);
    CHECK(grid::cellOf(live.bar[0].lanes[1][14]).look == grid::CellLook::empty);

    const ResolvedBar fillBar = grid::resolveFillView(groove, 1, 0.5f, 0.5f);
    CHECK(grid::cellOf(fillBar.lanes[1][14]).look == grid::CellLook::fill);
    CHECK(grid::cellOf(fillBar.lanes[4][2]).look == grid::CellLook::core); // the last bar's own hat shows through

    // The fill editor lights only fill steps. A main-pattern step that shows through is an outline.
    CHECK(grid::fillCellOf(fillBar.lanes[1][14]).look == grid::CellLook::fill);
    CHECK(grid::fillCellOf(fillBar.lanes[1][14]).velocity == 118);
    CHECK(grid::fillCellOf(fillBar.lanes[4][2]).look == grid::CellLook::outline);
    CHECK(grid::fillCellOf(fillBar.lanes[4][2]).velocity == 0);
    CHECK(grid::fillCellOf(fillBar.lanes[4][5]).look == grid::CellLook::empty);
}

TEST_CASE("A muted lane's steps are outlines, and the three layers have three heights", "[grid]") {
    GrooveBank bank;
    bank.scenes[0].lanes[4].muted = true;
    const grid::View view = grid::resolveView(GrooveState(bank), 0, 0.5f, 0.5f, -1);
    CHECK(grid::cellOf(view.bar[0].lanes[4][0]).look == grid::CellLook::outline);
    CHECK(grid::cellOf(view.bar[0].lanes[0][0]).look == grid::CellLook::core);

    CHECK(grid::heightFraction(Layer::core) == Approx(1.0f));
    CHECK(grid::heightFraction(Layer::groove) < grid::heightFraction(Layer::core));
    CHECK(grid::heightFraction(Layer::detail) < grid::heightFraction(Layer::groove));
}

TEST_CASE("A point maps to its lane and step at 16, 32 and 64 columns", "[grid]") {
    for (const int steps : {16, 32, 64}) {
        const grid::Geometry g{100.0f, 40.0f, 640.0f / static_cast<float>(steps), 20.0f, steps};
        int lane = -1;
        int step = -1;
        INFO("steps " << steps);
        REQUIRE(grid::cellAt(g, 100.0f, 40.0f, lane, step));
        CHECK(lane == 0);
        CHECK(step == 0);
        REQUIRE(grid::cellAt(g, 100.0f + 639.0f, 40.0f + 159.0f, lane, step));
        CHECK(lane == 7);
        CHECK(step == steps - 1);
        REQUIRE(grid::cellAt(g, 100.0f + 320.5f, 40.0f + 61.0f, lane, step)); // just past the middle, fourth row
        CHECK(lane == 3);
        CHECK(step == steps / 2);
        CHECK_FALSE(grid::cellAt(g, 99.0f, 50.0f, lane, step));   // left of the grid: the sidebar
        CHECK_FALSE(grid::cellAt(g, 741.0f, 50.0f, lane, step));  // right of the last column
        CHECK_FALSE(grid::cellAt(g, 200.0f, 39.0f, lane, step));  // above the first lane
        CHECK_FALSE(grid::cellAt(g, 200.0f, 201.0f, lane, step)); // below the last lane
    }
}

TEST_CASE("Toggling a step writes a Groove step at velocity 96, or clears it", "[grid]") {
    Pattern p = defaultBeat();
    CHECK(grid::toggleStep(p, false, 2, 5));
    CHECK(p.lanes[2].steps[5].velocity == 96);
    CHECK(p.lanes[2].steps[5].layer == Layer::groove);
    CHECK(grid::toggleStep(p, false, 2, 5));
    CHECK(p.lanes[2].steps[5] == Step{});
    CHECK(grid::toggleStep(p, false, 0, 0)); // a Core step toggles off like any other
    CHECK_FALSE(p.lanes[0].steps[0].on());

    CHECK_FALSE(grid::toggleStep(p, false, 0, 16)); // beyond a one-bar pattern
    CHECK_FALSE(grid::toggleStep(p, false, 8, 0));
    CHECK(grid::stepAt(p, false, 0, 16) == nullptr);
}

TEST_CASE("The fill view edits only the fill window", "[grid]") {
    Pattern p = defaultBeat();
    p.fill.beats = 2;
    CHECK_FALSE(grid::toggleStep(p, true, 1, 4)); // before the window
    CHECK(grid::stepAt(p, true, 1, 4) == nullptr);
    CHECK(grid::toggleStep(p, true, 1, 12));
    CHECK(p.fill.steps[1][12].velocity == 96);
    CHECK(p.lanes[1].steps[12].on()); // the main pattern is untouched

    CHECK(grid::setFillBeats(p, 1));
    CHECK(p.fill.beats == 1);
    CHECK(grid::toggleStep(p, true, 1, 9) == false); // now outside the one-beat window
    p.fill.steps[1][13].velocity = 96;
    CHECK(grid::setFillBeats(p, 4));
    CHECK(p.fill.steps[1][13].on());
    CHECK_FALSE(grid::setFillBeats(p, 4)); // no change
    CHECK_FALSE(grid::setFillBeats(p, 3)); // only 1, 2 or 4
}

TEST_CASE("Velocity, ratchet and layer edits apply to lit steps only", "[grid]") {
    Step off;
    CHECK_FALSE(grid::setStepVelocity(off, 100));
    CHECK_FALSE(grid::cycleRatchet(off));
    CHECK_FALSE(grid::setStepLayer(off, Layer::core));
    CHECK(off == Step{});

    Step s = hit(96, Layer::groove);
    CHECK(grid::setStepVelocity(s, 300));
    CHECK(s.velocity == 127);
    CHECK(grid::setStepVelocity(s, -5));
    CHECK(s.velocity == 1); // dragging down never switches a step off
    CHECK_FALSE(grid::setStepVelocity(s, 1));

    CHECK(grid::cycleRatchet(s));
    CHECK(s.ratchet == 2);
    grid::cycleRatchet(s);
    grid::cycleRatchet(s);
    CHECK(s.ratchet == 4);
    grid::cycleRatchet(s);
    CHECK(s.ratchet == 1);

    CHECK(grid::setStepLayer(s, Layer::core));
    CHECK(s.layer == Layer::core);
    CHECK_FALSE(grid::setStepLayer(s, Layer::core));
}

TEST_CASE("A lane length equal to the pattern means follow the pattern", "[grid]") {
    Pattern p = defaultBeat();
    CHECK(grid::setLaneLength(p, 6, 7));
    CHECK(p.lanes[6].length == 7);
    CHECK(grid::setLaneLength(p, 6, 16));
    CHECK(p.lanes[6].length == 0);
    CHECK(grid::setLaneLength(p, 6, 0) == false);
    CHECK(grid::setLaneLength(p, 6, 99) == false); // clamps to the pattern: still follow
    CHECK(p.lanes[6].length == 0);
}
