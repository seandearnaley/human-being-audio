// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/modal_models.h"
#include "sb/params.h"
#include "sb/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Strike Body parameter specs round-trip normalization and JSON", "[core]") {
    const auto& specs = sb::paramSpecs();
    REQUIRE(specs.size() == sb::kParamCount);
    REQUIRE(sb::kParamCount <= 64);

    for (const auto& s : specs) {
        REQUIRE(sb::findParam(s.key) == &s);
        const float norm = sb::toNormalised(s, s.def);
        REQUIRE(norm >= 0.0f);
        REQUIRE(norm <= 1.0f);
        const float back = sb::fromNormalised(s, norm);
        REQUIRE(back == Catch::Approx(s.def).margin(1e-3f));
        REQUIRE(!sb::formatValue(s, s.def).empty());
    }

    sb::PresetData orig;
    orig.name = "Test Neuro Snare";
    orig.category = "Snare";
    orig.params.set(sb::ParamId::bodyHz, 196.0f);
    orig.params.set(sb::ParamId::shellModel, 1.0f);
    orig.params.set(sb::ParamId::drive, 64.0f);
    orig.params.set(sb::ParamId::shellInvert, 1.0f);

    const std::string json = sb::serialisePresetJson(orig);
    const auto parsed = sb::parsePresetJson(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->name == orig.name);
    REQUIRE(parsed->category == orig.category);
    REQUIRE(parsed->params.real(sb::ParamId::bodyHz) == Catch::Approx(196.0f).margin(1e-3f));
    REQUIRE(parsed->params.choice(sb::ParamId::shellModel) == 1);
    REQUIRE(parsed->params.flag(sb::ParamId::shellInvert));
}

TEST_CASE("All 6 physical modal geometries are finite and strictly positive", "[core]") {
    for (int m = 0; m < 6; ++m) {
        const auto& g = sb::modalGeometry(static_cast<sb::ShellModel>(m));
        REQUIRE(g.ratios[0] == Catch::Approx(1.0));
        for (int k = 0; k < sb::kModeCount; ++k) {
            REQUIRE(g.ratios[static_cast<std::size_t>(k)] >= 1.0);
            REQUIRE(g.gains[static_cast<std::size_t>(k)] > 0.0);
            REQUIRE(g.radialOrder[static_cast<std::size_t>(k)] >= 0.0);
            REQUIRE(g.radialOrder[static_cast<std::size_t>(k)] <= 1.0);
        }
    }
}
