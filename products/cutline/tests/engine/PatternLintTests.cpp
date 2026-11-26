// SPDX-License-Identifier: AGPL-3.0-or-later
// Content lint for every factory pattern in products/cutline/patterns.
#include "cl/groove_json.h"
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace cl;

namespace {

// Note-ons in a resolved bar: ratchet hits and flams count.
int noteOns(const ResolvedBar& bar, int laneFrom, int laneTo, bool nonCoreOnly, bool fillOnly) {
    int n = 0;
    for (int lane = laneFrom; lane <= laneTo; ++lane) {
        for (const auto& s : bar.lanes[static_cast<std::size_t>(lane)]) {
            if (s.velocity == 0)
                continue;
            if (nonCoreOnly && (s.fromFill || s.layer == Layer::core))
                continue;
            if (fillOnly && !s.fromFill)
                continue;
            n += static_cast<int>(s.ratchet) + (s.flam ? 1 : 0);
        }
    }
    return n;
}

bool isCore(const Step& s) { return s.on() && s.layer == Layer::core; }

} // namespace

TEST_CASE("Every factory pattern keeps its anchor and stays inside the note budgets", "[patterns]") {
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PATTERNS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        ++files;
        const std::string file = entry.path().filename().string();
        INFO(file);

        std::ifstream in(entry.path());
        std::ostringstream text;
        text << in.rdbuf();
        std::string error;
        const auto pattern = parsePattern(text.str(), &error);
        INFO(error);
        REQUIRE(pattern.has_value());

        const int bars = patternBars(*pattern);
        for (int bar = 0; bar < bars; ++bar) {
            INFO("bar " << bar + 1);
            const auto origin = static_cast<std::size_t>(bar * kStepsPerBar);

            // Anchor: kick on beat 1 of every bar, and the same Core snare in every bar.
            CHECK(isCore(pattern->lanes[0].steps[origin]));
            bool snareCore = false;
            for (std::size_t step = 0; step < static_cast<std::size_t>(kStepsPerBar); ++step) {
                const bool first = isCore(pattern->lanes[1].steps[step]);
                CHECK(first == isCore(pattern->lanes[1].steps[origin + step]));
                snareCore = snareCore || first;
            }
            CHECK(snareCore);

            ResolvedBar resolved;
            resolveBar(*pattern, bar, {0.0f, 0.5f, false}, resolved);
            CHECK(noteOns(resolved, 0, kDrumLaneCount - 1, false, false) <= 16);

            resolveBar(*pattern, bar, {0.5f, 0.5f, false}, resolved);
            CHECK(noteOns(resolved, 0, kDrumLaneCount - 1, false, false) <= 24);
            CHECK(noteOns(resolved, 1, 3, true, false) <= 3); // snare, ghost, clap off the anchor
        }

        // Fill: one or two beats, at least one note, at most six per beat.
        CHECK((pattern->fill.beats == 1 || pattern->fill.beats == 2));
        ResolvedBar fill;
        resolveBar(*pattern, bars - 1, {0.5f, 0.5f, true}, fill);
        const int fillNotes = noteOns(fill, 0, kDrumLaneCount - 1, false, true);
        CHECK(fillNotes >= 1);
        CHECK(fillNotes <= 6 * static_cast<int>(pattern->fill.beats));
    }
    CHECK(files == 11);
}
