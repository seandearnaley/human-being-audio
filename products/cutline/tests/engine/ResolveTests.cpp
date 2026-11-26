// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("A live fill replaces every lane inside its window and ignores Density", "[engine][resolve]") {
    Pattern p = layered();
    p.lanes[4].steps[14] = hit(96, Layer::core);
    p.fill.beats = 1;
    p.fill.steps[1][14] = hit(118, Layer::groove);
    ResolvedBar bar;

    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(bar.lanes[4][14].velocity > 0);
    CHECK_FALSE(bar.lanes[1][14].written);

    resolveBar(p, 0, {0.0f, 0.5f, true}, bar);
    CHECK(bar.lanes[1][14].velocity == 118);
    CHECK(bar.lanes[1][14].fromFill);
    CHECK_FALSE(bar.lanes[4][14].written);
    CHECK(bar.lanes[0][0].velocity > 0); // outside the window the main pattern plays
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
