// SPDX-License-Identifier: AGPL-3.0-or-later
// The grid's logic without a window: what each cell looks like, and what each edit does.
#include "grid_edit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>
#include <vector>

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

TEST_CASE("On a looped lane every repeat of a step edits the one stored step it repeats", "[grid]") {
    GrooveBank bank;
    Pattern& p = bank.scenes[0];
    setPatternBars(p, 2);
    p.lanes[6].length = 7;
    CHECK(grid::stepAt(p, false, 6, 9) == &p.lanes[6].steps[2]);
    CHECK(grid::stepAt(p, false, 6, 32) == nullptr);
    const Pattern& constPattern = p;
    CHECK(grid::stepAt(constPattern, false, 6, 23) == &p.lanes[6].steps[2]);

    CHECK(grid::toggleStep(p, false, 6, 23)); // column 23 repeats stored step 2
    CHECK(p.lanes[6].steps[2].velocity == 96);
    CHECK_FALSE(p.lanes[6].steps[23].on());

    const grid::View view = grid::resolveView(GrooveState(bank), 0, 1.0f, 0.5f, -1);
    for (int column = 0; column < 32; ++column) {
        INFO("column " << column);
        const bool lit = column == 2 || column == 9 || column == 16 || column == 23 || column == 30;
        const grid::Cell cell = grid::cellOf(view.bar[static_cast<std::size_t>(column / kStepsPerBar)]
                                                 .lanes[6][static_cast<std::size_t>(column % kStepsPerBar)]);
        CHECK((cell.look == grid::CellLook::groove) == lit);
    }
}

TEST_CASE("A press released in place is a click; out and back is a drag", "[grid]") {
    grid::DragGesture gesture;
    gesture.begin(10.0f, 100.0f);
    CHECK_FALSE(gesture.move(12.0f, 102.0f, 4.0f));
    CHECK_FALSE(gesture.moved());
    CHECK(gesture.axis() == grid::Axis::none);

    gesture.begin(10.0f, 100.0f);
    CHECK(gesture.move(10.0f, 60.0f, 4.0f));
    CHECK(gesture.move(10.0f, 100.0f, 4.0f)); // back where it started: still a drag
    CHECK(gesture.moved());

    gesture.begin(10.0f, 50.0f); // a new press forgets the last one
    CHECK_FALSE(gesture.moved());
}

TEST_CASE("On a lit step the first axis past 4 px decides erase or velocity", "[grid]") {
    grid::DragGesture gesture;
    // Exactly 4 px is not past the threshold.
    gesture.begin(100.0f, 100.0f);
    CHECK_FALSE(gesture.move(104.0f, 104.0f, 4.0f));

    // Horizontal first: erase, even if it later moves further vertically.
    CHECK(gesture.move(105.0f, 103.0f, 4.0f));
    CHECK(gesture.axis() == grid::Axis::horizontal);
    gesture.move(105.0f, 160.0f, 4.0f);
    CHECK(gesture.axis() == grid::Axis::horizontal);

    // Vertical first: velocity.
    gesture.begin(100.0f, 100.0f);
    CHECK(gesture.move(102.0f, 94.0f, 4.0f));
    CHECK(gesture.axis() == grid::Axis::vertical);
    gesture.move(200.0f, 94.0f, 4.0f);
    CHECK(gesture.axis() == grid::Axis::vertical);

    // Leftward counts as horizontal too.
    gesture.begin(100.0f, 100.0f);
    CHECK(gesture.move(90.0f, 101.0f, 4.0f));
    CHECK(gesture.axis() == grid::Axis::horizontal);
}

TEST_CASE("A one-row geometry has one row", "[grid]") {
    const grid::Geometry strip{100.0f, 40.0f, 20.0f, 30.0f, 32, 1};
    int row = -1;
    int step = -1;
    REQUIRE(grid::cellAt(strip, 145.0f, 69.0f, row, step));
    CHECK(row == 0);
    CHECK(step == 2);
    CHECK_FALSE(grid::cellAt(strip, 145.0f, 71.0f, row, step)); // below the strip
}

