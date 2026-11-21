#include "tf/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace tf;

TEST_CASE("a preset survives a round trip") {
    Preset preset;
    preset.name = "Coupled Reese";
    preset.category = "Reese";
    preset.params[ParamId::coupling] = 0.9f;
    preset.params[ParamId::vowelA] = 3.0f;
    preset.params[ParamId::limit] = 0.0f;
    preset.params[ParamId::aDetune] = -12.5f;

    const auto parsed = parsePreset(serialisePreset(preset));
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "Coupled Reese");
    CHECK(parsed->category == "Reese");
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        CAPTURE(kParams[i].key);
        CHECK(parsed->params.values[i] == Approx(preset.params.values[i]));
    }
}

TEST_CASE("missing keys take defaults") {
    const auto parsed = parsePreset(R"({"name":"Sparse","coupling":0.1,"vowelB":"i","phaseReset":false})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->params[ParamId::coupling] == Approx(0.1f));
    CHECK(parsed->params.choice(ParamId::vowelB) == 3);
    CHECK_FALSE(parsed->params.on(ParamId::phaseReset));
    CHECK(parsed->params[ParamId::beat] == Approx(spec(ParamId::beat).def));
}

TEST_CASE("out-of-range numbers are clamped") {
    const auto parsed = parsePreset(R"({"coupling":7,"aDetune":-900})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->params[ParamId::coupling] == Approx(1.0f));
    CHECK(parsed->params[ParamId::aDetune] == Approx(-50.0f));
}

TEST_CASE("bad presets are rejected with a reason") {
    std::string error;
    CHECK_FALSE(parsePreset("[1,2]", &error).has_value());
    CHECK(error == "preset is not a JSON object");
    CHECK_FALSE(parsePreset(R"({"wobble":1})", &error).has_value());
    CHECK(error == "unknown parameter \"wobble\"");
    CHECK_FALSE(parsePreset(R"({"vowelA":"x"})", &error).has_value());
    CHECK(error == "bad value for \"vowelA\"");
    CHECK_FALSE(parsePreset("not json", &error).has_value());
}

TEST_CASE("a drawn curve travels with a preset whose lane plays it") {
    Preset preset;
    preset.name = "Drawn";
    preset.params[laneParam(1, LaneField::shape)] = static_cast<float>(LaneShape::custom);
    preset.curves[1] = curves::make({ { 0.0f, -1.0f, 0.5f, Segment::line },
                                      { 0.25f, 0.5f, 0.0f, Segment::hold },
                                      { 0.6f, 1.0f, 0.0f, Segment::smooth } });

    const auto json = serialisePreset(preset);
    CHECK(json.find("lane2Curve") != std::string::npos);
    CHECK(json.find("lane1Curve") == std::string::npos);  // lane 1 is not Custom

    const auto parsed = parsePreset(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->curves[1] == preset.curves[1]);
    CHECK(parsed->curves[0] == defaultLaneCurves()[0]);
}

TEST_CASE("a malformed curve is rejected with a reason") {
    std::string error;
    CHECK_FALSE(parsePreset(R"({"lane1Curve":[[0,0,0,"wavy"]]})", &error).has_value());
    CHECK(error == "bad value for \"lane1Curve\"");
    CHECK_FALSE(parsePreset(R"({"lane1Curve":[[0,0]]})", &error).has_value());
    CHECK_FALSE(parsePreset(R"({"lane1Curve":[]})", &error).has_value());
}
