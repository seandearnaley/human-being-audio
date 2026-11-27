// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

using namespace cl;

namespace {

Step hit(int velocity, Layer layer) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
    return s;
}

int sounding(const ResolvedBar& bar) {
    int n = 0;
    for (const auto& lane : bar.lanes) {
        for (const auto& s : lane)
            n += s.velocity > 0 ? 1 : 0;
    }
    return n;
}

// One Core kick, two Groove hits, two Detail hits.
Pattern layered() {
    Pattern p{};
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[0].steps[10] = hit(96, Layer::groove);
    p.lanes[4].steps[2] = hit(96, Layer::groove);
    p.lanes[4].steps[3] = hit(96, Layer::detail);
    p.lanes[2].steps[7] = hit(56, Layer::detail);
    return p;
}

} // namespace

TEST_CASE("Density brings Groove in up to 50% and Detail in up to 100%", "[engine][resolve]") {
    const Pattern p = layered();
    ResolvedBar bar;

    resolveBar(p, 0, {0.0f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 1);
    CHECK(bar.lanes[0][0].velocity > 0);
    CHECK(bar.lanes[0][10].written);
    CHECK(bar.lanes[0][10].velocity == 0);

    resolveBar(p, 0, {0.25f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 2);
    CHECK(bar.lanes[0][10].velocity > 0); // kick outranks hat at the same metric weight

    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 3);
    resolveBar(p, 0, {0.75f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 4);
    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 5);
}

TEST_CASE("Raising Density never removes a hit and Core always sounds", "[engine][resolve]") {
    const Pattern p = layered();
    ResolvedBar bar;
    int previous = 0;
    for (int d = 0; d <= 100; ++d) {
        resolveBar(p, 0, {static_cast<float>(d) / 100.0f, 0.5f, false}, bar);
        const int now = sounding(bar);
        CHECK(now >= previous);
        CHECK(bar.lanes[0][0].velocity > 0);
        previous = now;
    }
    CHECK(previous == 5);
}

TEST_CASE("Hits rank by beat strength, then lane priority, then position", "[engine][resolve]") {
    CHECK(rankBefore(0, 0, 0, 8));       // downbeat before beat 3
    CHECK(rankBefore(4, 8, 0, 4));       // beat 3 before beat 2, whatever the lane
    CHECK(rankBefore(0, 4, 1, 4));       // kick before snare
    CHECK(rankBefore(4, 2, 2, 2));       // closed hat before ghost
    CHECK(rankBefore(0, 2, 0, 18));      // bar 1 before bar 2
    CHECK_FALSE(rankBefore(0, 3, 7, 2)); // an odd 16th never outranks an even one
}

TEST_CASE("Dynamics scales velocity contrast around 64 and is the identity at 50%", "[engine][resolve]") {
    CHECK(applyDynamics(96, 0.5f) == 96);
    CHECK(applyDynamics(96, 0.0f) == 80);
    CHECK(applyDynamics(96, 1.0f) == 112);
    CHECK(applyDynamics(120, 1.0f) == 127);
    CHECK(applyDynamics(1, 1.0f) == 1);
}

TEST_CASE("A live fill replaces only the lanes drawn in it and ignores Density", "[engine][resolve]") {
    Pattern p = layered();
    p.lanes[4].steps[14] = hit(96, Layer::core);
    p.lanes[1].steps[13] = hit(96, Layer::core);
    p.fill.beats = 1;
    p.fill.steps[1][14] = hit(118, Layer::groove);
    ResolvedBar bar;

    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(bar.lanes[4][14].velocity > 0);
    CHECK(bar.lanes[1][13].velocity > 0);
    CHECK_FALSE(bar.lanes[1][14].written);

    resolveBar(p, 0, {0.0f, 0.5f, true}, bar);
    CHECK(bar.lanes[1][14].velocity == 118); // the drawn lane takes the fill, whatever the Density
    CHECK(bar.lanes[1][14].fromFill);
    CHECK_FALSE(bar.lanes[1][13].written); // and loses its own steps inside the window
    CHECK(bar.lanes[4][14].velocity > 0);  // a lane with nothing drawn in the fill keeps playing
    CHECK_FALSE(bar.lanes[4][14].fromFill);
    CHECK(bar.lanes[0][0].velocity > 0); // outside the window the main pattern plays
}

TEST_CASE("An empty fill changes nothing, and steps drawn before the window do not count", "[engine][resolve]") {
    Pattern p = layered();
    p.lanes[4].steps[14] = hit(96, Layer::core);
    p.fill.beats = 1;
    p.fill.steps[4][2] = hit(96, Layer::groove); // before the 1-beat window
    ResolvedBar plain;
    ResolvedBar filled;
    resolveBar(p, 0, {1.0f, 0.5f, false}, plain);
    resolveBar(p, 0, {1.0f, 0.5f, true}, filled);
    for (std::size_t lane = 0; lane < plain.lanes.size(); ++lane) {
        for (std::size_t step = 0; step < plain.lanes[lane].size(); ++step) {
            CHECK(filled.lanes[lane][step].velocity == plain.lanes[lane][step].velocity);
            CHECK_FALSE(filled.lanes[lane][step].fromFill);
        }
    }
}

TEST_CASE("Muted and non-soloed lanes stay written but silent", "[engine][resolve]") {
    Pattern p = layered();
    ResolvedBar bar;

    p.lanes[0].muted = true;
    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].written);
    CHECK(bar.lanes[0][0].velocity == 0);

    p.lanes[0].muted = false;
    p.lanes[4].solo = true;
    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 0);
    CHECK(bar.lanes[4][2].velocity > 0);
}

