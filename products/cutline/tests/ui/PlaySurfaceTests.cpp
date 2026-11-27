// SPDX-License-Identifier: AGPL-3.0-or-later
// The PLAY surface's memory without a window: each sent note is taken once and fades over one bar of its own clock.
#include "sent_history.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cl;
using namespace cl::ui;
using Catch::Approx;

namespace {

SentNote sent(std::uint32_t seq, double ppq, float rate = 1.0f, int lane = 0) {
    SentNote n;
    n.seq = seq;
    n.ppq = ppq;
    n.rate = rate;
    n.lane = static_cast<std::int8_t>(lane);
    n.velocity = 100;
    return n;
}

int liveCount(const SentHistory& history, double now) {
    int n = 0;
    history.forEachLive(now, [&](const SentNote&, float) { ++n; });
    return n;
}

} // namespace

TEST_CASE("A sent note is taken once, by its sequence number", "[play]") {
    SentHistory history;
    const std::vector<SentNote> batch{sent(1, 0.0), sent(2, 0.5)};
    history.ingest(batch);
    history.ingest(batch); // the same refresh read twice
    CHECK(liveCount(history, 1.0) == 2);
    const std::vector<SentNote> next{sent(2, 0.5), sent(3, 1.0)};
    history.ingest(next);
    CHECK(liveCount(history, 1.0) == 3);
}

TEST_CASE("A mark fades over one bar of the clock it was sent on", "[play]") {
    CHECK(SentHistory::lifeOf(sent(1, 8.0), 8.0) == Approx(1.0f));
    CHECK(SentHistory::lifeOf(sent(1, 8.0), 10.0) == Approx(0.5f));
    CHECK(SentHistory::lifeOf(sent(1, 8.0), 12.0) == Approx(0.0f));
    CHECK(SentHistory::lifeOf(sent(1, 8.0, 2.0f), 9.0) == Approx(0.5f)); // at 2x a bar is two host beats
    CHECK(SentHistory::lifeOf(sent(1, 8.0), 7.9) == Approx(1.0f));       // sent later in the block it was read with

    SentHistory history;
    const std::vector<SentNote> batch{sent(1, 0.0), sent(2, 3.0)};
    history.ingest(batch);
    CHECK(liveCount(history, 3.5) == 2);
    CHECK(liveCount(history, 4.5) == 1);
    CHECK(liveCount(history, 7.5) == 0);

    // A jump moves the marks with the clock: they keep their age.
    history.shift(-3.0);
    CHECK(liveCount(history, 1.0) == 1);
}

TEST_CASE("Every drum lane has its own surface row, kick at the bottom and closed hat on top", "[play]") {
    std::vector<bool> used(kDrumLaneCount, false);
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const int row = surfaceRowOfLane(lane);
        REQUIRE(row >= 0);
        REQUIRE(row < kDrumLaneCount);
        CHECK_FALSE(used[static_cast<std::size_t>(row)]);
        used[static_cast<std::size_t>(row)] = true;
    }
    CHECK(surfaceRowOfLane(0) == kDrumLaneCount - 1); // kick: the bottom third, as Roll's Y
    CHECK(surfaceRowOfLane(4) == 0);                  // closed hat: the top third
    // Snare in the middle third of the height.
    const float snareMid = (static_cast<float>(surfaceRowOfLane(1)) + 0.5f) / static_cast<float>(kDrumLaneCount);
    CHECK(snareMid > 1.0f / 3.0f);
    CHECK(snareMid < 2.0f / 3.0f);
}
