// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using Catch::Approx;

TEST_CASE("Cutline parameter table has unique keys and round-trips normalised values", "[core]") {
    REQUIRE(cl::kParamCount <= 64);

    for (std::size_t i = 0; i < cl::kParams.size(); ++i) {
        const auto& s = cl::kParams[i];
        REQUIRE_FALSE(s.key.empty());
        REQUIRE_FALSE(s.name.empty());
        const auto found = cl::findParam(s.key);
        REQUIRE(found.has_value());
        CHECK(static_cast<std::size_t>(*found) == i);

        const float norm = cl::toNormalised(s, s.def);
        CHECK(norm >= 0.0f);
        CHECK(norm <= 1.0f);
        CHECK(cl::fromNormalised(s, norm) == Approx(s.def).margin(1e-3f));
        CHECK_FALSE(cl::formatValue(s, s.def).empty());
    }
}

TEST_CASE("Cutline presets round-trip parameters and custom curves through JSON", "[core]") {
    cl::Preset orig;
    orig.name = "Neuro Whip";
    orig.category = "Neuro";
    orig.params[cl::ParamId::grammar] = 2.0f;
    orig.params[cl::ParamId::gravity] = -0.45f;
    orig.params[cl::ParamId::seed] = 731.0f;
    orig.params[cl::ParamId::outputMode] = 1.0f;
    orig.curves[1] = cl::curves::rampDown();

    const std::string json = cl::serialisePreset(orig);
    std::string err;
    const auto parsed = cl::parsePreset(json, &err);
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == orig.name);
    CHECK(parsed->category == orig.category);
    CHECK(parsed->params[cl::ParamId::grammar] == Approx(2.0f));
    CHECK(parsed->params[cl::ParamId::gravity] == Approx(-0.45f));
    CHECK(parsed->params[cl::ParamId::seed] == Approx(731.0f));
    CHECK(parsed->params[cl::ParamId::outputMode] == Approx(1.0f));
    CHECK(parsed->curves[1] == cl::curves::rampDown());
}

TEST_CASE("Cutline presets preserve Live MIDI alias and round-trip PhraseOverrides", "[core]") {
    std::string err;
    const auto legacy = cl::parsePreset(R"({"name":"Legacy","inputMode":"Live MIDI"})", &err);
    REQUIRE(legacy.has_value());
    CHECK(legacy->params.choice(cl::ParamId::inputMode) == static_cast<int>(cl::InputMode::chordCarve));

    cl::PhraseOverrides ov;
    ov.fillStartFrac = 0.72f;
    ov.turnStartFrac = 0.88f;
    ov.spans[0].locked = true;
    ov.spans[0].lockedSeed = 407;
    ov.spans[2].roleOverride = static_cast<std::int8_t>(cl::Role::rest);
    if (auto* n = ov.findOrCreateNote(3)) {
        n->pitchDelta = 5;
        n->velocityDelta = 28;
        n->muted = true;
        n->flipBend = true;
    }

    const auto json = cl::serialiseOverrides(ov);
    const auto parsed = cl::parseOverrides(json);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == ov);
}

TEST_CASE("Cutline 1.0 PhraseOverrides round-trip NoteKey, transforms, chords, and drumStyle", "[core]") {
    cl::Preset p;
    p.name = "Phase5 Roundtrip";
    p.category = "Breaks";
    p.params[cl::ParamId::outputMode] = static_cast<float>(cl::OutputMode::drum);
    p.params[cl::ParamId::drumStyle] = static_cast<float>(cl::DrumStyle::think);

    p.overrides.barChordOffsets = {0, -2, -1, 3, 0, 0, 0, 0};
    p.overrides.spans[0].locked = true;
    p.overrides.spans[0].lockedSeed = 777;
    p.overrides.spans[1].octaveShift = 1;
    p.overrides.spans[1].invert = true;
    p.overrides.spans[2].timeScale = -1;

    const cl::NoteKey key{0, 24, 1};
    auto* n = p.overrides.findOrCreateNote(key);
    REQUIRE(n != nullptr);
    n->pitchDelta = -5;
    n->velocityDelta = 24;
    n->lengthSixteenths = 4;
    n->microNudgeTicks = -12;
    n->ratchetCount = 3;
    n->flipBend = true;

    REQUIRE(p.overrides.hasChordProgression());
    REQUIRE(p.overrides.findNoteFor(key, 99) != nullptr);
    CHECK(p.overrides.findNoteFor(key, 99)->ratchetCount == 3);

    const std::string json = cl::serialisePreset(p);
    std::string err;
    const auto parsed = cl::parsePreset(json, &err);
    REQUIRE(parsed.has_value());
    CHECK(parsed->params.choice(cl::ParamId::drumStyle) == static_cast<int>(cl::DrumStyle::think));
    CHECK(parsed->overrides == p.overrides);
}
