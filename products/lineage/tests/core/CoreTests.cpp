// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ln/lanes.h"
#include "ln/params.h"
#include "ln/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Lineage parameter table maps, clamps, and formats cleanly", "[core]") {
    for (int i = 0; i < ln::kParamCount; ++i) {
        const auto id = static_cast<ln::ParamId>(i);
        const auto& s = ln::spec(id);
        REQUIRE(ln::findParam(s.key) == id);
        const float norm = ln::toNormalised(s, s.def);
        REQUIRE(norm >= 0.0f);
        REQUIRE(norm <= 1.0f);
        const float back = ln::fromNormalised(s, norm);
        REQUIRE(back == Catch::Approx(s.def).margin(1e-3f));
        REQUIRE(!ln::formatValue(s, s.def).empty());
    }
    CHECK(ln::formatValue(ln::spec(ln::ParamId::filt1Hz), 1850.0f) == "1.85 kHz");
    CHECK(ln::formatValue(ln::spec(ln::ParamId::subSplit), 105.0f) == "105 Hz");
    CHECK(ln::formatValue(ln::spec(ln::ParamId::lane1Hz), 0.50f) == "0.50 Hz");
}

TEST_CASE("Lineage preset JSON round-trips parameters, custom curves, and autoPrint", "[core]") {
    ln::Preset p;
    p.name = "Molten Strata";
    p.category = "Strata";
    p.params[ln::ParamId::coreMode] = static_cast<float>(ln::CoreMode::softSync);
    p.params[ln::ParamId::genMix] = 0.72f;
    p.params[ln::ParamId::lane1Mode] = static_cast<float>(ln::LaneMode::freeRun);
    p.params[ln::ParamId::lane1Hz] = 0.65f;
    p.params[ln::ParamId::lane1Shape] = static_cast<float>(ln::LaneShape::custom);
    p.curves[0] = ln::curves::triangle();
    p.autoPrint = true;

    const auto text = ln::serialisePreset(p);
    std::string err;
    const auto loaded = ln::parsePreset(text, &err);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "Molten Strata");
    CHECK(loaded->autoPrint);
    CHECK(loaded->params.choice(ln::ParamId::coreMode) == static_cast<int>(ln::CoreMode::softSync));
    CHECK(loaded->params[ln::ParamId::genMix] == Catch::Approx(0.72f).margin(1e-4f));
    CHECK(loaded->params[ln::ParamId::lane1Hz] == Catch::Approx(0.65f).margin(1e-4f));
    CHECK(loaded->curves[0] == p.curves[0]);
}

TEST_CASE("Lineage Free-Hz linked follower inherits clock and scales ratio", "[core]") {
    auto p = ln::Params::defaults();
    p[ln::ParamId::lane1Mode] = static_cast<float>(ln::LaneMode::freeRun);
    p[ln::ParamId::lane1Hz] = 0.80f;
    p[ln::ParamId::lane2Follow] = 1.0f; // Follow Lane 1
    p[ln::ParamId::lane2Ratio] = 4.0f;  // x2
    p[ln::ParamId::lane2Mode] = static_cast<float>(ln::LaneMode::sync);

    CHECK(ln::laneUsesFreeClock(p, 0));
    CHECK(ln::laneUsesFreeClock(p, 1));
    CHECK(ln::laneRateHz(p, 0) == Catch::Approx(0.80).margin(1e-6));
    CHECK(ln::laneRateHz(p, 1) == Catch::Approx(1.60).margin(1e-6));
}
