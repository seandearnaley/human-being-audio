// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
