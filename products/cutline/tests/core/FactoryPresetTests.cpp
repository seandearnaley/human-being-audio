// SPDX-License-Identifier: AGPL-3.0-or-later
// The factory presets, checked against the factory patterns they are built from (tools/build_presets.py).
#include "cl/groove_json.h"
#include "cl/preset.h"

#include <catch2/catch_test_macros.hpp>
#include <hba/core/preset_json.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cl;

namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::map<std::string, Preset> loadPresets() {
    std::map<std::string, Preset> presets;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PRESETS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        std::string error;
        const auto preset = parsePreset(slurp(entry.path()), &error);
        INFO(entry.path().filename().string() << ": " << error);
        REQUIRE(preset.has_value());
        REQUIRE(!preset->name.empty());
        REQUIRE(presets.emplace(preset->name, *preset).second); // names are unique
    }
    return presets;
}

struct LibraryPattern {
    std::string name;
    std::string category;
    Pattern pattern;
};

std::vector<LibraryPattern> loadPatterns() {
    std::vector<LibraryPattern> patterns;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PATTERNS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        const std::string text = slurp(entry.path());
        std::string error;
        LibraryPattern p;
        const auto pattern = parsePattern(text, &error, &p.name);
        INFO(entry.path().filename().string() << ": " << error);
        REQUIRE(pattern.has_value());
        p.pattern = *pattern;
        p.category = hba::core::Json::parse(text, nullptr, false).value("category", std::string{});
        patterns.push_back(std::move(p));
    }
    return patterns;
}

// The library pattern a scene plays, or nullptr when the scene matches none.
const LibraryPattern* findPattern(const std::vector<LibraryPattern>& library, const Pattern& scene) {
    for (const auto& p : library) {
        if (p.pattern == scene)
            return &p;
    }
    return nullptr;
}

bool isSlicer(const Preset& preset) { return preset.category == "MIDI Slicer"; }

} // namespace

TEST_CASE("There are 22 factory presets and every one is a Cutline 3 preset", "[presets]") {
    const auto presets = loadPresets();
    CHECK(presets.size() == 22);
    const std::set<std::string> categories{"Drum & Bass", "Halftime",   "Jungle",      "Breaks",
                                           "Club",        "Bass Music", "Strike Body", "MIDI Slicer"};
    std::map<std::string, int> perCategory;
    for (const auto& [name, preset] : presets) {
        INFO(name);
        CHECK(categories.count(preset.category) == 1);
        ++perCategory[preset.category];
    }
    CHECK(perCategory["MIDI Slicer"] == 5);
    CHECK(perCategory["Strike Body"] == 4);
}

TEST_CASE("Every drum preset fills all four scenes from the pattern library, with Fill off", "[presets]") {
    const auto presets = loadPresets();
    const auto library = loadPatterns();
    const Params defaults;
    int drum = 0;
    for (const auto& [name, preset] : presets) {
        INFO(name);
        CHECK(preset.params.choice(ParamId::fillEvery) == 0); // fills are off by default, factory included
        CHECK(preset.params[ParamId::density] == defaults[ParamId::density]);
        CHECK(preset.bank.slicerLength == 16);
        CHECK(preset.bank.slicerSteps[0].on());
        if (isSlicer(preset))
            continue;
        ++drum;
        const bool strikeBody = preset.category == "Strike Body";
        CHECK(preset.params.choice(ParamId::noteMap) ==
              static_cast<int>(strikeBody ? NoteMap::strikeBody : NoteMap::liveRack));
        for (std::size_t scene = 0; scene < preset.bank.scenes.size(); ++scene) {
            INFO("scene " << static_cast<char>('A' + scene));
            CHECK(!(preset.bank.scenes[scene] == Pattern{}));
            const LibraryPattern* source = findPattern(library, preset.bank.scenes[scene]);
            REQUIRE(source != nullptr);
            // Strike Body presets play only the patterns written for one Strike Body voice, and only they do.
            CHECK((source->category == "Strike Body") == strikeBody);
        }
    }
    CHECK(drum == 17);
}

TEST_CASE("Every library pattern is played by a factory preset", "[presets]") {
    const auto presets = loadPresets();
    const auto library = loadPatterns();
    CHECK(library.size() == 46);
    std::set<std::string> played;
    for (const auto& [name, preset] : presets) {
        for (const auto& scene : preset.bank.scenes) {
            if (const LibraryPattern* source = findPattern(library, scene))
                played.insert(source->name);
        }
    }
    for (const auto& p : library) {
        INFO(p.name);
        CHECK(played.count(p.name) == 1);
    }
}

TEST_CASE("The slicer presets start in MIDI Slicer mode with a gate and a pattern", "[presets]") {
    const auto presets = loadPresets();
    const auto library = loadPatterns();
    for (const char* name : {"Slicer: UKG Chord Stabs", "Slicer: Liquid Strum", "Slicer: Break Arp",
                             "Slicer: Neuro Bass + Stab", "Slicer: Live Clip Re-Slicer"}) {
        INFO(name);
        const auto found = presets.find(name);
        REQUIRE(found != presets.end());
        const Preset& preset = found->second;
        CHECK(preset.category == "MIDI Slicer");
        CHECK(preset.params.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer));
        CHECK(preset.bank.slicerSteps[0].on());
        CHECK(findPattern(library, preset.bank.scenes[0]) != nullptr);
        CHECK(preset.bank.scenes[0].lanes[0].steps[0].on()); // a kick on beat 1 for Bass / Stab to follow
    }
    CHECK(presets.at("Slicer: Neuro Bass + Stab").params.choice(ParamId::slicerStyle) ==
          static_cast<int>(SlicerStyle::bassStab));
    CHECK(presets.at("Slicer: UKG Chord Stabs").params[ParamId::swing] > 0.3f);
}