TEST_CASE("A new step takes the brush's layer and velocity; the fill view ignores the layer", "[grid]") {
    CHECK(grid::brushVelocity(Layer::core) == 112);
    CHECK(grid::brushVelocity(Layer::groove) == 96);
    CHECK(grid::brushVelocity(Layer::detail) == 64);

    Pattern p{};
    for (const Layer brush : {Layer::core, Layer::groove, Layer::detail}) {
        const int lane = static_cast<int>(brush);
        CHECK(grid::paintStep(p, false, lane, 3, brush));
        const Step& s = p.lanes[static_cast<std::size_t>(lane)].steps[3];
        CHECK(s.layer == brush);
        CHECK(static_cast<int>(s.velocity) == grid::brushVelocity(brush));
        CHECK_FALSE(grid::paintStep(p, false, lane, 3, Layer::core)); // a lit step is left alone
        CHECK(s.layer == brush);
    }
    CHECK(grid::toggleStep(p, false, 5, 0, Layer::detail));
    CHECK(p.lanes[5].steps[0].velocity == 64);

    p.fill.beats = 2;
    CHECK(grid::paintStep(p, true, 1, 12, Layer::detail));
    CHECK(p.fill.steps[1][12].velocity == grid::kFillStepVelocity);
    CHECK(p.fill.steps[1][12].layer == Layer::groove);        // fill steps have no layer: canonical Groove
    CHECK_FALSE(grid::paintStep(p, true, 1, 4, Layer::core)); // before the fill window
}

TEST_CASE("An erase drag turns off only lit steps on its lane and never lights one", "[grid]") {
    Pattern p = defaultBeat(); // hats on every even step
    const Pattern before = p;
    // The drag from step 2 to step 9 on the hat lane, as the grid runs it: erase each step it passes.
    int erased = 0;
    for (int step = 2; step <= 9; ++step)
        erased += grid::eraseStep(p, false, 4, step) ? 1 : 0;
    CHECK(erased == 4); // steps 2, 4, 6 and 8
    for (int step = 0; step < kStepsPerBar; ++step) {
        INFO("step " << step);
        const bool wasLit = before.lanes[4].steps[static_cast<std::size_t>(step)].on();
        CHECK(p.lanes[4].steps[static_cast<std::size_t>(step)].on() == (wasLit && (step < 2 || step > 9)));
    }
    for (int lane = 0; lane < kDrumLaneCount; ++lane)
        if (lane != 4)
            CHECK(p.lanes[static_cast<std::size_t>(lane)] == before.lanes[static_cast<std::size_t>(lane)]);
    CHECK_FALSE(grid::eraseStep(p, false, 4, 3)); // already off
}

TEST_CASE("Option-drag nudges 3 px per unit, clamped; Option-click flams lit steps only", "[grid]") {
    CHECK(grid::nudgeFromDrag(0, 0.0f, grid::kNudgeDragPx) == 0);
    CHECK(grid::nudgeFromDrag(0, 3.0f, grid::kNudgeDragPx) == 1);
    CHECK(grid::nudgeFromDrag(0, -30.0f, grid::kNudgeDragPx) == -10);
    CHECK(grid::nudgeFromDrag(6, 9.0f, grid::kNudgeDragPx) == 9);
    CHECK(grid::nudgeFromDrag(0, 400.0f, grid::kNudgeDragPx) == 24);
    CHECK(grid::nudgeFromDrag(0, -400.0f, grid::kNudgeDragPx) == -24);
    CHECK(grid::nudgeFromDrag(0, 6.0f, 6.0f) == 1); // the panel passes 3 design pixels, scaled

    Step s = hit(96, Layer::groove);
    CHECK(grid::setStepNudge(s, -7));
    CHECK(s.nudge == -7);
    CHECK_FALSE(grid::setStepNudge(s, -7));
    CHECK(grid::setStepNudge(s, 99));
    CHECK(s.nudge == 24);
    CHECK(grid::toggleFlam(s));
    CHECK(s.flam);
    CHECK(grid::toggleFlam(s));
    CHECK_FALSE(s.flam);

    Step off;
    CHECK_FALSE(grid::setStepNudge(off, 6));
    CHECK_FALSE(grid::toggleFlam(off));
    CHECK(off == Step{});
}

TEST_CASE("A lane pocket is set from the menu choices and reads +5ms in the sidebar", "[grid]") {
    Pattern p{};
    CHECK(grid::setLanePocket(p, 6, 5));
    CHECK(p.lanes[6].offsetMs == 5);
    CHECK_FALSE(grid::setLanePocket(p, 6, 5));
    CHECK(grid::setLanePocket(p, 6, -99));
    CHECK(p.lanes[6].offsetMs == -kMaxOffsetMs);
    CHECK_FALSE(grid::setLanePocket(p, 8, 5));
    CHECK(grid::kPocketChoicesMs == std::array<int, 5>{-10, -5, 0, 5, 10});
    CHECK(grid::pocketLabel(0).empty());
    CHECK(grid::pocketLabel(5) == "+5ms");
    CHECK(grid::pocketLabel(-10) == "-10ms");
}

