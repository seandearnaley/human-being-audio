// SPDX-License-Identifier: AGPL-3.0-or-later
// Vary: deterministic, bounded to the target bar, and inside the Density 50% budgets.
#include "cl/groove_json.h"
#include "cl/vary.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace cl;

namespace {

std::vector<std::pair<std::string, Pattern>> factoryPatterns() {
    std::vector<std::pair<std::string, Pattern>> patterns;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PATTERNS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        std::ostringstream text;
        text << in.rdbuf();
        auto pattern = parsePattern(text.str());
        REQUIRE(pattern.has_value());
        patterns.emplace_back(entry.path().filename().string(), *pattern);
    }
    return patterns;
}

// Note-ons of one bar at Density 50% (Core and Groove), counted from the stored steps: a ratchet counts its hits,
// a flam counts two. The second count is the non-Core snare, ghost and clap note-ons.
std::pair<int, int> budgetOf(const Pattern& p, int bar) {
    int notes = 0;
    int backbeat = 0;
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        for (int step = 0; step < kStepsPerBar; ++step) {
            const Step& s = p.lanes[static_cast<std::size_t>(lane)]
                                .steps[static_cast<std::size_t>(storedStep(p, lane, bar * kStepsPerBar + step))];
            if (!s.on() || s.layer == Layer::detail)
                continue;
            const int hits = static_cast<int>(s.ratchet) + (s.flam ? 1 : 0);
            notes += hits;
            if (lane >= 1 && lane <= 3 && s.layer != Layer::core)
                backbeat += hits;
        }
    }
    return {notes, backbeat};
}

// Stored steps that differ between two patterns.
int differences(const Pattern& a, const Pattern& b) {
    int n = 0;
    for (std::size_t lane = 0; lane < a.lanes.size(); ++lane)
        for (std::size_t step = 0; step < a.lanes[lane].steps.size(); ++step)
            n += a.lanes[lane].steps[step] == b.lanes[lane].steps[step] ? 0 : 1;
    return n;
}

bool sameLaneSettings(const DrumLane& a, const DrumLane& b) {
    return a.length == b.length && a.midiNote == b.midiNote && a.offsetMs == b.offsetMs && a.muted == b.muted &&
           a.solo == b.solo;
}

} // namespace

TEST_CASE("Vary is deterministic and returns false on an empty pattern", "[vary]") {
    Pattern empty{};
    Pattern out = defaultBeat();
    CHECK_FALSE(vary(empty, out));
    CHECK(out == empty);

    const Pattern beat = defaultBeat();
    Pattern first;
    Pattern second;
    REQUIRE(vary(beat, first));
    REQUIRE(vary(beat, second));
    CHECK(first == second);
    CHECK_FALSE(first == beat);

    // A Core-only beat can only gain Detail hits on odd steps of the hat lane (the only one of ghost, hat and
    // perc A that has steps).
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        for (int step = 0; step < kStepsPerBar; ++step) {
            const Step& was = beat.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            const Step& now = first.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            if (was == now)
                continue;
            INFO("lane " << lane << " step " << step);
            CHECK(lane == 4);
            CHECK((step & 1) == 1);
            CHECK(now.velocity == 64);
            CHECK(now.layer == Layer::detail);
        }
    }
}

TEST_CASE("Vary changes only Groove and Detail steps of the target bar", "[vary]") {
    for (const auto& [file, pattern] : factoryPatterns()) {
        INFO(file);
        Pattern varied;
        REQUIRE(vary(pattern, varied));
        CHECK(varied.bars == pattern.bars);
        CHECK(varied.fill == pattern.fill);
        // Three moves: a shift touches two stored steps, the other moves one.
        CHECK(differences(pattern, varied) >= 1);
        CHECK(differences(pattern, varied) <= 2 * 3);

        const int target = varyTargetBar(pattern);
        CHECK(target == patternBars(pattern) - 1);
        for (int lane = 0; lane < kDrumLaneCount; ++lane) {
            const auto li = static_cast<std::size_t>(lane);
            CHECK(sameLaneSettings(pattern.lanes[li], varied.lanes[li]));
            const int loop = laneLoopSteps(pattern, lane);
            for (int stored = 0; stored < kMaxPatternSteps; ++stored) {
                const Step& was = pattern.lanes[li].steps[static_cast<std::size_t>(stored)];
                const Step& now = varied.lanes[li].steps[static_cast<std::size_t>(stored)];
                if (was == now)
                    continue;
                INFO("lane " << lane << " stored step " << stored);
                CHECK_FALSE((was.on() && was.layer == Layer::core));
                CHECK_FALSE((now.on() && now.layer == Layer::core));
                // On a lane that follows the pattern, the change is inside the target bar. A looped lane's stored
                // steps repeat in every bar.
                if (loop == patternSteps(pattern))
                    CHECK(stored / kStepsPerBar == target);
                else
                    CHECK(stored < loop);
            }
        }
    }
}

TEST_CASE("The lint budgets hold for every factory pattern after 1, 2 and 10 presses", "[vary]") {
    for (const auto& [file, pattern] : factoryPatterns()) {
        Pattern current = pattern;
        for (int press = 1; press <= 10; ++press) {
            Pattern next;
            vary(current, next);
            current = next;
            if (press != 1 && press != 2 && press != 10)
                continue;
            for (int bar = 0; bar < patternBars(current); ++bar) {
                INFO(file << " press " << press << " bar " << bar + 1);
                const auto [notes, backbeat] = budgetOf(current, bar);
                CHECK(notes <= kVaryMaxBarNotes);
                CHECK(backbeat <= kVaryMaxBarBackbeatExtras);
            }
        }
        // Repeated presses walk further from the original.
        CHECK_FALSE(current == pattern);
    }
}

TEST_CASE("A looped lane varies its stored steps, and a bar already over budget is not pushed further", "[vary]") {
    Pattern p{};
    setPatternBars(p, 2);
    p.lanes[0].steps[0].velocity = 100;
    p.lanes[0].steps[0].layer = Layer::core;
    p.lanes[6].length = 5;
    p.lanes[6].steps[1].velocity = 96; // Groove, on a 5-step loop
    Pattern varied;
    REQUIRE(vary(p, varied));
    CHECK(varied.lanes[6].length == 5);
    for (int stored = 5; stored < kMaxPatternSteps; ++stored)
        CHECK_FALSE(varied.lanes[6].steps[static_cast<std::size_t>(stored)].on());

    // Every snare step a Groove flam: far over the backbeat budget. Vary may remove or move, never add more.
    Pattern loud{};
    for (int step = 0; step < kStepsPerBar; ++step) {
        Step& s = loud.lanes[1].steps[static_cast<std::size_t>(step)];
        s.velocity = 96;
        s.flam = (step & 1) == 0;
    }
    Pattern after;
    REQUIRE(vary(loud, after));
    CHECK(budgetOf(after, 0).second <= budgetOf(loud, 0).second);
}
