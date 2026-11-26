// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/hits.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cl;
using Catch::Approx;

namespace {

Step hit(int velocity) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = Layer::core;
    return s;
}

std::vector<Hit> hitsOf(const Pattern& p, double origin, const HitTiming& timing) {
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    std::vector<Hit> hits;
    forEachHit(p, bar, origin, timing, [&](const Hit& h) { hits.push_back(h); });
    return hits;
}

} // namespace

TEST_CASE("Swing delays odd 16ths, leaves even 16ths alone and never reorders time", "[engine][hits]") {
    CHECK(swungStepBeats(2, 1.0) == 0.5);
    CHECK(swungStepBeats(1, 0.0) == 0.25);
    CHECK(swungStepBeats(1, 1.0) == Approx(0.375));
    CHECK(swungStepBeats(16, 0.7) == 4.0);
    for (double swing : {0.0, 0.33, 0.67, 1.0}) {
        double last = -1.0;
        for (int i = 0; i <= 640; ++i) {
            const double warped = swingBeat(static_cast<double>(i) * 0.025, swing);
            REQUIRE(warped > last);
            last = warped;
        }
    }
}

TEST_CASE("A fill set to every N bars fires in bars N, 2N, 3N of a ruler that starts at 1", "[engine][hits]") {
    CHECK(fillScheduled(3, 4));
    CHECK(fillScheduled(7, 4));
    CHECK_FALSE(fillScheduled(4, 4));
    CHECK_FALSE(fillScheduled(0, 4));
    CHECK(fillScheduled(1, 2));
    CHECK_FALSE(fillScheduled(3, 0));
    CHECK(kFillEveryBars[0] == 0);
    CHECK(kFillEveryBars[4] == 16);
}

TEST_CASE("Ratchets divide the step evenly and a flam adds one early, quieter hit", "[engine][hits]") {
    Pattern p{};
    p.lanes[4].steps[14] = hit(96);
    p.lanes[4].steps[14].ratchet = 2;
    p.lanes[1].steps[4] = hit(118);
    p.lanes[1].steps[4].flam = true;

    const auto hits = hitsOf(p, 8.0, {0.0, 0.6, 172.0 / 60000.0});
    REQUIRE(hits.size() == 4);
    CHECK(hits[0].grace);
    CHECK(hits[0].lane == 1);
    CHECK(hits[0].velocity == 78);
    CHECK(hits[0].beat == Approx(9.0 - 20.0 * 172.0 / 60000.0));
    CHECK(hits[1].beat == 9.0);
    CHECK(hits[1].velocity == 118);
    CHECK_FALSE(hits[1].grace);
    CHECK(hits[2].beat == 11.5);
    CHECK(hits[3].beat == Approx(11.625));
    CHECK(hits[3].durBeats < 0.125);
}

TEST_CASE("Nudge and lane offset shift a hit without moving its step", "[engine][hits]") {
    Pattern p{};
    p.lanes[0].steps[4] = hit(96);
    p.lanes[0].steps[4].nudge = 12; // 12/96 = 1/8 beat late
    p.lanes[0].offsetMs = -10;
    const double beatsPerMs = 120.0 / 60000.0;
    const auto hits = hitsOf(p, 0.0, {0.0, 0.6, beatsPerMs});
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].step == 4);
    CHECK(hits[0].beat == Approx(1.0 + 0.125 - 10.0 * beatsPerMs));
}

TEST_CASE("Swung ratchets stay inside their step", "[engine][hits]") {
    Pattern p{};
    p.lanes[4].steps[1] = hit(96);
    p.lanes[4].steps[1].ratchet = 4;
    const auto hits = hitsOf(p, 0.0, {1.0, 0.6, 0.0});
    REQUIRE(hits.size() == 4);
    CHECK(hits[0].beat == Approx(0.375));
    CHECK(hits[3].beat < 0.5);
}