TEST_CASE("Ticks sit where forEachHit puts each note-on, relative to the cell start", "[grid]") {
    // Perc A step 5 (odd, so swung): ratchet 3, flam, nudged late, on a lane 10 ms late.
    GrooveBank bank;
    Pattern& p = bank.scenes[0];
    p.lanes[6].steps[5] = hit(100, Layer::core);
    p.lanes[6].steps[5].ratchet = 3;
    p.lanes[6].steps[5].flam = true;
    p.lanes[6].steps[5].nudge = 6;
    p.lanes[6].offsetMs = 10;
    p.lanes[1].steps[0] = hit(100, Layer::core); // a plain hit on beat 1
    p.lanes[1].steps[8] = hit(100, Layer::core);
    p.lanes[1].steps[8].nudge = -12; // half a step early: out of its cell
    p.lanes[1].steps[15] = hit(100, Layer::core);
    p.lanes[1].steps[15].nudge = 24; // late, past the cell's right edge
    const GrooveState groove(bank);
    const grid::View view = grid::resolveView(groove, 0, 0.5f, 0.5f, -1);

    const HitTiming timing = grid::tickTiming(0.6f, 172.0, 1);
    CHECK(timing.swing == Approx(0.6));
    CHECK(timing.beatsPerMs == Approx(172.0 / 60000.0));
    CHECK(grid::tickTiming(0.6f, 172.0, 2).beatsPerMs == Approx(2.0 * 172.0 / 60000.0));

    grid::BarTicks ticks;
    grid::barTicks(groove.bank.scenes[0], view.bar[0], timing, ticks);

    // The same times from forEachHit itself, for perc A step 5.
    std::vector<Hit> hits;
    forEachHit(groove.bank.scenes[0], view.bar[0], 0.0, timing, [&hits](const Hit& h) {
        if (h.lane == 6 && h.step == 5)
            hits.push_back(h);
    });
    REQUIRE(hits.size() == 4); // the grace hit and three ratchet hits
    const grid::CellTicks& cell = ticks[6][5];
    REQUIRE(cell.count == 4);
    for (std::size_t i = 0; i < hits.size(); ++i) {
        INFO("hit " << i);
        const double rel = (hits[i].beat - 5 * 0.25) / 0.25;
        CHECK(cell.ticks[i].grace == hits[i].grace);
        if (rel < 0.0) {
            CHECK(cell.ticks[i].outside == -1);
            CHECK(cell.ticks[i].at == Approx(0.0f));
        } else if (rel >= 1.0) {
            CHECK(cell.ticks[i].outside == 1);
            CHECK(cell.ticks[i].at == Approx(1.0f));
        } else {
            CHECK(cell.ticks[i].outside == 0);
            CHECK(cell.ticks[i].at == Approx(rel).margin(1e-5));
        }
    }
    CHECK(cell.ticks[0].grace);                  // the flam's grace hit comes first
    CHECK(cell.ticks[0].at <= cell.ticks[1].at); // before the main hit
    CHECK(cell.ticks[1].at > 0.0f);              // swing, nudge and pocket push the first hit into the cell
    CHECK(cell.ticks[2].at > cell.ticks[1].at);
    CHECK(cell.ticks[3].at > cell.ticks[2].at);

    CHECK(ticks[1][0].count == 1); // a plain hit sits on the cell's left edge
    CHECK(ticks[1][0].ticks[0].at == Approx(0.0f));
    CHECK(ticks[1][0].ticks[0].outside == 0);
    CHECK(ticks[1][8].ticks[0].outside == -1); // early
    CHECK(ticks[1][15].ticks[0].outside == 1); // late
    CHECK(ticks[2][0].count == 0);             // nothing written: no ticks

    CHECK(grid::tickAt(-0.01, false).outside == -1);
    CHECK(grid::tickAt(0.125, false).at == Approx(0.5f));
    CHECK(grid::tickAt(0.25, false).outside == 1);
}

