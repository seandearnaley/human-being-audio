// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "tg/chord_matrix.h"
#include "tg/params.h"
#include "tg/preset.h"

#include <cmath>

using Catch::Approx;

TEST_CASE("All parameter specs round-trip normalised values cleanly", "[core][params]") {
    const auto& specs = tg::paramSpecs();
    REQUIRE(specs.size() == tg::kParamCount);
    REQUIRE(tg::kParamCount <= 64);

    for (const auto& s : specs) {
        const float defNorm = tg::toNormalised(s, s.def);
        REQUIRE(defNorm >= 0.0f);
        REQUIRE(defNorm <= 1.0f);
        const float back = tg::fromNormalised(s, defNorm);
        REQUIRE(back == Approx(s.def).margin(1e-2f));
        REQUIRE(!tg::formatValue(s, s.def).empty());
    }
}

TEST_CASE("Scale Gravity potential well is strictly monotonic and dwells at scale degrees", "[core][gravity]") {
    // F Phrygian (rootPc = 5, scale = 0): F (53) -> Gb (54) -> Ab (56) -> Bb (58) -> C (60)
    double prev = 52.0;
    for (int i = 0; i <= 800; ++i) {
        const double raw = 53.0 + static_cast<double>(i) * 0.01;
        const double warped = tg::applyGravity(raw, 5, 0, tg::GravityMode::scaleWells, 0.9, 41.0);
        REQUIRE(warped > prev);
        prev = warped;
    }
    // Exact scale degrees are fixed points!
    REQUIRE(tg::applyGravity(53.0, 5, 0, tg::GravityMode::scaleWells, 1.0, 41.0) == Approx(53.0).margin(1e-6));
    REQUIRE(tg::applyGravity(54.0, 5, 0, tg::GravityMode::scaleWells, 1.0, 41.0) == Approx(54.0).margin(1e-6));
    REQUIRE(tg::applyGravity(56.0, 5, 0, tg::GravityMode::scaleWells, 1.0, 41.0) == Approx(56.0).margin(1e-6));

    // Midpoint between Gb(54) and Ab(56) pulls closer to nearer scale degree when offset slightly toward Gb
    const double nearGb = tg::applyGravity(54.45, 5, 0, tg::GravityMode::scaleWells, 0.85, 41.0);
    REQUIRE(nearGb < 54.45);
    REQUIRE(nearGb > 54.0);
}

TEST_CASE("Just Intonation Gravity locks pure 3:2 fifth cleanly", "[core][gravity]") {
    const double anchor = 41.0;                              // F2
    const double pureFifth = anchor + 12.0 * std::log2(1.5); // ~48.01955
    const double locked = tg::applyGravity(48.0, 5, 0, tg::GravityMode::justIntonation, 1.0, anchor);
    REQUIRE(std::abs(locked - pureFifth) < std::abs(48.0 - pureFifth));
}

TEST_CASE("Tymoczko voice-leading solver eliminates voice crossings", "[core][voice-leading]") {
    tg::SceneMatrix m;
    m.offsets[0] = {0, 7, 12, 15, 19};
    // Intentionally scrambled Scene 1
    m.offsets[1] = {20, 14, 0, 12, 7};
    m.offsets[2] = {19, 15, 12, 7, 0};
    m.offsets[3] = {12, 0, 19, 7, 15};

    tg::optimizeVoiceLeading(m, false);
    REQUIRE(m.offsets[1][0] == Approx(0.0f));
    REQUIRE(m.offsets[1][1] == Approx(7.0f));
    REQUIRE(m.offsets[1][2] == Approx(12.0f));
    REQUIRE(m.offsets[1][3] == Approx(14.0f));
    REQUIRE(m.offsets[1][4] == Approx(20.0f));
}

TEST_CASE("Pedal-Steel Stagger differentiates arrival times and Anchor V1 locks bass", "[core][morph]") {
    const auto m = tg::matrixForPack(0);
    const auto unstaggered = tg::evaluateMorphPitches(m, 41.0, 5, 0, 0.16, 0.0, tg::GravityMode::scaleWells, 0.0,
                                                      tg::StaggerOrder::innerFirst, false, false);
    const auto staggered = tg::evaluateMorphPitches(m, 41.0, 5, 0, 0.16, 0.0, tg::GravityMode::scaleWells, 0.85,
                                                    tg::StaggerOrder::innerFirst, true, false);
    // Voice 0 anchored strictly at root offset (41.0)
    REQUIRE(staggered.pitches[0] == Approx(41.0).margin(1e-6));
    REQUIRE(unstaggered.pitches[0] < 41.0);
    // Outer voice 4 lags behind when staggered
    REQUIRE(staggered.pitches[4] != Approx(unstaggered.pitches[4]).margin(0.05));
}

TEST_CASE("Preset JSON codec preserves params, curves, and custom SceneMatrix", "[core][preset]") {
    tg::PresetData p;
    p.name = "Test Mobius";
    p.category = "Mobius";
    p.params.set(tg::ParamId::mode, 2.0f);
    p.params.set(tg::ParamId::gravity, 77.5f);
    p.curves[0] = tg::presetSBloomCurve();
    p.matrix = tg::matrixForPack(6);
    p.matrix.offsets[2][3] = 17.5f;
    p.matrix.isCustom = true;

    const auto dumped = tg::serialisePresetJson(p);
    const auto loaded = tg::parsePresetJson(dumped);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->name == "Test Mobius");
    REQUIRE(loaded->params.choice(tg::ParamId::mode) == 2);
    REQUIRE(loaded->params.real(tg::ParamId::gravity) == Approx(77.5f));
    REQUIRE(loaded->curves[0] == p.curves[0]);
    REQUIRE(loaded->matrix.isCustom);
    REQUIRE(loaded->matrix.offsets[2][3] == Approx(17.5f));
}

TEST_CASE("captureChordIntoScene captures 1..5 held MIDI notes cleanly and marks custom", "[core][capture]") {
    auto m = tg::matrixForPack(0);
    const std::array<int, 3> triad{41, 48, 54}; // F2, C3, Gb3 (Phrygian b9) relative to F2 (41)
    REQUIRE(tg::captureChordIntoScene(m, 1, triad.data(), 3, 41, false, false));
    REQUIRE(m.isCustom);
    REQUIRE(m.offsets[1][0] == Approx(0.0f));
    REQUIRE(m.offsets[1][1] == Approx(7.0f));
    REQUIRE(m.offsets[1][2] == Approx(13.0f));
    // Padded voices double lower tones an octave up
    REQUIRE(m.offsets[1][3] == Approx(12.0f));
    REQUIRE(m.offsets[1][4] == Approx(19.0f));
}
