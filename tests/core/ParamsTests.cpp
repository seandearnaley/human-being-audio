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
    // 0.2: Reach wide open by default, a light Weight, feedback in cycles only.
    CHECK(p[ParamId::reach] == Approx(96.0f));
    CHECK(spec(ParamId::reach).min == Approx(2.0f));
    CHECK(spec(ParamId::reach).max == Approx(256.0f));
    CHECK(p[ParamId::weight] == Approx(0.3f));
    CHECK(spec(ParamId::fbTime).choices.size() == 5);
    CHECK(kFbCycles[static_cast<std::size_t>(p.choice(ParamId::fbTime))] == 1.0);
    // The sub/growl split is a harmonic of the note: 2.75 is 120 Hz at F1.
    CHECK(p[ParamId::subSplit] == Approx(2.75f));
    CHECK(spec(ParamId::subSplit).min == Approx(1.5f));
    CHECK(spec(ParamId::subSplit).max == Approx(6.0f));
    CHECK(spec(ParamId::subSplit).unit == Unit::harmonic);
    CHECK(findParam("subSplit") == ParamId::subSplit);
    CHECK_FALSE(findParam("subFreq").has_value());
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
    CHECK(formatValue(spec(ParamId::reach), 96.0f) == "\u00d796");
    CHECK(formatValue(spec(ParamId::fbTime), 2.0f) == "1 cyc");
    CHECK(formatValue(spec(ParamId::fbTime), 4.0f) == "4 cyc");
    CHECK(formatValue(spec(ParamId::sculpt), 6.0f) == "\u00d76.0");
    CHECK(formatValue(spec(ParamId::subSplit), 4.0f) == "\u00d74.0");
}

TEST_CASE("the eight macros are the first eight parameters, in knob order") {
    const std::array<std::string_view, 8> macros { "coupling", "beat", "reach", "tract", "warp", "sculpt", "motion", "width" };
    for (std::size_t i = 0; i < macros.size(); ++i) {
        CHECK(kParams[i].key == macros[i]);
        CHECK(kParams[i].automatable);
    }
}