TEST_CASE("Outlines and silent steps have no ticks; slicer steps tick like drum steps", "[grid]") {
    GrooveBank bank;
    bank.scenes[0].lanes[4].muted = true; // the default beat's hats go silent
    const GrooveState groove(bank);
    const grid::View view = grid::resolveView(groove, 0, 0.5f, 0.5f, -1);
    grid::BarTicks ticks;
    grid::barTicks(groove.bank.scenes[0], view.bar[0], grid::tickTiming(0.0f, 120.0, 1), ticks);
    CHECK(ticks[4][0].count == 0);
    CHECK(ticks[0][0].count == 1);

    Step gate = hit(96, Layer::groove);
    gate.ratchet = 2;
    gate.flam = true; // a chord strum, not a grace hit
    const HitTiming timing = grid::tickTiming(0.5f, 120.0, 1);
    const grid::CellTicks odd = grid::slicerTicks(gate, 17, timing);
    REQUIRE(odd.count == 2);
    CHECK_FALSE(odd.ticks[0].grace);
    CHECK(odd.ticks[0].at == Approx(swungStepBeats(1, 0.5) / 0.25 - 1.0).margin(1e-5)); // swung like step 1
    gate.nudge = -12;
    const grid::CellTicks first = grid::slicerTicks(gate, 0, timing);
    CHECK(first.ticks[0].outside == 0); // the phrase starts no note before its first step
    CHECK(grid::slicerTicks(Step{}, 3, timing).count == 0);
}

TEST_CASE("The phrase strip shows the phrase that holds the playing bar", "[grid]") {
    grid::PhraseInput in;
    in.patternBars = 2;
    in.fillEvery = 4;

    // Stopped: bars 1 to 8, nothing playing.
    grid::Phrase stopped = grid::phraseOf(in);
    CHECK(stopped.length == 8);
    CHECK(stopped.start == 0);
    for (const auto& bar : stopped.bars)
        CHECK_FALSE(bar.playing);

    // The same host position at each Rate: the pattern clock runs at half, the same, or twice the host's bars.
    const double ppq = 4.0 * 10.5; // the middle of host bar 11
    for (const auto& [rate, playing] :
         std::array<std::pair<double, std::int64_t>, 3>{{{0.5, 5}, {1.0, 10}, {2.0, 21}}}) {
        INFO("rate " << rate);
        in.playing = true;
        in.absBar = grid::phraseBarAt(ppq, rate);
        CHECK(in.absBar == playing);
        const grid::Phrase phrase = grid::phraseOf(in);
        CHECK(phrase.start == (playing / 8) * 8);
        for (int i = 0; i < phrase.length; ++i) {
            const grid::PhraseBar& bar = phrase.bars[static_cast<std::size_t>(i)];
            CHECK(bar.absBar == phrase.start + i);
            CHECK(bar.patternBar == static_cast<int>(bar.absBar % 2));
            CHECK(bar.playing == (bar.absBar == playing));
            CHECK(bar.fill == ((bar.absBar + 1) % 4 == 0)); // bars 4, 8, 12... of the ruler
        }
    }

    // Every Fill choice: Off never fills; 16 shows 16 bars.
    in.absBar = 10;
    for (const int every : kFillEveryBars) {
        INFO("fill every " << every);
        in.fillEvery = every;
        const grid::Phrase phrase = grid::phraseOf(in);
        CHECK(phrase.length == (every == 16 ? 16 : 8));
        for (int i = 0; i < phrase.length; ++i) {
            const grid::PhraseBar& bar = phrase.bars[static_cast<std::size_t>(i)];
            CHECK(bar.fill == (every > 0 && (bar.absBar + 1) % every == 0));
        }
    }

    // Fill Now: every bar from the playing one on.
    in.fillEvery = 0;
    in.fillNow = true;
    const grid::Phrase now = grid::phraseOf(in);
    for (int i = 0; i < now.length; ++i)
        CHECK(now.bars[static_cast<std::size_t>(i)].fill == (now.bars[static_cast<std::size_t>(i)].absBar >= 10));

    // A pending 1 Bar scene switch marks the next bar; other quantizes mark nothing.
    in.fillNow = false;
    in.pendingScene = 2;
    in.pendingOnBar = true;
    const grid::Phrase pending = grid::phraseOf(in);
    for (int i = 0; i < pending.length; ++i) {
        const grid::PhraseBar& bar = pending.bars[static_cast<std::size_t>(i)];
        CHECK(bar.pendingScene == (bar.absBar == 11 ? 2 : -1));
    }
    in.pendingOnBar = false;
    for (const auto& bar : grid::phraseOf(in).bars)
        CHECK(bar.pendingScene == -1);
}