TEST_CASE("A lane with a shorter length loops across the bar", "[engine][resolve]") {
    Pattern p{};
    p.lanes[6].length = 3;
    p.lanes[6].steps[0] = hit(96, Layer::core);
    ResolvedBar bar;
    resolveBar(p, 0, {0.0f, 0.5f, false}, bar);
    for (int step = 0; step < kStepsPerBar; ++step)
        CHECK((bar.lanes[6][static_cast<std::size_t>(step)].velocity > 0) == (step % 3 == 0));
}

TEST_CASE("The cached density map gives the same result as the convenience overload", "[engine][resolve]") {
    const Pattern p = layered();
    DensityMap map;
    buildDensityMap(p, map);
    CHECK(map.onAt[0][0] == 0.0f);
    CHECK(map.onAt[0][1] == 2.0f);
    ResolvedBar a;
    ResolvedBar b;
    for (int d = 0; d <= 10; ++d) {
        const ResolveInput in{static_cast<float>(d) / 10.0f, 0.5f, false};
        resolveBar(p, 0, in, a);
        resolveBar(p, map, 0, in, b);
        CHECK(sounding(a) == sounding(b));
    }
}

TEST_CASE("Bar 2 of a pattern resolves its own steps, and a negative bar wraps", "[engine][resolve]") {
    Pattern p{};
    p.bars = 2;
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[1].steps[20] = hit(96, Layer::core); // bar 2, step 4
    ResolvedBar bar;
    resolveBar(p, 1, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 0);
    CHECK(bar.lanes[1][4].velocity == 96);
    resolveBar(p, -1, {0.5f, 0.5f, false}, bar); // the bar before bar 0 is the last bar
    CHECK(bar.lanes[1][4].velocity == 96);
    resolveBar(p, -2, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 96);
}

TEST_CASE("A looped lane keeps its phase across the bar line", "[engine][resolve]") {
    Pattern p{};
    p.bars = 2;
    p.lanes[6].length = 5;
    p.lanes[6].steps[0] = hit(96, Layer::core);
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    for (int step = 0; step < kStepsPerBar; ++step)
        CHECK((bar.lanes[6][static_cast<std::size_t>(step)].velocity > 0) == (step % 5 == 0));
    resolveBar(p, 1, {0.5f, 0.5f, false}, bar);
    for (int step = 0; step < kStepsPerBar; ++step)
        CHECK((bar.lanes[6][static_cast<std::size_t>(step)].velocity > 0) == ((16 + step) % 5 == 0));
}

TEST_CASE("At one metric weight the lanes arrive kick, snare, hat, open hat, clap, ghost, perc A, perc B",
          "[engine][resolve]") {
    const std::array<int, kDrumLaneCount> order{0, 1, 4, 5, 3, 2, 6, 7};
    for (std::size_t i = 0; i + 1 < order.size(); ++i) {
        CHECK(rankBefore(order[i], 4, order[i + 1], 4));
        CHECK_FALSE(rankBefore(order[i + 1], 4, order[i], 4));
    }
    Pattern p{};
    for (int lane = 0; lane < kDrumLaneCount; ++lane)
        p.lanes[static_cast<std::size_t>(lane)].steps[4] = hit(96, Layer::groove);
    ResolvedBar bar;
    for (std::size_t n = 1; n <= order.size(); ++n) {
        resolveBar(p, 0, {0.5f * static_cast<float>(n) / 8.0f, 0.5f, false}, bar);
        for (std::size_t i = 0; i < order.size(); ++i)
            CHECK((bar.lanes[static_cast<std::size_t>(order[i])][4].velocity > 0) == (i < n));
    }
}

TEST_CASE("A lane that is both soloed and muted is silent, and mute silences fill steps", "[engine][resolve]") {
    Pattern p{};
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[1].steps[4] = hit(96, Layer::core);
    p.lanes[0].solo = true;
    p.lanes[0].muted = true;
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].written);
    CHECK(bar.lanes[0][0].velocity == 0);
    CHECK(bar.lanes[1][4].velocity == 0); // another lane is soloed

    Pattern f{};
    f.fill.beats = 1;
    f.fill.steps[1][12] = hit(96, Layer::groove);
    f.lanes[1].muted = true;
    resolveBar(f, 0, {0.5f, 0.5f, true}, bar);
    CHECK(bar.lanes[1][12].written);
    CHECK(bar.lanes[1][12].fromFill);
    CHECK(bar.lanes[1][12].velocity == 0);
}

TEST_CASE("Resolve clamps ratchet and nudge and survives NaN controls", "[engine][resolve]") {
    Pattern p{};
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[0].steps[0].ratchet = 9;
    p.lanes[0].steps[0].nudge = 100;
    p.lanes[4].steps[2] = hit(96, Layer::groove);
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].ratchet == 4);
    CHECK(bar.lanes[0][0].nudge == 24);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    resolveBar(p, 0, {nan, nan, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 96); // Core sounds, at the identity Dynamics
    CHECK(bar.lanes[4][2].written);
    CHECK(bar.lanes[4][2].velocity == 0); // a NaN Density is 0%: Core only
    CHECK(applyDynamics(96, nan) == 96);
}
