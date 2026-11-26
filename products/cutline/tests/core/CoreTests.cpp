// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/pattern.h"
#include "cl/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>

using namespace cl;
using Catch::Approx;

TEST_CASE("Cutline 2.0 parameter contract stays within 64-param Ableton limit and round-trips", "[core]") {
    static_assert(kParamCount <= 64);
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
        const float back = fromNormalised(s, norm);
        CHECK(back == Approx(s.def).margin(1e-3f));
        CHECK(!formatValue(s, s.def).empty());
    }

    // Recall & automation anchor keys preserved
    CHECK(findParam("gravity") == ParamId::gravity);
    CHECK(findParam("ratchet") == ParamId::ratchet);
}

TEST_CASE("Kit Note Maps resolve Live Rack, Strike Body, external drum VSTs, and custom notes", "[core]") {
    LaneConfig defCfg{};
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 0, defCfg) == 36);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 1, defCfg) == 38);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 4, defCfg) == 42);
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 1, defCfg) == 36); // Main
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 2, defCfg) == 37); // Ghost
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 3, defCfg) == 38); // Rim
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 6, defCfg) == 39); // Roll
    CHECK(effectiveLaneMidiNote(NoteMap::chromatic, 7, defCfg) == 43);

    // External drum VST canonical mappings
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 2, defCfg) == 43); // AD2 Snare Shallow Ghost
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 3, defCfg) == 37); // AD2 Snare Rimshot
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 4, defCfg) == 49); // AD2 HH Closed Tip
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 5, defCfg) == 55); // AD2 HH Open B
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 7, defCfg) == 77); // AD2 Crash 1

    CHECK(effectiveLaneMidiNote(NoteMap::toontrack, 3, defCfg) == 40); // Toontrack Rimshot
    CHECK(effectiveLaneMidiNote(NoteMap::toontrack, 4, defCfg) == 42); // Toontrack HH Closed

    CHECK(effectiveLaneMidiNote(NoteMap::bfd3, 0, defCfg) == 24); // BFD3 C0 Kick
    CHECK(effectiveLaneMidiNote(NoteMap::bfd3, 1, defCfg) == 26); // BFD3 D0 Snare
    CHECK(effectiveLaneMidiNote(NoteMap::bfd3, 3, defCfg) == 29); // BFD3 F0 Rimshot

    CHECK(effectiveLaneMidiNote(NoteMap::ggd, 0, defCfg) == 24);  // GGD C0 Kick
    CHECK(effectiveLaneMidiNote(NoteMap::ggd, 4, defCfg) == 43);  // GGD Closed HH Tip
    CHECK(effectiveLaneMidiNote(NoteMap::ggd, 7, defCfg) == 52);  // GGD Crash 1

    CHECK(effectiveLaneMidiNote(NoteMap::ssd5, 6, defCfg) == 45);    // SSD5 Mid Tom
    CHECK(effectiveLaneMidiNote(NoteMap::kontakt, 7, defCfg) == 51); // NI Studio/Abbey Ride Bow
    CHECK(effectiveLaneMidiNote(NoteMap::xlnXo, 4, defCfg) == 40);   // XO Closed Hat
    CHECK(effectiveLaneMidiNote(NoteMap::xlnXo, 7, defCfg) == 43);   // XO Flex 2

    // Every kit map & lane has a valid MIDI note (1..127) and non-empty role label
    for (int m = 0; m < kNoteMapCount; ++m) {
        const auto nm = static_cast<NoteMap>(m);
        for (int l = 0; l < kDrumLaneCount; ++l) {
            const int note = effectiveLaneMidiNote(nm, l, defCfg);
            CHECK(note >= 12);
            CHECK(note <= 120);
            CHECK(!kitLaneName(nm, l).empty());
        }
    }

    LaneConfig custom{};
    custom.midiNote = 51;
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 0, custom) == 51);
}

TEST_CASE("Locked lanes survive Mutate and Polymeter actions cleanly", "[core]") {
    PatternBank bank{};
    auto& sc = bank.scenes[0];
    sc.customized = true;
    sc.lanes[0].locked = true;
    sc.lanes[0].stepLength = 16;
    sc.lanes[0].steps[0].state = StepState::forceOn;
    sc.lanes[0].steps[0].velOffset = 12;

    const LaneConfig lockedBefore = sc.lanes[0];
    mutateUnlockedLanes(sc, 12345U);
    CHECK(sc.lanes[0] == lockedBefore);

    applyPolymeterToUnlocked(sc, 67890U);
    CHECK(sc.lanes[0] == lockedBefore);
    CHECK(sc.lanes[6].stepLength != 16);
    CHECK(sc.lanes[7].stepLength != 16);
}

TEST_CASE("Preset and PatternBank JSON serialization round-trips polymeters, ratchets, and scenes", "[core]") {
    Preset orig;
    orig.name = "Poly Jungle Test";
    orig.category = "Breaks";
    orig.params[ParamId::style] = static_cast<float>(DrumStyle::amen);
    orig.params[ParamId::gravity] = 0.42f;
    orig.params[ParamId::noteMap] = static_cast<float>(NoteMap::strikeBody);
    orig.bank.slicerLength = 12;
    orig.bank.slicerSteps[3].state = StepState::forceOn;
    orig.bank.slicerSteps[3].ratchet = 3;
    orig.bank.scenes[0].customized = true;
    orig.bank.scenes[0].lanes[6].stepLength = 7;
    orig.bank.scenes[0].lanes[6].euclidHits = 5;
    orig.bank.scenes[0].lanes[6].steps[2].flam = true;
    orig.bank.scenes[0].lanes[6].steps[2].velOffset = 18;
    orig.bank.scenes[0].lanes[1].locked = true;
    orig.bank.scenes[0].lanes[1].midiNote = 40;

    const std::string json = serialisePreset(orig);
    std::string err;
    const auto parsed = parsePreset(json, &err);
    INFO("parse error: " << err);
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == orig.name);
    CHECK(parsed->category == orig.category);
    CHECK(parsed->params[ParamId::gravity] == Approx(0.42f).margin(1e-3f));
    CHECK(parsed->params.choice(ParamId::noteMap) == static_cast<int>(NoteMap::strikeBody));
    CHECK(parsed->bank == orig.bank);
}
