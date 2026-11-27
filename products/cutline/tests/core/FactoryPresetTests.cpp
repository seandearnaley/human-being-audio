// SPDX-License-Identifier: AGPL-3.0-or-later
// The factory presets, checked against the factory patterns they are built from.
#include "cl/groove_json.h"
#include "cl/preset.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

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

} // namespace

TEST_CASE("There are 16 factory presets and every one is a Cutline 3 preset", "[presets]") {
    const auto presets = loadPresets();
    CHECK(presets.size() == 16);
    int slicer = 0;
    for (const auto& [name, preset] : presets) {
        INFO(name);
        CHECK((preset.category == "Breaks" || preset.category == "Neuro" || preset.category == "Club" ||
               preset.category == "MIDI Slicer"));
        slicer += preset.category == "MIDI Slicer" ? 1 : 0;
    }
    CHECK(slicer == 5);
}

TEST_CASE("Every factory pattern has a preset that plays it in scene A with default parameters", "[presets]") {
    const auto presets = loadPresets();
    const Params defaults;
    int patterns = 0;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PATTERNS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        ++patterns;
        std::string error;
        std::string name;
        const auto pattern = parsePattern(slurp(entry.path()), &error, &name);
        INFO(entry.path().filename().string() << ": " << error);
        REQUIRE(pattern.has_value());
        const auto found = presets.find(name);
        REQUIRE(found != presets.end());
        const Preset& preset = found->second;
        CHECK(preset.bank.scenes[0] == *pattern);
        for (std::size_t scene = 1; scene < preset.bank.scenes.size(); ++scene)
            CHECK(preset.bank.scenes[scene] == Pattern{});
        CHECK(preset.params.values == defaults.values);
        CHECK(preset.bank.slicerLength == 16);
        CHECK(preset.bank.slicerSteps[0].on());
    }
    CHECK(patterns == 11);
}

TEST_CASE("The slicer presets start in MIDI Slicer mode with a gate and a pattern", "[presets]") {
    const auto presets = loadPresets();
    for (const char* name : {"Slicer: UKG Chord Stabs", "Slicer: Liquid Strum", "Slicer: Break Arp",
                             "Slicer: Neuro Bass + Stab", "Slicer: Live Clip Re-Slicer"}) {
        INFO(name);
        const auto found = presets.find(name);
        REQUIRE(found != presets.end());
        const Preset& preset = found->second;
        CHECK(preset.params.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer));
        CHECK(preset.bank.slicerSteps[0].on());
        CHECK(preset.bank.scenes[0].lanes[0].steps[0].on()); // a kick on beat 1 for Bass / Stab to follow
    }
    CHECK(presets.at("Slicer: Neuro Bass + Stab").params.choice(ParamId::slicerStyle) ==
          static_cast<int>(SlicerStyle::bassStab));
    CHECK(presets.at("Slicer: UKG Chord Stabs").params[ParamId::swing] > 0.3f);
}
