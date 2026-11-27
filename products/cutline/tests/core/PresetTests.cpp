// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_json.h"
#include "cl/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cl;
using Catch::Approx;

TEST_CASE("A preset round-trips its parameters, curves and bank", "[preset]") {
    Preset orig;
    orig.name = "Two-Step";
    orig.category = "Club";
    orig.params[ParamId::density] = 0.72f;
    orig.params[ParamId::swing] = 0.44f;
    orig.params[ParamId::fillEvery] = 2.0f;
    orig.params[ParamId::noteMap] = static_cast<float>(NoteMap::strikeBody);
    orig.curves[1] = curves::sine();
    orig.bank.scenes[1].bars = 2;
    orig.bank.scenes[1].lanes[6].length = 7;
    orig.bank.scenes[1].lanes[6].steps[2].velocity = 71;
    orig.bank.scenes[1].lanes[6].steps[2].flam = true;
    orig.bank.scenes[1].fill.beats = 1;
    orig.bank.scenes[1].fill.steps[1][14].velocity = 118;
    orig.bank.slicerLength = 12;
    orig.bank.slicerSteps[3].ratchet = 3;
    sanitise(orig.bank);

    const std::string json = serialisePreset(orig);
    CHECK(json.find("\"format\": 3") != std::string::npos);

    std::string error;
    const auto parsed = parsePreset(json, &error);
    INFO(error);
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "Two-Step");
    CHECK(parsed->category == "Club");
    CHECK(parsed->params[ParamId::density] == Approx(0.72f).margin(1e-3f));
    CHECK(parsed->params[ParamId::swing] == Approx(0.44f).margin(1e-3f));
    CHECK(parsed->params.choice(ParamId::fillEvery) == 2);
    CHECK(parsed->params.choice(ParamId::noteMap) == static_cast<int>(NoteMap::strikeBody));
    CHECK(parsed->curves[1] == orig.curves[1]);
    CHECK(parsed->bank == orig.bank);
}

TEST_CASE("A Cutline 2 preset is rejected with a clear message", "[preset]") {
    std::string error;
    CHECK_FALSE(parsePreset(R"({"name":"Amen Chop 172","category":"Breaks","style":"Amen","gravity":0.24})", &error));
    CHECK(error == kOldFormatError);
    CHECK_FALSE(parsePreset(R"({"format":2,"name":"Old"})", &error));
    CHECK(error == kOldFormatError);
    CHECK_FALSE(parsePreset(R"({"format":"3","name":"Old"})", &error));
    CHECK(error == kOldFormatError);
}

TEST_CASE("A Cutline 3 preset is strict about what it contains", "[preset]") {
    std::string error;
    CHECK_FALSE(parsePreset("not json", &error));
    CHECK_FALSE(parsePreset(R"({"format":3,"gravity":0.2})", &error));
    CHECK(error.find("gravity") != std::string::npos);
    CHECK_FALSE(parsePreset(R"({"format":3,"density":"loud"})", &error));
    CHECK_FALSE(parsePreset(R"({"format":3,"bank":{"scenes":[{"bars":3}]}})", &error));
    CHECK(error.find("bars") != std::string::npos);
}

TEST_CASE("A preset without a bank gets the default bank", "[preset]") {
    const auto parsed = parsePreset(R"({"format":3,"name":"Bare","swing":0.2})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->bank == GrooveBank{});
    CHECK(parsed->params[ParamId::swing] == Approx(0.2f));
}
