#include "tf/params.h"

#include <array>
#include <string_view>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

using Catch::Approx;
using namespace tf;

TEST_CASE("every parameter round-trips through its normalised position") {
    const auto index = GENERATE(range(0, kParamCount));
    const auto& p = kParams[static_cast<std::size_t>(index)];
    CAPTURE(p.key);

    CHECK(fromNormalised(p, 0.0f) == Approx(p.min));
    CHECK(fromNormalised(p, 1.0f) == Approx(p.max));
    CHECK(fromNormalised(p, toNormalised(p, p.def)) == Approx(p.def).margin(1e-3));

    const auto position = GENERATE(0.1f, 0.37f, 0.5f, 0.93f);
    const float real = fromNormalised(p, position);
    CHECK(real >= p.min);
    CHECK(real <= p.max);
    if (p.kind == ParamKind::continuous) CHECK(toNormalised(p, real) == Approx(position).margin(1e-4));
}

TEST_CASE("parameters are found by key") {
    CHECK(findParam("coupling") == ParamId::coupling);
    CHECK(findParam("lane3Depth") == laneParam(2, LaneField::depth));
    CHECK_FALSE(findParam("nope").has_value());
}

TEST_CASE("defaults match the design") {
    const auto p = Params::defaults();
    CHECK(p[ParamId::coupling] == Approx(0.72f));
    CHECK(p.on(ParamId::phaseReset));
    CHECK(choices::vowel[static_cast<std::size_t>(p.choice(ParamId::vowelA))] == "æ");
    CHECK(kLaneRateQuarters[static_cast<std::size_t>(p.choice(ParamId::lane2Rate))] == 1.0);
}

TEST_CASE("values are formatted for display") {
    CHECK(formatValue(spec(ParamId::coupling), 0.72f) == "72 %");
    CHECK(formatValue(spec(ParamId::aPm), -0.48f) == "-48 %");
    CHECK(formatValue(spec(ParamId::aDetune), 7.2f) == "+7.2 ct");
    CHECK(formatValue(spec(ParamId::glide), 12.5f) == "12.5 ms");
    CHECK(formatValue(spec(ParamId::xover), 100.0f) == "100 Hz");
    CHECK(formatValue(spec(ParamId::sideGain), 4.2f) == "+4.2 dB");
    CHECK(formatValue(spec(ParamId::ceiling), -0.3f) == "-0.3 dB");
    CHECK(formatValue(spec(ParamId::aRatio), 1.0f) == "1.000");
    CHECK(formatValue(spec(ParamId::bPhase), 180.0f) == "180°");
    CHECK(formatValue(spec(ParamId::vowelB), 5.0f) == "u");
    CHECK(formatValue(spec(ParamId::limit), 1.0f) == "On");
    CHECK(formatValue(spec(ParamId::reach), 16.0f) == "\u00d716");
    CHECK(formatValue(spec(ParamId::fbTime), 2.0f) == "1 cyc");
    CHECK(formatValue(spec(ParamId::sculpt), 6.0f) == "\u00d76.0");
}

TEST_CASE("the eight macros are the first eight parameters, in knob order") {
    const std::array<std::string_view, 8> macros { "coupling", "beat", "reach", "tract", "warp", "sculpt", "motion", "width" };
    for (std::size_t i = 0; i < macros.size(); ++i) {
        CHECK(kParams[i].key == macros[i]);
        CHECK(kParams[i].automatable);
    }
}
