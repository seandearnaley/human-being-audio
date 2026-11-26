// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hba/core/preset_json.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string_view>

namespace {
enum class TestKind { continuous, choice, toggle };

struct TestSpec {
    TestKind kind = TestKind::continuous;
    float low = 0.0f;
    float high = 1.0f;
    float def = 0.5f;
    std::span<const std::string_view> choices{};
};

float clampTest(const TestSpec& s, float v) { return std::clamp(v, s.low, s.high); }

constexpr std::array<std::string_view, 3> kChoices{"Alpha", "Beta", "Gamma"};
} // namespace

TEST_CASE("jsonStringOr and jsonBoolOr tolerate missing and mistyped JSON fields", "[core][json]") {
    const hba::core::Json j = {
        {"name", "Sub Lock"}, {"badName", 42}, {"enabled", true}, {"numBool", 1}, {"badBool", "yes"},
    };
    CHECK(hba::core::jsonStringOr(j, "name", "fallback") == "Sub Lock");
    CHECK(hba::core::jsonStringOr(j, "badName", "fallback") == "fallback");
    CHECK(hba::core::jsonStringOr(j, "missing", "fallback") == "fallback");
    CHECK(hba::core::jsonStringOr(hba::core::Json(123), "name", "fallback") == "fallback");

    CHECK(hba::core::jsonBoolOr(j, "enabled", false));
    CHECK_FALSE(hba::core::jsonBoolOr(j, "numBool", false));
    CHECK_FALSE(hba::core::jsonBoolOr(j, "badBool", false));
    CHECK(hba::core::jsonBoolOr(j, "missing", true));
}

TEST_CASE("readParamJson and writeParamJson guard NaN, Inf, choices, and empty choice lists", "[core][json]") {
    const TestSpec contSpec{.kind = TestKind::continuous, .low = 0.0f, .high = 1.0f, .def = 0.25f};
    const auto ok = hba::core::readParamJson(contSpec, hba::core::Json(0.75), clampTest);
    REQUIRE(ok.has_value());
    CHECK(*ok == Catch::Approx(0.75f));

    // Non-finite numbers and wrong types return nullopt / fall back to spec default.
    const auto nanVal =
        hba::core::readParamJson(contSpec, hba::core::Json(std::numeric_limits<double>::quiet_NaN()), clampTest);
    CHECK_FALSE(nanVal.has_value());

    const auto writtenInf = hba::core::writeParamJson(contSpec, std::numeric_limits<float>::infinity(), clampTest);
    CHECK(writtenInf.get<double>() == Catch::Approx(0.25));

    // Toggle round-trip.
    const TestSpec toggleSpec{.kind = TestKind::toggle, .low = 0.0f, .high = 1.0f, .def = 0.0f};
    CHECK(hba::core::readParamJson(toggleSpec, hba::core::Json(true), clampTest).value_or(-1.0f) ==
          Catch::Approx(1.0f));
    CHECK(hba::core::writeParamJson(toggleSpec, 1.0f, clampTest).get<bool>());

    // Choice clamping and empty choice safety.
    const TestSpec choiceSpec{.kind = TestKind::choice, .low = 0.0f, .high = 2.0f, .def = 1.0f, .choices = kChoices};
    CHECK(hba::core::writeParamJson(choiceSpec, 99.0f, clampTest).get<std::string>() == "Gamma");
    CHECK(hba::core::writeParamJson(choiceSpec, -5.0f, clampTest).get<std::string>() == "Alpha");
    CHECK(hba::core::readParamJson(choiceSpec, hba::core::Json("Beta"), clampTest).value_or(-1.0f) ==
          Catch::Approx(1.0f));
    CHECK_FALSE(hba::core::readParamJson(choiceSpec, hba::core::Json("Missing"), clampTest).has_value());

    const TestSpec emptyChoices{.kind = TestKind::choice, .low = 0.0f, .high = 0.0f, .def = 0.0f};
    const auto emptyJson = hba::core::writeParamJson(emptyChoices, 0.0f, clampTest);
    CHECK(emptyJson.is_string());
    CHECK(emptyJson.get<std::string>().empty());
}

TEST_CASE("Curve JSON round-trip clamps out-of-range segment types and rejects malformed input", "[core][json]") {
    hba::dsp::Curve c{};
    c.count = 3;
    c.points[0] = {0.0f, 0.2f, 0.0f, static_cast<hba::dsp::Segment>(99)};
    c.points[1] = {0.5f, 0.8f, 0.5f, hba::dsp::Segment::hold};
    c.points[2] = {0.9f, -0.4f, -0.5f, hba::dsp::Segment::smooth};

    const auto encoded = hba::core::writeCurveJson(c);
    REQUIRE(encoded.is_array());
    REQUIRE(encoded.size() == 3);
    CHECK(encoded[0][3].get<std::string>() == "hold");
    CHECK(encoded[1][3].get<std::string>() == "hold");
    CHECK(encoded[2][3].get<std::string>() == "smooth");

    const auto decoded = hba::core::readCurveJson(encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded->count == 3);
    CHECK(decoded->points[1].y == Catch::Approx(0.8f));
    CHECK(decoded->points[1].segment == hba::dsp::Segment::hold);
    CHECK(decoded->points[2].segment == hba::dsp::Segment::smooth);

    CHECK_FALSE(hba::core::readCurveJson(hba::core::Json("invalid")).has_value());
    CHECK_FALSE(hba::core::parseCurveJson("{bad json").has_value());
    CHECK(hba::core::laneOfCurveKey("lane3Curve", 4) == 2);
    CHECK(hba::core::laneOfCurveKey("lane9Curve", 4) == -1);
}
