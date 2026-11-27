// SPDX-License-Identifier: AGPL-3.0-or-later
// Content lint for every factory pattern in products/cutline/patterns and every fill in products/cutline/fills.
#include "cl/groove_json.h"
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>
#include <hba/core/preset_json.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<std::filesystem::path> jsonFiles(const char* directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".json")
            files.push_back(entry.path());
    }
    return files;
}

bool nonEmptyString(const hba::core::Json& root, const char* key) {
    const auto it = root.find(key);
    return it != root.end() && it->is_string() && !it->get<std::string>().empty();
}

} // namespace

TEST_CASE("Every factory pattern keeps its anchor and stays inside the note budgets", "[patterns]") {
    const auto files = jsonFiles(CL_PATTERNS_DIR);
    std::set<std::string> names;
    for (const auto& path : files) {
        INFO(path.filename().string());
        const std::string text = slurp(path);

        // The pattern menus group by "category"; the pattern reader itself ignores it.
        const auto root = hba::core::Json::parse(text, nullptr, false);
        REQUIRE(root.is_object());
        CHECK(nonEmptyString(root, "category"));

        std::string error;
        std::string name;
        const auto pattern = parsePattern(text, &error, &name);
        INFO(error);
        REQUIRE(pattern.has_value());
        CHECK(!name.empty());
        CHECK(names.insert(name).second); // names are unique: presets are built by name

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

            resolveBar(*pattern, bar, {1.0f, 0.5f, false}, resolved);
            CHECK(noteOns(resolved, 0, kDrumLaneCount - 1, false, false) <= 32);
        }

        // Fill: one or two beats, at least one note, at most six per beat.
        CHECK((pattern->fill.beats == 1 || pattern->fill.beats == 2));
        ResolvedBar fill;
        resolveBar(*pattern, bars - 1, {0.5f, 0.5f, true}, fill);
        const int fillNotes = noteOns(fill, 0, kDrumLaneCount - 1, false, true);
        CHECK(fillNotes >= 1);
        CHECK(fillNotes <= 6 * static_cast<int>(pattern->fill.beats));
    }
    CHECK(files.size() == 46);
}

TEST_CASE("Every library fill loads as a pattern's fill and passes the fill lint", "[patterns]") {
    const auto files = jsonFiles(CL_FILLS_DIR);
    std::set<std::string> names;
    for (const auto& path : files) {
        INFO(path.filename().string());
        const auto root = hba::core::Json::parse(slurp(path), nullptr, false);
        REQUIRE(root.is_object());

        // Exactly {format, name, category, beats, lanes}: "beats" and "lanes" as in a pattern's "fill".
        std::set<std::string> keys;
        for (const auto& [key, value] : root.items())
            keys.insert(key);
        CHECK(keys == std::set<std::string>{"format", "name", "category", "beats", "lanes"});
        CHECK(root.value("format", 0) == kGrooveFormat);
        CHECK(nonEmptyString(root, "name"));
        CHECK(nonEmptyString(root, "category"));
        CHECK(names.insert(root.value("name", std::string{})).second);

        // The plugin loads a fill by wrapping it as a pattern's fill; it must parse that way.
        hba::core::Json wrapped = hba::core::Json::object();
        wrapped["format"] = kGrooveFormat;
        wrapped["fill"] = {{"beats", root["beats"]}, {"lanes", root["lanes"]}};
        std::string error;
        const auto pattern = parsePattern(wrapped.dump(), &error);
        INFO(error);
        REQUIRE(pattern.has_value());

        // Nothing is drawn before the window, where the reader would drop it without a word.
        const int window = fillWindowStart(pattern->fill);
        for (const auto& [lane, text] : root["lanes"].items()) {
            INFO(lane);
            REQUIRE(text.is_string());
            CHECK(text.get<std::string>().find_first_not_of('.') >= static_cast<std::size_t>(window));
        }

        // The same fill rules as a pattern's fill.
        CHECK((pattern->fill.beats == 1 || pattern->fill.beats == 2));
        ResolvedBar fill;
        resolveBar(*pattern, 0, {0.5f, 0.5f, true}, fill);
        const int fillNotes = noteOns(fill, 0, kDrumLaneCount - 1, false, true);
        CHECK(fillNotes >= 1);
        CHECK(fillNotes <= 6 * static_cast<int>(pattern->fill.beats));
    }
    CHECK(files.size() == 13);
}
