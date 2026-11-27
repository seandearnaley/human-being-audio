// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/params.h"
#include "dt/pattern.h"
#include "dt/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Datum parameter table round-trips normalised values and formats cleanly", "[core]") {
    for (const auto& s : dt::kParamSpecs) {
        const float n = dt::toNormalised(s, s.def);
        CHECK(n >= 0.0f);
        CHECK(n <= 1.0f);
        CHECK(dt::fromNormalised(s, n) == Catch::Approx(s.def).margin(1e-3f));
        CHECK(!dt::formatValue(s, s.def).empty());
    }
    CHECK(dt::noteNameForHz(55.0f) == "A1");
    CHECK(dt::formatValue(dt::spec(dt::ParamId::tuneHz), 55.0f).find("A1") != std::string::npos);
}

TEST_CASE("Datum preset JSON round-trips params, pitch/amp splines, kit slots, and step pattern", "[core]") {
    dt::Preset orig;
    orig.name = "Dan Worrall Test Kick";
    orig.category = "Neurofunk";
    orig.params[dt::ParamId::tuneHz] = 49.0f;
    orig.params[dt::ParamId::coreTopology] = static_cast<float>(dt::CoreTopology::membrane);
    orig.params[dt::ParamId::shaperType] = static_cast<float>(dt::ShaperType::asym);
    orig.params[dt::ParamId::zeroCross] = 1.0f;
    hba::dsp::insertEnvelopeNode(orig.pitchCurve, 0.42f, 0.24f, -0.3f, hba::dsp::EnvSegment::exp);
    orig.pattern.slots[2].tuneOffsetSt = 2.5f;
    orig.pattern.steps[4] = {104, 1, 2};

    const std::string json = dt::serializePreset(orig);
    std::string err;
    const auto parsed = dt::parsePreset(json, &err);
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == orig.name);
    CHECK(parsed->category == orig.category);
    CHECK(parsed->params[dt::ParamId::tuneHz] == Catch::Approx(49.0f));
    CHECK(parsed->params.choice(dt::ParamId::coreTopology) == static_cast<int>(dt::CoreTopology::membrane));
    CHECK(parsed->pitchCurve.count == orig.pitchCurve.count);
    CHECK(parsed->pattern.slots[2].tuneOffsetSt == Catch::Approx(2.5f));
    CHECK(parsed->pattern.steps[4].velocity == 104);
    CHECK(parsed->pattern.steps[4].ratchet == 2);
}
