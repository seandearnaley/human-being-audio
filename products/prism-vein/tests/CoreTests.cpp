// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/params.h"
#include "pv/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>

TEST_CASE("Prism Vein parameter catalog is unique, normalized round-trips, and <= 64 automatable", "[core]") {
    std::set<std::string> keys;
    int autoCount = 0;
    for (const auto& s : pv::kParamSpecs) {
        CHECK(keys.insert(std::string(s.key)).second);
        CHECK(pv::findParam(s.key) == s.id);
        if (s.automatable)
            ++autoCount;

        const float nDef = pv::toNormalised(s, s.def);
        CHECK(nDef >= 0.0f);
        CHECK(nDef <= 1.0f);
        const float rt = pv::fromNormalised(s, nDef);
        CHECK(rt == Catch::Approx(s.def).margin(1e-3f));

        CHECK_FALSE(pv::formatValue(s, s.def).empty());
    }
    CHECK(autoCount <= 64);
    CHECK(pv::describeChordMask(pv::kDefaultChordMask) == "F m9");
}

TEST_CASE("Prism Vein Preset JSON round-trips parameters, chord mask, and 4 lane curves cleanly", "[core]") {
    pv::Preset orig;
    orig.name = "Test Neuro Prism";
    orig.category = "Neurofunk";
    orig.chordMask = 0x059Au; // Eb M9
    orig.params.set(pv::ParamId::color, 0.92f);
    orig.params.set(pv::ParamId::tame, 0.67f);
    orig.params.set(pv::ParamId::shift, -5.0f);
    orig.params.set(pv::ParamId::engineMode, 1.0f);
    orig.curves[0] = hba::dsp::curves::square();

    const std::string json = pv::serializePreset(orig);
    std::string err;
    const auto parsed = pv::parsePreset(json, &err);
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == orig.name);
    CHECK(parsed->category == orig.category);
    CHECK(parsed->chordMask == orig.chordMask);
    CHECK(parsed->params.real(pv::ParamId::color) == Catch::Approx(0.92f).margin(1e-4f));
    CHECK(parsed->params.real(pv::ParamId::tame) == Catch::Approx(0.67f).margin(1e-4f));
    CHECK(parsed->params.real(pv::ParamId::shift) == Catch::Approx(-5.0f).margin(1e-4f));
    CHECK(parsed->params.choice(pv::ParamId::engineMode) == 1);
    CHECK(parsed->curves[0] == orig.curves[0]);

    // Malformed JSON fails gracefully.
    CHECK_FALSE(pv::parsePreset("{not json", &err).has_value());
}
