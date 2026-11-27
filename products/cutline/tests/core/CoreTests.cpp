// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>
#include <string>
#include <string_view>

using namespace cl;
using Catch::Approx;

TEST_CASE("The parameter contract has 42 unique keys that round-trip", "[core]") {
    REQUIRE(kParamCount == 42);
    REQUIRE(kParams.size() == static_cast<std::size_t>(kParamCount));

    std::set<std::string> keys;
    for (int i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        const auto& s = spec(id);
        REQUIRE(!s.key.empty());
        REQUIRE(keys.insert(std::string(s.key)).second);
        REQUIRE(findParam(s.key) == id);
        const float norm = toNormalised(s, s.def);
        CHECK(norm >= 0.0f);
        CHECK(norm <= 1.0f);
        CHECK(fromNormalised(s, norm) == Approx(s.def).margin(1e-3f));
        CHECK(!formatValue(s, s.def).empty());
    }
}

TEST_CASE("Bank 1 is Density, Swing, Dynamics, Fill, Scene, Rate, Pad X, Pad Y", "[core]") {
    const std::array<std::string_view, 8> keys{"density", "swing",   "dynamics", "fillEvery",
                                               "scene",   "seqRate", "padX",     "padY"};
    for (std::size_t i = 0; i < keys.size(); ++i)
        CHECK(kParams[i].key == keys[i]);
    CHECK(kParams[8].key == "fillNow");
    CHECK(kParams[static_cast<std::size_t>(kParamCount) - 1].key == "ccThinMs");

    const Params p;
    CHECK(p[ParamId::density] == Approx(0.5f));
    CHECK(p[ParamId::swing] == Approx(0.0f));
    CHECK(p[ParamId::dynamics] == Approx(0.5f));
    CHECK(p.choice(ParamId::fillEvery) == 0);
    CHECK(formatValue(spec(ParamId::fillEvery), 2.0f) == "4 bars");
    CHECK_FALSE(p.on(ParamId::fillNow));
    CHECK(p[ParamId::padX] == Approx(0.5f));
    CHECK(p[ParamId::padY] == Approx(0.5f));
}

TEST_CASE("Nothing sends MIDI the user did not ask for by default", "[core]") {
    const Params p;
    CHECK_FALSE(p.on(ParamId::padMidi));
    CHECK(p.choice(ParamId::padCcMode) == static_cast<int>(PadCcMode::off));
    CHECK_FALSE(p.on(ParamId::lane1On));
    CHECK_FALSE(p.on(ParamId::lane2On));
    CHECK(p.choice(ParamId::sceneQuantize) == static_cast<int>(Quantize::oneBar));
    CHECK(p.choice(ParamId::padProgram) == static_cast<int>(PadProgram::repeat));
}

TEST_CASE("The Cutline 2 generator parameters are gone", "[core]") {
    for (const char* key : {"style", "gravity", "ghosts", "chop", "ratchet", "complexity", "fills", "phraseBars",
                            "attractor", "fillType", "variation"})
        CHECK_FALSE(findParam(key).has_value());
    CHECK(kPerfPadCount == 4);
    CHECK(spec(ParamId::padProgram).choices.size() == 4);
    CHECK(kPerfPadLabels[0] == "Repeat");
    CHECK(kPerfPadLabels[3] == "Drop");
}

TEST_CASE("Kit note maps resolve built-in notes and per-lane overrides", "[core]") {
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 0, 0) == 36);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 1, 0) == 38);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 4, 0) == 42);
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 1, 0) == 36);
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 6, 0) == 39);
    CHECK(effectiveLaneMidiNote(NoteMap::chromatic, 7, 0) == 43);
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 5, 0) == 55);
    CHECK(effectiveLaneMidiNote(NoteMap::bfd3, 0, 0) == 24);
    CHECK(effectiveLaneMidiNote(NoteMap::xlnXo, 7, 0) == 43);
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 0, 51) == 51);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 0, 200) == 36); // out of range: fall back to the map

    for (int m = 0; m < kNoteMapCount; ++m) {
        for (int l = 0; l < kDrumLaneCount; ++l) {
            const int note = effectiveLaneMidiNote(static_cast<NoteMap>(m), l, 0);
            CHECK(note >= 12);
            CHECK(note <= 120);
            CHECK(!kitLaneName(static_cast<NoteMap>(m), l).empty());
        }
    }
    CHECK(midiNoteLabel(60) == "C4");
}
